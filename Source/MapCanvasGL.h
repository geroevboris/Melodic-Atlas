#pragma once
#include "SamplerEngine.h"
#include <unordered_map>

namespace Atlas
{
    class MapCanvasComponent : public juce::Component,
                               private juce::Timer
    {
    public:
        explicit MapCanvasComponent(SamplerEngine& engine)
            : samplerEngine(engine)
        {
            formatManager.registerBasicFormats();
            startTimerHz(60);
        }

        ~MapCanvasComponent() override
        {
            stopTimer();
        }

        void setSampleDatabase(const std::vector<SampleRecord>* dbPtr)
        {
            database = dbPtr;
            activeSampleIndex = -1;
            hoveredSampleIndex = -1;
            auditionHistory.clear();
            peakCache.clear();
            repaint();
        }

        void setViewFilter(bool loopsTab, int clusterFilter, uint32_t subFilterMask)
        {
            showLoopsOnly = loopsTab;
            activeClusterFilter = clusterFilter;
            activeSubMask = subFilterMask;
            repaint();
        }

        int getActiveSampleIndex() const { return activeSampleIndex; }

        // Вызывается при клике на пэд в банке: подсвечивает точку на карте и открывает её карточку волны
        void selectSampleFromPad(int sampleIdx)
        {
            if (database == nullptr || sampleIdx < 0 || sampleIdx >= (int)database->size())
                return;

            activeSampleIndex = sampleIdx;
            ensureWaveformPeaksLoaded(sampleIdx);
            repaint();
        }

        std::function<void(int sampleIdx)> onSampleRightClicked;
        std::function<void(int sampleIdx, juce::Point<int> globalMousePos, bool dragFinished)> onSampleDragged;

        void paint(juce::Graphics& g) override
        {
            const float w = (float)getWidth();
            const float h = (float)getHeight();
            const float mapBottomY = h - 26.0f;

            // 1. Фон карты и координатная сетка
            g.setColour(juce::Colour(0xff0f1319));
            g.fillRect(0.0f, 0.0f, w, mapBottomY);

            g.setColour(juce::Colour(0xff171d26));
            const int gridCols = 16;
            const int gridRows = 8;
            for (int c = 1; c < gridCols; ++c)
            {
                float gx = (w / (float)gridCols) * (float)c;
                g.drawVerticalLine((int)gx, 0.0f, mapBottomY);
            }
            for (int r = 1; r < gridRows; ++r)
            {
                float gy = (mapBottomY / (float)gridRows) * (float)r;
                g.drawHorizontalLine((int)gy, 0.0f, w);
            }

            // 2. Отрисовка точек сэмплов
            if (database != nullptr)
            {
                for (int i = 0; i < (int)database->size(); ++i)
                {
                    const auto& rec = (*database)[(size_t)i];
                    if (rec.isLoop != showLoopsOnly)
                        continue;

                    auto pt = mapToScreen(rec.mapX, rec.mapY);
                    if (pt.x < -12.0f || pt.x > w + 12.0f || pt.y < 24.0f || pt.y > mapBottomY - 6.0f)
                        continue;

                    bool passesFilter = matchesCurrentFilter(rec);
                    bool isActive = (i == activeSampleIndex);
                    bool isHovered = (i == hoveredSampleIndex);

                    juce::Colour dotCol = MusicMath::getClusterColour(rec.cluster, rec.toneShadeBucket);
                    if (!passesFilter)
                        dotCol = dotCol.withAlpha(0.14f);
                    else if (isActive || isHovered)
                        dotCol = dotCol.brighter(0.45f);
                    else
                        dotCol = dotCol.withAlpha(0.88f);

                    float radius = isActive ? 5.2f : (isHovered ? 4.4f : 3.3f);

                    if (isActive)
                    {
                        g.setColour(dotCol.withAlpha(0.32f));
                        g.fillEllipse(pt.x - 9.5f, pt.y - 9.5f, 19.0f, 19.0f);
                        g.setColour(dotCol);
                        g.drawEllipse(pt.x - 8.5f, pt.y - 8.5f, 17.0f, 17.0f, 1.4f);
                    }

                    g.setColour(dotCol);
                    g.fillEllipse(pt.x - radius, pt.y - radius, radius * 2.0f, radius * 2.0f);
                }
            }

            // 3. Всплывающая карточка волны для выбранной точки (с реальным белым Playhead)
            activeCardRect = {};
            if (database != nullptr && activeSampleIndex >= 0 && activeSampleIndex < (int)database->size())
            {
                const auto& rec = (*database)[(size_t)activeSampleIndex];
                if (rec.isLoop == showLoopsOnly)
                {
                    auto pt = mapToScreen(rec.mapX, rec.mapY);
                    float cardW = 216.0f;
                    float cardH = 56.0f;

                    float cx = pt.x + 14.0f;
                    float cy = pt.y - cardH - 8.0f;
                    if (cx + cardW > w - 12.0f) cx = pt.x - cardW - 14.0f;
                    if (cy < 32.0f)             cy = pt.y + 12.0f;
                    if (cy + cardH > mapBottomY - 6.0f) cy = mapBottomY - cardH - 6.0f;

                    activeCardRect = { cx, cy, cardW, cardH };
                    juce::Colour accent = MusicMath::getClusterColour(rec.cluster, rec.toneShadeBucket);

                    g.setColour(juce::Colour(0xf2161b22));
                    g.fillRoundedRectangle(activeCardRect, 4.0f);
                    g.setColour(accent.withAlpha(0.85f));
                    g.drawRoundedRectangle(activeCardRect, 4.0f, 1.3f);

                    // Мини-волна внутри карточки
                    juce::Rectangle<float> waveBox(cx + 8.0f, cy + 7.0f, cardW - 16.0f, 24.0f);
                    const auto& peaks = ensureWaveformPeaksLoaded(activeSampleIndex);
                    float playheadNorm = samplerEngine.isPreviewActive() ? samplerEngine.getPreviewPlayheadNorm() : 0.0f;

                    if (!peaks.empty())
                    {
                        float barW = waveBox.getWidth() / (float)peaks.size();
                        float midY = waveBox.getCentreY();
                        for (size_t k = 0; k < peaks.size(); ++k)
                        {
                            float normK = (float)k / (float)peaks.size();
                            float barH = std::max(2.0f, peaks[k] * waveBox.getHeight() * 0.92f);
                            float bx = waveBox.getX() + (float)k * barW;

                            bool passed = (samplerEngine.isPreviewActive() && normK <= playheadNorm);
                            g.setColour(passed ? accent.brighter(0.55f) : accent.withAlpha(0.75f));
                            g.fillRect(bx, midY - barH * 0.5f, std::max(1.2f, barW - 0.8f), barH);
                        }
                    }

                    // Белая полоса реальной позиции воспроизведения (Playhead)
                    if (samplerEngine.isPreviewActive())
                    {
                        float px = waveBox.getX() + juce::jlimit(0.0f, 1.0f, playheadNorm) * waveBox.getWidth();
                        g.setColour(juce::Colours::white);
                        g.drawLine(px, waveBox.getY() - 1.0f, px, waveBox.getBottom() + 1.0f, 1.8f);
                    }

                    // Подпись с именем сэмпла и нотой
                    g.setColour(juce::Colour(0xffe6eef8));
                    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::bold));
                    juce::String noteStr = MusicMath::getNoteName(rec.pitchClass);
                    juce::String cardTitle = rec.name.substring(0, 16) + " [" + noteStr + "] DRAG ->";
                    g.drawText(cardTitle, (int)cx + 8, (int)(cy + cardH - 20.0f), (int)cardW - 16, 16,
                               juce::Justification::centredLeft, true);
                }
            }

            // Призрак карточки при перетаскивании в пэд или DAW
            if (isDraggingCard && database != nullptr && activeSampleIndex >= 0 && activeSampleIndex < (int)database->size())
            {
                const auto& rec = (*database)[(size_t)activeSampleIndex];
                juce::Rectangle<float> ghost(currentDragPos.x - 70.0f, currentDragPos.y - 18.0f, 140.0f, 32.0f);
                juce::Colour accent = MusicMath::getClusterColour(rec.cluster, rec.toneShadeBucket);

                g.setColour(juce::Colour(0xcc182029));
                g.fillRoundedRectangle(ghost, 4.0f);
                g.setColour(accent);
                g.drawRoundedRectangle(ghost, 4.0f, 1.5f);
                g.setColour(juce::Colours::white);
                g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::bold));
                g.drawText(rec.name.substring(0, 16), ghost.reduced(6.0f, 0.0f), juce::Justification::centred, true);
            }

            // 4. Верхняя плашка легенды и кнопки масштабирования (СТРОГО ПОВЕРХ ВСЕХ ТОЧЕК)
            drawTopOverlayAndZoomControls(g, w);

            // 5. Нижняя строка истории прослушивания и инфо
            drawBottomHistoryBar(g, w, h, mapBottomY);

            g.setColour(juce::Colour(0xff262e3b));
            g.drawRect(0.0f, 0.0f, w, mapBottomY, 1.0f);
        }

        void mouseMove(const juce::MouseEvent& e) override
        {
            if (zoomOutRect.contains(e.position) || zoomInRect.contains(e.position) || zoomResetRect.contains(e.position))
            {
                if (hoveredSampleIndex != -1)
                {
                    hoveredSampleIndex = -1;
                    repaint();
                }
                return;
            }

            int hit = findNearestSample(e.position, 12.0f);
            if (hit != hoveredSampleIndex)
            {
                hoveredSampleIndex = hit;
                repaint();
            }
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            auto pt = e.position;
            isDraggingCard = false;
            isPanningMap = false;
            isAuditionSweeping = false;
            didPanDuringRightDrag = false;
            lastPanPos = pt;

            // 1. В первую очередь проверяем кнопки масштабирования (чтобы точки под ними никогда не мешали)
            if (zoomOutRect.contains(pt))
            {
                setZoomAroundCenter(zoomFactor - 0.25f);
                return;
            }
            if (zoomInRect.contains(pt))
            {
                setZoomAroundCenter(zoomFactor + 0.25f);
                return;
            }
            if (zoomResetRect.contains(pt))
            {
                zoomFactor = 1.0f;
                panOffset = { 0.0f, 0.0f };
                repaint();
                return;
            }

            // 2. Проверка клика по кружочкам истории прослушивания внизу карты
            const float mapBottomY = (float)getHeight() - 26.0f;
            if (pt.y >= mapBottomY)
            {
                for (size_t i = 0; i < historyNodeRects.size(); ++i)
                {
                    if (historyNodeRects[i].contains(pt) && i < auditionHistory.size())
                    {
                        triggerSamplePreview(auditionHistory[i], true);
                        return;
                    }
                }
                return;
            }

            // 3. Правый клик (или зажатие двумя пальцами на трекпаде)
            if (e.mods.isPopupMenu())
            {
                isPanningMap = true;
                rightClickOnCard = (activeCardRect.contains(pt) && activeSampleIndex >= 0);
                rightClickSampleIdx = rightClickOnCard ? activeSampleIndex : findNearestSample(pt, 14.0f);
                return;
            }

            // 4. Левый клик по всплывающей карточке волны -> начало перетаскивания (Drag) или повторное превью
            if (activeCardRect.contains(pt) && activeSampleIndex >= 0)
            {
                isDraggingCard = true;
                currentDragPos = pt;
                triggerSamplePreview(activeSampleIndex, true);
                return;
            }

            // 5. Левый клик по точке на карте -> мгновенный старт превью с 0.0 (даже при повторном клике на ту же точку!)
            int hit = findNearestSample(pt, 15.0f);
            if (hit >= 0)
            {
                isAuditionSweeping = true;
                lastSweptSampleIndex = hit;
                triggerSamplePreview(hit, true);
            }
            else
            {
                // Если кликнули в пустое место карты — можно как свайпать по точкам, так и панорамировать с Alt/Shift
                if (e.mods.isShiftDown() || e.mods.isAltDown() || e.mods.isMiddleButtonDown())
                    isPanningMap = true;
                else
                    isAuditionSweeping = true;
            }
        }

        void mouseDrag(const juce::MouseEvent& e) override
        {
            auto pt = e.position;

            // Перетаскивание карточки сэмпла в пэд или в DAW
            if (isDraggingCard && activeSampleIndex >= 0)
            {
                currentDragPos = pt;
                if (onSampleDragged)
                    onSampleDragged(activeSampleIndex, e.getScreenPosition(), false);
                repaint();
                return;
            }

            // Панорамирование карты (с инвертированной вертикалью по твоему запросу)
            if (isPanningMap)
            {
                if (pt.getDistanceFrom(lastPanPos) > 2.5f)
                    didPanDuringRightDrag = true;

                float dx = pt.x - lastPanPos.x;
                float dy = pt.y - lastPanPos.y;
                lastPanPos = pt;

                panOffset.x += dx;
                panOffset.y -= dy; // Инверсия вертикального движения при ведении двумя пальцами
                clampPanOffset();
                repaint();
                return;
            }

            // Скольжение курсором по точкам карты (бесшовное переключение звуков через кроссфейд)
            if (isAuditionSweeping)
            {
                int hit = findNearestSample(pt, 15.0f);
                if (hit >= 0 && hit != lastSweptSampleIndex)
                {
                    lastSweptSampleIndex = hit;
                    triggerSamplePreview(hit, false);
                }
            }
        }

        void mouseUp(const juce::MouseEvent& e) override
        {
            if (isDraggingCard && activeSampleIndex >= 0)
            {
                isDraggingCard = false;
                if (onSampleDragged)
                    onSampleDragged(activeSampleIndex, e.getScreenPosition(), true);
                samplerEngine.stopPreview();
                repaint();
                return;
            }

            // Если это был короткий правый клик без панорамирования
            if (e.mods.isPopupMenu() && !didPanDuringRightDrag)
            {
                if (rightClickOnCard && activeSampleIndex >= 0 && database != nullptr)
                {
                    showWaveformCardContextMenu(activeSampleIndex, e.getScreenPosition());
                }
                else if (rightClickSampleIdx >= 0 && onSampleRightClicked)
                {
                    onSampleRightClicked(rightClickSampleIdx);
                }
            }

            isPanningMap = false;
            isAuditionSweeping = false;
            lastSweptSampleIndex = -1;
            samplerEngine.stopPreview();
        }

        void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
        {
            // Если зажат Cmd/Ctrl или крутится обычное колесо без горизонтальной составляющей — плавный зум
            if (e.mods.isCommandDown() || e.mods.isCtrlDown() || std::abs(wheel.deltaX) < 0.0001f)
            {
                float step = wheel.deltaY * 0.65f;
                setZoomAroundCenter(zoomFactor + step);
            }
            else
            {
                // Двухпальцевый свайп по трекпаду — панорамирование с инверсией вертикали
                panOffset.x += wheel.deltaX * 160.0f;
                panOffset.y -= wheel.deltaY * 160.0f;
                clampPanOffset();
                repaint();
            }
        }

    private:
        void timerCallback() override
        {
            if (samplerEngine.isPreviewActive())
                repaint();
        }

        void drawTopOverlayAndZoomControls(juce::Graphics& g, float w)
        {
            const juce::Font monoSmall(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::bold);

            // Непрозрачная тёмная подложка под верхнюю строку легенды и кнопок зума, чтобы точки не просвечивали
            g.setColour(juce::Colour(0xe60f1319));
            g.fillRect(1.0f, 1.0f, w - 2.0f, 26.0f);

            // Легенда кластеров слева
            float lx = 10.0f;
            for (int c = 0; c < 8; ++c)
            {
                auto cg = static_cast<ClusterGroup>(c);
                for (int shade = 0; shade < 3; ++shade)
                {
                    g.setColour(MusicMath::getClusterColour(cg, shade));
                    g.fillEllipse(lx + (float)shade * 4.5f, 11.0f, 4.0f, 4.0f);
                }
                lx += 15.0f;
                g.setColour(juce::Colour(0xff9aa7b8));
                g.setFont(monoSmall);
                juce::String cname = MusicMath::getClusterName(cg);
                float tw = (float)cname.length() * 6.5f + 10.0f;
                g.drawText(cname, (int)lx, 4, (int)tw, 18, juce::Justification::centredLeft, false);
                lx += tw;
            }

            // Кнопки масштабирования в правом верхнем углу (всегда поверх точек!)
            zoomOutRect   = { w - 106.0f, 4.0f, 24.0f, 20.0f };
            zoomInRect    = { w - 78.0f,  4.0f, 24.0f, 20.0f };
            zoomResetRect = { w - 50.0f,  4.0f, 42.0f, 20.0f };

            auto drawZoomBtn = [&](juce::Rectangle<float> r, const juce::String& txt)
            {
                g.setColour(juce::Colour(0xff1e2530));
                g.fillRoundedRectangle(r, 3.0f);
                g.setColour(juce::Colour(0xff455264));
                g.drawRoundedRectangle(r, 3.0f, 1.0f);
                g.setColour(juce::Colour(0xffe2eaf2));
                g.setFont(monoSmall);
                g.drawText(txt, r, juce::Justification::centred, false);
            };

            drawZoomBtn(zoomOutRect,   "-");
            drawZoomBtn(zoomInRect,    "+");
            drawZoomBtn(zoomResetRect, juce::String(zoomFactor, 1) + "x");
        }

        void drawBottomHistoryBar(juce::Graphics& g, float w, float h, float mapBottomY)
        {
            const juce::Font monoSmall(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain);

            g.setColour(juce::Colour(0xff131820));
            g.fillRect(0.0f, mapBottomY, w, h - mapBottomY);

            g.setColour(juce::Colour(0xff7f8c9d));
            g.setFont(monoSmall);
            g.drawText("AUDITION:", 8, (int)mapBottomY, 64, 26, juce::Justification::centredLeft, false);

            historyNodeRects.clear();
            float hx = 74.0f;
            float hy = mapBottomY + 13.0f;

            for (size_t i = 0; i < auditionHistory.size(); ++i)
            {
                int sIdx = auditionHistory[i];
                bool isCurrent = (sIdx == activeSampleIndex);

                if (i > 0)
                {
                    g.setColour(juce::Colour(0xff343e4d));
                    g.drawLine(hx - 8.0f, hy, hx - 3.0f, hy, 1.0f);
                }

                juce::Rectangle<float> nodeRect(hx - 4.0f, hy - 4.0f, 8.0f, 8.0f);
                historyNodeRects.push_back(nodeRect.expanded(3.0f));

                if (isCurrent)
                {
                    g.setColour(juce::Colours::white);
                    g.fillEllipse(nodeRect);
                }
                else
                {
                    g.setColour(juce::Colour(0xff7f8c9d));
                    g.drawEllipse(nodeRect, 1.2f);
                }
                hx += 16.0f;
            }

            if (database != nullptr && activeSampleIndex >= 0 && activeSampleIndex < (int)database->size())
            {
                const auto& rec = (*database)[(size_t)activeSampleIndex];
                juce::String info = rec.name + " [" + MusicMath::getNoteName(rec.pitchClass) + " | "
                                  + MusicMath::getClusterName(rec.cluster) + " | "
                                  + (rec.isLoop ? "LOOP" : "1-SHOT") + "] (R-Click Card -> Finder)";
                g.setColour(juce::Colour(0xffb8c4d4));
                g.drawText(info, (int)(w * 0.42f), (int)mapBottomY, (int)(w * 0.57f) - 8, 26,
                           juce::Justification::centredRight, true);
            }
        }

        void showWaveformCardContextMenu(int sampleIdx, juce::Point<int> screenPt)
        {
            if (database == nullptr || sampleIdx < 0 || sampleIdx >= (int)database->size())
                return;

            const auto& rec = (*database)[(size_t)sampleIdx];
            juce::PopupMenu menu;
            menu.addItem(1, "Show in Finder / Explorer");
            menu.addItem(2, "Send to Selected Pad");

            menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({ screenPt.x, screenPt.y, 2, 2 }),
                               [this, sampleIdx, rec](int res)
                               {
                                   if (res == 1 && rec.file.existsAsFile())
                                       rec.file.revealToUser();
                                   else if (res == 2 && onSampleRightClicked)
                                       onSampleRightClicked(sampleIdx);
                               });
        }

        void triggerSamplePreview(int sampleIdx, bool forceRestart)
        {
            if (database == nullptr || sampleIdx < 0 || sampleIdx >= (int)database->size())
                return;

            if (!forceRestart && activeSampleIndex == sampleIdx && samplerEngine.isPreviewActive())
                return;

            activeSampleIndex = sampleIdx;
            ensureWaveformPeaksLoaded(sampleIdx);

            // Добавляем в историю прослушивания карты
            auto it = std::find(auditionHistory.begin(), auditionHistory.end(), sampleIdx);
            if (it != auditionHistory.end())
                auditionHistory.erase(it);
            auditionHistory.push_back(sampleIdx);
            if (auditionHistory.size() > 16)
                auditionHistory.erase(auditionHistory.begin());

            samplerEngine.startPreview(sampleIdx, (*database)[(size_t)sampleIdx]);
            repaint();
        }

        const std::vector<float>& ensureWaveformPeaksLoaded(int sampleIdx)
        {
            static const std::vector<float> emptyPeaks;
            if (database == nullptr || sampleIdx < 0 || sampleIdx >= (int)database->size())
                return emptyPeaks;

            auto it = peakCache.find(sampleIdx);
            if (it != peakCache.end())
                return it->second;

            std::vector<float> peaks(44, 0.15f);
            const auto& file = (*database)[(size_t)sampleIdx].file;
            if (file.existsAsFile())
            {
                if (auto reader = std::unique_ptr<juce::AudioFormatReader>(formatManager.createReaderFor(file)))
                {
                    int len = (int)std::min<juce::int64>(reader->lengthInSamples, (juce::int64)(reader->sampleRate * 8.0));
                    if (len > 64)
                    {
                        juce::AudioBuffer<float> tmp(1, len);
                        reader->read(&tmp, 0, len, 0, true, false);
                        const float* d = tmp.getReadPointer(0);
                        int step = std::max(1, len / 44);
                        for (int b = 0; b < 44; ++b)
                        {
                            float m = 0.05f;
                            int s0 = b * step;
                            int s1 = std::min(len, s0 + step);
                            for (int i = s0; i < s1; ++i)
                                m = std::max(m, std::abs(d[i]));
                            peaks[(size_t)b] = juce::jlimit(0.08f, 1.0f, m);
                        }
                    }
                }
            }

            peakCache[sampleIdx] = std::move(peaks);
            return peakCache[sampleIdx];
        }

        bool matchesCurrentFilter(const SampleRecord& rec) const
        {
            if (activeClusterFilter >= 0 && (int)rec.cluster != activeClusterFilter)
                return false;
            if (activeSubMask != 0 && (rec.subMask & activeSubMask) != activeSubMask)
                return false;
            return true;
        }

        juce::Point<float> mapToScreen(float mx, float my) const
        {
            const float w = (float)getWidth();
            const float h = (float)getHeight() - 26.0f;
            const float padX = 28.0f;
            const float padY = 34.0f;

            float baseX = padX + mx * (w - padX * 2.0f);
            float baseY = padY + my * (h - padY - 16.0f);

            float cx = w * 0.5f;
            float cy = h * 0.5f;

            float sx = cx + (baseX - cx) * zoomFactor + panOffset.x;
            float sy = cy + (baseY - cy) * zoomFactor + panOffset.y;
            return { sx, sy };
        }

        int findNearestSample(juce::Point<float> mousePt, float maxRadiusPx) const
        {
            if (database == nullptr) return -1;
            const float mapBottomY = (float)getHeight() - 26.0f;
            if (mousePt.y >= mapBottomY) return -1;

            int bestIdx = -1;
            float bestDistSq = maxRadiusPx * maxRadiusPx;

            for (int i = 0; i < (int)database->size(); ++i)
            {
                const auto& rec = (*database)[(size_t)i];
                if (rec.isLoop != showLoopsOnly)
                    continue;
                if (!matchesCurrentFilter(rec))
                    continue;

                auto pt = mapToScreen(rec.mapX, rec.mapY);
                float dSq = mousePt.getDistanceSquaredFrom(pt);
                if (dSq < bestDistSq)
                {
                    bestDistSq = dSq;
                    bestIdx = i;
                }
            }
            return bestIdx;
        }

        void setZoomAroundCenter(float newZoom)
        {
            zoomFactor = juce::jlimit(1.0f, 4.0f, std::round(newZoom * 10.0f) * 0.1f);
            clampPanOffset();
            repaint();
        }

        void clampPanOffset()
        {
            float maxPanX = (float)getWidth() * (zoomFactor - 1.0f) * 0.65f + 40.0f;
            float maxPanY = (float)getHeight() * (zoomFactor - 1.0f) * 0.65f + 40.0f;
            panOffset.x = juce::jlimit(-maxPanX, maxPanX, panOffset.x);
            panOffset.y = juce::jlimit(-maxPanY, maxPanY, panOffset.y);
        }

        SamplerEngine& samplerEngine;
        juce::AudioFormatManager formatManager;
        const std::vector<SampleRecord>* database = nullptr;
        std::unordered_map<int, std::vector<float>> peakCache;

        bool showLoopsOnly = false;
        int activeClusterFilter = -1;
        uint32_t activeSubMask = 0;

        int activeSampleIndex = -1;
        int hoveredSampleIndex = -1;
        int lastSweptSampleIndex = -1;
        int rightClickSampleIdx = -1;
        bool rightClickOnCard = false;

        float zoomFactor = 1.0f;
        juce::Point<float> panOffset { 0.0f, 0.0f };
        juce::Point<float> lastPanPos;
        juce::Point<float> currentDragPos;

        bool isDraggingCard = false;
        bool isPanningMap = false;
        bool isAuditionSweeping = false;
        bool didPanDuringRightDrag = false;

        juce::Rectangle<float> activeCardRect;
        juce::Rectangle<float> zoomOutRect, zoomInRect, zoomResetRect;
        std::vector<int> auditionHistory;
        std::vector<juce::Rectangle<float>> historyNodeRects;
    };
}
