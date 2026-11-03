#pragma once
#include "AtlasTypes.h"
#include "SamplerEngine.h"
#include <deque>
#include <functional>

namespace Atlas
{
    class MapCanvasComponent : public juce::Component,
                               private juce::Timer
    {
    public:
        std::function<void(int sampleIdx)> onSampleTriggered;
        std::function<void(int sampleIdx)> onSampleRightClicked;
        std::function<void(int sampleIdx, juce::Point<int> screenPos, bool finished)> onSampleDragged;

        MapCanvasComponent(SamplerEngine& engineRef)
            : sampler(engineRef)
        {
            setOpaque(true);
            setWantsKeyboardFocus(true);
            startTimerHz(30);
        }

        void setSampleDatabase(const std::vector<SampleRecord>* dbPtr)
        {
            database = dbPtr;
            activeSampleIndex = -1;
            isHoldingPoint = false;
            playheadProgress = 0.0f;
            historyIndices.clear();
            repaint();
        }

        // selectedCluster: -1 = все группы, 0..7 = конкретный ClusterGroup
        void setViewFilter(bool showLoopsTab, int selectedCluster, uint32_t activeSubMask)
        {
            if (loopsTabActive != showLoopsTab)
            {
                sampler.stopAudition();
                isHoldingPoint = false;
            }

            loopsTabActive    = showLoopsTab;
            clusterFilterIdx  = selectedCluster;
            subFilterBitMask  = activeSubMask;
            repaint();
        }

        int getActiveSampleIndex() const { return activeSampleIndex; }

        void selectSampleByIndex(int idx, bool triggerAudition)
        {
            if (database == nullptr || idx < 0 || idx >= (int)database->size())
                return;

            activeSampleIndex = idx;
            if (triggerAudition)
            {
                isHoldingPoint = true;
                sampler.auditionSample(idx, (*database)[(size_t)idx], true, playheadProgress);
            }
            repaint();
        }

        void paint(juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat();
            const float historyHeight = 26.0f;
            const auto historyRect = bounds.removeFromBottom(historyHeight);
            const auto mapRect = bounds;

            // 1. Фон карты
            g.setColour(juce::Colour(0xff0b0e13));
            g.fillRect(mapRect);

            // 2. Тонкая координатная сетка
            drawSubtleGrid(g, mapRect);

            // 3. Легенда кластеров и кнопки зума сверху
            drawClusterLegend(g, mapRect);

            // 4. Отрисовка плотного полотна точек с 3 оттенками высоты тона
            if (database != nullptr && !database->empty())
            {
                const auto& db = *database;

                const int gridCellPx = (zoomLevel < 1.8f) ? 5 : ((zoomLevel < 3.5f) ? 3 : 2);
                const int gridW = std::max(1, (int)mapRect.getWidth() / gridCellPx + 2);
                const int gridH = std::max(1, (int)mapRect.getHeight() / gridCellPx + 2);
                std::vector<uint8_t> occupied((size_t)(gridW * gridH), 0);

                const bool hasActiveFilter = (clusterFilterIdx >= 0 || subFilterBitMask != 0);

                // Приглушённые точки, не попавшие в текущий фильтр
                if (hasActiveFilter)
                {
                    for (size_t i = 0; i < db.size(); ++i)
                    {
                        const auto& rec = db[i];
                        if (rec.isLoop != loopsTabActive || checkFilterMatch(rec))
                            continue;

                        auto pt = mapToScreen(rec.mapX, rec.mapY, mapRect);
                        if (!mapRect.contains(pt)) continue;

                        int gx = juce::jlimit(0, gridW - 1, (int)(pt.x - mapRect.getX()) / gridCellPx);
                        int gy = juce::jlimit(0, gridH - 1, (int)(pt.y - mapRect.getY()) / gridCellPx);
                        size_t cellIdx = (size_t)(gy * gridW + gx);
                        if (occupied[cellIdx]) continue;
                        occupied[cellIdx] = 1;

                        juce::Colour baseCol = MusicMath::getClusterColour(rec.cluster, rec.toneShadeBucket);
                        g.setColour(baseCol.withAlpha(0.12f));
                        g.fillEllipse(pt.x - 2.0f, pt.y - 2.0f, 4.0f, 4.0f);
                    }
                    std::fill(occupied.begin(), occupied.end(), 0);
                }

                // Активные точки
                const float dotRadius = 2.8f;
                for (int i = 0; i < (int)db.size(); ++i)
                {
                    const auto& rec = db[(size_t)i];
                    if (rec.isLoop != loopsTabActive || !checkFilterMatch(rec))
                        continue;

                    auto pt = mapToScreen(rec.mapX, rec.mapY, mapRect);
                    if (!mapRect.contains(pt))
                        continue;

                    bool isHoveredOrActive = (i == activeSampleIndex || i == highlightedHistorySampleIndex);
                    if (!isHoveredOrActive)
                    {
                        int gx = juce::jlimit(0, gridW - 1, (int)(pt.x - mapRect.getX()) / gridCellPx);
                        int gy = juce::jlimit(0, gridH - 1, (int)(pt.y - mapRect.getY()) / gridCellPx);
                        size_t cellIdx = (size_t)(gy * gridW + gx);
                        if (occupied[cellIdx]) continue;
                        occupied[cellIdx] = 1;
                    }

                    juce::Colour col = MusicMath::getClusterColour(rec.cluster, rec.toneShadeBucket);

                    if (isHoveredOrActive)
                    {
                        float pulseRadius = isHoldingPoint ? (12.0f + 3.0f * std::sin(pulsePhase * 2.2f))
                                                           : (9.5f + 1.5f * std::sin(pulsePhase));
                        g.setColour(col.withAlpha(isHoldingPoint ? 0.36f : 0.22f));
                        g.fillEllipse(pt.x - pulseRadius, pt.y - pulseRadius, pulseRadius * 2.0f, pulseRadius * 2.0f);

                        g.setColour(isHoldingPoint ? juce::Colours::white : col);
                        g.drawEllipse(pt.x - 7.5f, pt.y - 7.5f, 15.0f, 15.0f, isHoldingPoint ? 2.0f : 1.4f);

                        g.setColour(juce::Colours::white);
                        g.fillEllipse(pt.x - 3.6f, pt.y - 3.6f, 7.2f, 7.2f);
                    }
                    else
                    {
                        g.setColour(juce::Colour(0xcc07090c));
                        g.fillEllipse(pt.x - (dotRadius + 0.8f), pt.y - (dotRadius + 0.8f),
                                      (dotRadius + 0.8f) * 2.0f, (dotRadius + 0.8f) * 2.0f);

                        g.setColour(col.withAlpha(0.96f));
                        g.fillEllipse(pt.x - dotRadius, pt.y - dotRadius, dotRadius * 2.0f, dotRadius * 2.0f);
                    }
                }

                // 5. Карточка волны с белой полосой проигрывания (Playhead)
                if (activeSampleIndex >= 0 && activeSampleIndex < (int)db.size())
                {
                    const auto& rec = db[(size_t)activeSampleIndex];
                    if (rec.isLoop == loopsTabActive)
                    {
                        auto pt = mapToScreen(rec.mapX, rec.mapY, mapRect);
                        drawWaveformCallout(g, pt, rec, mapRect);
                    }
                }

                // 6. Визуальный «призрак» перетаскиваемой карточки волны
                if (isDraggingCalloutCard && activeSampleIndex >= 0 && activeSampleIndex < (int)db.size())
                {
                    drawDraggedGhostCard(g, db[(size_t)activeSampleIndex]);
                }
            }
            else
            {
                g.setColour(juce::Colour(0xff687380));
                g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain));
                g.drawText("[ DROP SAMPLE FOLDER HERE OR CLICK 'SCAN FOLDER' ]",
                           mapRect, juce::Justification::centred, false);
            }

            // 7. Лента истории внизу карты
            drawHistoryTrack(g, historyRect);
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            grabKeyboardFocus();
            const float historyHeight = 26.0f;

            if (e.position.y >= (float)getHeight() - historyHeight)
            {
                handleHistoryClick(e.position);
                return;
            }

            // Кнопки быстрого зума в правом верхнем углу карты
            if (btnZoomIn.contains(e.position))
            {
                zoomByFactor(1.25f, getLocalBounds().toFloat().getCentre());
                return;
            }
            if (btnZoomOut.contains(e.position))
            {
                zoomByFactor(0.80f, getLocalBounds().toFloat().getCentre());
                return;
            }
            if (btnZoomReset.contains(e.position))
            {
                zoomLevel = 1.0f;
                panOffset = { 0.0f, 0.0f };
                repaint();
                return;
            }

            // Правый клик по точке — мгновенное добавление звука в выделенный пэд без меню
            if (e.mods.isRightButtonDown())
            {
                int hitIdx = findClosestSample(e.position);
                if (hitIdx >= 0)
                {
                    activeSampleIndex = hitIdx;
                    if (onSampleRightClicked)
                        onSampleRightClicked(hitIdx);
                    repaint();
                }
                else
                {
                    isPanningCanvas = true;
                    lastPanPos = e.position;
                }
                return;
            }

            if (e.mods.isMiddleButtonDown())
            {
                isPanningCanvas = true;
                lastPanPos = e.position;
                return;
            }

            dragStartPos = e.position;
            currentDragLocalPos = e.position;
            isDraggingCalloutCard = false;
            isPanningCanvas = false;

            // Захват и перетаскивание работает ТОЛЬКО по карточке волны
            if (activeSampleIndex >= 0 && lastCalloutBox.contains(e.position))
            {
                startedOnCalloutCard = true;
                startSampleHold(activeSampleIndex);
                return;
            }

            startedOnCalloutCard = false;
            int hitIdx = findClosestSample(e.position);

            if (hitIdx >= 0)
            {
                startSampleHold(hitIdx);
            }
            else
            {
                // Клик ЛКМ в пустом пространстве позволяет «подтянуться» за холст к нужным звукам
                isHoldingPoint = false;
                sampler.stopAudition();
                isPanningCanvas = true;
                lastPanPos = e.position;
                repaint();
            }
        }

        void mouseDrag(const juce::MouseEvent& e) override
        {
            currentDragLocalPos = e.position;
            const float historyHeight = 26.0f;

            // 1. Панорамирование холста (ЛКМ в пустом пространстве или средняя кнопка мыши)
            if (isPanningCanvas || e.mods.isMiddleButtonDown())
            {
                auto delta = e.position - lastPanPos;
                lastPanPos = e.position;
                panOffset.x -= delta.x / (zoomLevel * std::max(200.0f, (float)getWidth()));
                panOffset.y += delta.y / (zoomLevel * std::max(160.0f, (float)getHeight()));
                panOffset.x = juce::jlimit(-0.48f, 0.48f, panOffset.x);
                panOffset.y = juce::jlimit(-0.48f, 0.48f, panOffset.y);
                repaint();
                return;
            }

            // 2. Перетаскивание сэмпла работает строго от карточки волны
            if (startedOnCalloutCard && activeSampleIndex >= 0)
            {
                if (e.position.getDistanceFrom(dragStartPos) > 5.0f)
                {
                    if (!isDraggingCalloutCard)
                    {
                        isDraggingCalloutCard = true;
                        isHoldingPoint = false;
                        sampler.stopAudition();
                    }
                    if (onSampleDragged)
                        onSampleDragged(activeSampleIndex, localPointToGlobal(e.position.toInt()), false);
                    repaint();
                    return;
                }
            }

            // 3. Свободный сёрфинг по точкам при зажатой ЛКМ (без случайного перетаскивания точек)
            if (e.position.y < (float)getHeight() - historyHeight)
            {
                int hitIdx = findClosestSample(e.position);
                if (hitIdx >= 0 && hitIdx != activeSampleIndex)
                {
                    startSampleHold(hitIdx);
                }
            }
        }

        void mouseUp(const juce::MouseEvent& e) override
        {
            if (isDraggingCalloutCard && activeSampleIndex >= 0)
            {
                isDraggingCalloutCard = false;
                startedOnCalloutCard = false;
                if (onSampleDragged)
                    onSampleDragged(activeSampleIndex, localPointToGlobal(e.position.toInt()), true);
                repaint();
                return;
            }

            startedOnCalloutCard = false;
            isPanningCanvas = false;

            // Как только клик отпущен — звук/луп мягко уходит в фэйд
            if (isHoldingPoint)
            {
                isHoldingPoint = false;
                if (database != nullptr && activeSampleIndex >= 0 && activeSampleIndex < (int)database->size())
                    sampler.auditionSample(activeSampleIndex, (*database)[(size_t)activeSampleIndex], false, playheadProgress);
                else
                    sampler.stopAudition();
                repaint();
            }
        }

        void mouseMove(const juce::MouseEvent& e) override
        {
            const float historyHeight = 26.0f;
            if (e.position.y >= (float)getHeight() - historyHeight)
            {
                updateHistoryHover(e.position);
            }
            else if (highlightedHistorySampleIndex != -1)
            {
                highlightedHistorySampleIndex = -1;
                repaint();
            }
        }

        // Жест трекпада (сведение/разведение пальцев на macOS) для плавного зума
        void mouseMagnify(const juce::MouseEvent& e, float scaleFactor) override
        {
            zoomByFactor(scaleFactor, e.position);
        }

        // Двумя пальцами по тачпаду — перемещение в пространстве; с модификатором (Cmd/Ctrl/Alt) — зум
        void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
        {
            bool zoomModifier = e.mods.isCommandDown() || e.mods.isCtrlDown() || e.mods.isAltDown();

            if (zoomModifier)
            {
                float factor = (wheel.deltaY > 0.0f) ? 1.16f : 0.86f;
                zoomByFactor(factor, e.position);
                return;
            }

            // Если есть горизонтальная составляющая (жест двумя пальцами на тачпаде) или карта увеличена — двигаем камеру
            if (std::abs(wheel.deltaX) > 0.0005f || zoomLevel > 1.02f)
            {
                panOffset.x -= wheel.deltaX * (0.35f / zoomLevel);
                panOffset.y += wheel.deltaY * (0.35f / zoomLevel);
                panOffset.x = juce::jlimit(-0.48f, 0.48f, panOffset.x);
                panOffset.y = juce::jlimit(-0.48f, 0.48f, panOffset.y);
                repaint();
            }
            else if (std::abs(wheel.deltaY) > 0.01f)
            {
                // На обычной мыши с одиночным колёсиком при полном отдалении позволяем плавно приблизить
                float factor = (wheel.deltaY > 0.0f) ? 1.15f : 0.87f;
                zoomByFactor(factor, e.position);
            }
        }

        bool keyPressed(const juce::KeyPress& key) override
        {
            if (key == juce::KeyPress::spaceKey)
            {
                isHoldingPoint = false;
                sampler.stopAudition();
                repaint();
                return true;
            }
            return false;
        }

    private:
        void timerCallback() override
        {
            pulsePhase += 0.18f;
            if (pulsePhase > juce::MathConstants<float>::twoPi)
                pulsePhase -= juce::MathConstants<float>::twoPi;

            if (isHoldingPoint && database != nullptr && activeSampleIndex >= 0 && activeSampleIndex < (int)database->size())
            {
                sampler.auditionSample(activeSampleIndex, (*database)[(size_t)activeSampleIndex], true, playheadProgress);
                repaint();
            }
            else if (activeSampleIndex >= 0)
            {
                repaint();
            }
        }

        void zoomByFactor(float factor, juce::Point<float> anchorPt)
        {
            const auto mapRect = getLocalBounds().toFloat().withTrimmedBottom(26.0f);
            float oldZoom = zoomLevel;
            zoomLevel = juce::jlimit(1.0f, 10.0f, zoomLevel * factor);

            if (zoomLevel <= 1.02f)
            {
                panOffset = { 0.0f, 0.0f };
            }
            else if (mapRect.contains(anchorPt))
            {
                float relX = (anchorPt.x - mapRect.getCentreX()) / mapRect.getWidth();
                float relY = (anchorPt.y - mapRect.getCentreY()) / mapRect.getHeight();
                panOffset.x += relX * (1.0f / oldZoom - 1.0f / zoomLevel);
                panOffset.y -= relY * (1.0f / oldZoom - 1.0f / zoomLevel);
                panOffset.x = juce::jlimit(-0.48f, 0.48f, panOffset.x);
                panOffset.y = juce::jlimit(-0.48f, 0.48f, panOffset.y);
            }
            repaint();
        }

        bool checkFilterMatch(const SampleRecord& rec) const
        {
            if (clusterFilterIdx >= 0 && (int)rec.cluster != clusterFilterIdx)
                return false;

            if (subFilterBitMask != 0 && (rec.subMask & subFilterBitMask) == 0)
                return false;

            return true;
        }

        juce::Point<float> mapToScreen(float normX, float normY, const juce::Rectangle<float>& rect) const
        {
            float zx = (normX - 0.5f - panOffset.x) * zoomLevel + 0.5f;
            float zy = (1.0f - normY - 0.5f - panOffset.y) * zoomLevel + 0.5f;

            float px = rect.getX() + 24.0f + zx * (rect.getWidth() - 48.0f);
            float py = rect.getY() + 26.0f + zy * (rect.getHeight() - 44.0f);
            return { px, py };
        }

        int findClosestSample(juce::Point<float> mousePos) const
        {
            if (database == nullptr || database->empty()) return -1;

            const auto mapRect = getLocalBounds().toFloat().withTrimmedBottom(26.0f);
            const auto& db = *database;

            int closestIdx = -1;
            float minDistSq = 16.0f * 16.0f;

            for (int i = 0; i < (int)db.size(); ++i)
            {
                const auto& rec = db[(size_t)i];
                if (rec.isLoop != loopsTabActive || !checkFilterMatch(rec))
                    continue;

                auto pt = mapToScreen(rec.mapX, rec.mapY, mapRect);
                if (!mapRect.contains(pt)) continue;

                float dSq = pt.getDistanceSquaredFrom(mousePos);
                if (dSq < minDistSq)
                {
                    minDistSq = dSq;
                    closestIdx = i;
                }
            }
            return closestIdx;
        }

        void startSampleHold(int idx)
        {
            if (database == nullptr || idx < 0 || idx >= (int)database->size())
                return;

            sampler.stopAudition();
            activeSampleIndex = idx;
            isHoldingPoint = true;
            playheadProgress = 0.0f;
            sampler.auditionSample(idx, (*database)[(size_t)idx], true, playheadProgress);

            if (historyIndices.empty() || historyIndices.back() != idx)
            {
                historyIndices.push_back(idx);
                if (historyIndices.size() > 15)
                    historyIndices.pop_front();
            }

            if (onSampleTriggered)
                onSampleTriggered(idx);

            repaint();
        }

        void drawSubtleGrid(juce::Graphics& g, const juce::Rectangle<float>& rect)
        {
            g.setColour(juce::Colour(0xff131820));
            const int vLines = 16;
            const int hLines = 8;

            for (int i = 1; i < vLines; ++i)
            {
                float x = rect.getX() + rect.getWidth() * ((float)i / (float)vLines);
                g.drawVerticalLine((int)x, rect.getY(), rect.getBottom());
            }
            for (int j = 1; j < hLines; ++j)
            {
                float y = rect.getY() + rect.getHeight() * ((float)j / (float)hLines);
                g.drawHorizontalLine((int)y, rect.getX(), rect.getRight());
            }

            g.setColour(juce::Colour(0xff232a34));
            g.drawRect(rect, 1.0f);
        }

        void drawClusterLegend(juce::Graphics& g, const juce::Rectangle<float>& mapRect)
        {
            struct LegendItem { ClusterGroup group; const char* title; };
            static const LegendItem items[8] = {
                { ClusterGroup::Keys,    "KEYS" },
                { ClusterGroup::Synth,   "SYNTH" },
                { ClusterGroup::Bass,    "BASS" },
                { ClusterGroup::Vocal,   "VOX" },
                { ClusterGroup::Strings, "STRINGS" },
                { ClusterGroup::Guitar,  "GUITAR" },
                { ClusterGroup::FX,      "FX" },
                { ClusterGroup::Other,   "OTHER" }
            };

            g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::plain));
            float x = mapRect.getX() + 10.0f;
            float y = mapRect.getY() + 6.0f;

            for (int i = 0; i < 8; ++i)
            {
                // Рисуем 3 точки-оттенка (низкий, средний, высокий тон) для наглядности
                for (int shade = 0; shade < 3; ++shade)
                {
                    g.setColour(MusicMath::getClusterColour(items[i].group, shade));
                    g.fillEllipse(x + (float)shade * 4.5f, y + 3.5f, 5.0f, 5.0f);
                }

                g.setColour(juce::Colour(0xff8a97a6));
                juce::String lbl(items[i].title);
                float tw = (float)lbl.length() * 6.5f + 10.0f;
                g.drawText(lbl, (int)(x + 16.0f), (int)y - 1, (int)tw, 14, juce::Justification::centredLeft, false);
                x += tw + 22.0f;
            }

            // Кнопки управления зумом в правом верхнем углу карты
            float rx = mapRect.getRight() - 108.0f;
            btnZoomOut   = { rx,        y, 24.0f, 16.0f };
            btnZoomIn    = { rx + 28.0f, y, 24.0f, 16.0f };
            btnZoomReset = { rx + 56.0f, y, 44.0f, 16.0f };

            auto drawMiniBtn = [&](juce::Rectangle<float> r, const juce::String& t)
            {
                g.setColour(juce::Colour(0xff19202a));
                g.fillRoundedRectangle(r, 2.5f);
                g.setColour(juce::Colour(0xff323c4a));
                g.drawRoundedRectangle(r, 2.5f, 1.0f);
                g.setColour(juce::Colour(0xffa9b6c5));
                g.drawText(t, r, juce::Justification::centred, false);
            };

            drawMiniBtn(btnZoomOut, "-");
            drawMiniBtn(btnZoomIn, "+");
            drawMiniBtn(btnZoomReset, juce::String(zoomLevel, 1) + "x");
        }

        void drawWaveformCallout(juce::Graphics& g, juce::Point<float> pt,
                                 const SampleRecord& rec, const juce::Rectangle<float>& mapRect)
        {
            const float boxW = 206.0f;
            const float boxH = 58.0f;

            float bx = pt.x + 14.0f;
            float by = pt.y - boxH - 10.0f;
            if (bx + boxW > mapRect.getRight() - 8.0f) bx = pt.x - boxW - 14.0f;
            if (by < mapRect.getY() + 22.0f) by = pt.y + 14.0f;

            lastCalloutBox = { bx, by, boxW, boxH };

            g.setColour(juce::Colour(0xaa8b98a9));
            g.drawLine(pt.x, pt.y, lastCalloutBox.getCentreX(), lastCalloutBox.getBottom(), 1.0f);

            g.setColour(juce::Colour(0xf2131820));
            g.fillRoundedRectangle(lastCalloutBox, 3.5f);
            juce::Colour col = MusicMath::getClusterColour(rec.cluster, rec.toneShadeBucket);
            g.setColour(col.withAlpha(0.90f));
            g.drawRoundedRectangle(lastCalloutBox, 3.5f, 1.3f);

            auto waveArea = lastCalloutBox.reduced(8.0f, 6.0f).withTrimmedBottom(17.0f);
            auto audio = sampler.getAuditionAudio();
            if (audio != nullptr && !audio->waveformPeaks.empty())
            {
                const auto& peaks = audio->waveformPeaks;
                const float barW = waveArea.getWidth() / (float)peaks.size();
                const float midY = waveArea.getCentreY();

                for (size_t k = 0; k < peaks.size(); ++k)
                {
                    float normPos = (float)k / (float)peaks.size();
                    bool passedPlayhead = isHoldingPoint && (normPos <= playheadProgress);
                    g.setColour(passedPlayhead ? col.brighter(0.35f) : col.withAlpha(0.82f));

                    float h = std::max(1.5f, peaks[k] * waveArea.getHeight() * 0.92f);
                    g.fillRect(waveArea.getX() + (float)k * barW, midY - h * 0.5f, std::max(1.0f, barW - 0.4f), h);
                }
            }

            // Белая вертикальная полоса проигрывания (Playhead) во время удержания точки
            if (isHoldingPoint)
            {
                float playX = waveArea.getX() + juce::jlimit(0.0f, 1.0f, playheadProgress) * waveArea.getWidth();
                g.setColour(juce::Colours::white);
                g.drawLine(playX, waveArea.getY() - 1.0f, playX, waveArea.getBottom() + 1.0f, 1.8f);
            }

            g.setColour(juce::Colour(0xffdce5f0));
            g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::plain));
            juce::String label = rec.name.substring(0, 15) + " [" + MusicMath::getNoteName(rec.pitchClass) + "] DRAG CARD->";
            g.drawText(label, lastCalloutBox.reduced(7.0f, 3.0f).removeFromBottom(16.0f),
                       juce::Justification::centredLeft, true);
        }

        void drawDraggedGhostCard(juce::Graphics& g, const SampleRecord& rec)
        {
            juce::Rectangle<float> ghostRect(currentDragLocalPos.x - 75.0f, currentDragLocalPos.y - 22.0f, 150.0f, 38.0f);
            juce::Colour col = MusicMath::getClusterColour(rec.cluster, rec.toneShadeBucket);

            g.setColour(juce::Colour(0xd8161c26));
            g.fillRoundedRectangle(ghostRect, 4.0f);
            g.setColour(col);
            g.drawRoundedRectangle(ghostRect, 4.0f, 1.6f);

            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::bold));
            g.drawText("DRAGGING: " + rec.name.substring(0, 12), ghostRect.reduced(6.0f, 4.0f),
                       juce::Justification::centred, true);
        }

        void drawHistoryTrack(juce::Graphics& g, const juce::Rectangle<float>& rect)
        {
            g.setColour(juce::Colour(0xff11151b));
            g.fillRect(rect);
            g.setColour(juce::Colour(0xff232a34));
            g.drawHorizontalLine((int)rect.getY(), rect.getX(), rect.getRight());

            g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
            g.setColour(juce::Colour(0xff7f8b99));
            g.drawText("HISTORY:", rect.withX(10.0f).withWidth(70.0f), juce::Justification::centredLeft, false);

            const float startX = 82.0f;
            const float stepX = 22.0f;
            const float cy = rect.getCentreY();

            for (int i = 0; i < 15; ++i)
            {
                float cx = startX + (float)i * stepX;
                if (i < 14)
                {
                    g.setColour(juce::Colour(0xff2a323d));
                    g.drawLine(cx + 5.0f, cy, cx + stepX - 5.0f, cy, 1.2f);
                }

                if (i < (int)historyIndices.size() && database != nullptr)
                {
                    int sIdx = historyIndices[(size_t)i];
                    const auto& rec = (*database)[(size_t)sIdx];
                    bool isHovered = (sIdx == highlightedHistorySampleIndex);
                    bool isLatest  = (i == (int)historyIndices.size() - 1);

                    if (isHovered)
                    {
                        g.setColour(MusicMath::getClusterColour(rec.cluster, rec.toneShadeBucket));
                        g.fillEllipse(cx - 5.0f, cy - 5.0f, 10.0f, 10.0f);
                    }
                    else if (isLatest)
                    {
                        g.setColour(juce::Colour(0xffc9d4e0));
                        g.fillEllipse(cx - 4.0f, cy - 4.0f, 8.0f, 8.0f);
                    }
                    else
                    {
                        g.setColour(juce::Colour(0xff525d6b));
                        g.drawEllipse(cx - 3.5f, cy - 3.5f, 7.0f, 7.0f, 1.4f);
                    }
                }
                else
                {
                    g.setColour(juce::Colour(0xff252c36));
                    g.fillEllipse(cx - 2.0f, cy - 2.0f, 4.0f, 4.0f);
                }
            }

            if (database != nullptr && activeSampleIndex >= 0 && activeSampleIndex < (int)database->size())
            {
                const auto& rec = (*database)[(size_t)activeSampleIndex];
                g.setColour(juce::Colour(0xff8c99a8));
                juce::String typeStr = rec.isLoop ? "LOOP" : "1-SHOT";
                juce::String info = rec.name + " [" + MusicMath::getNoteName(rec.pitchClass) + " | " +
                                    MusicMath::getClusterName(rec.cluster) + " | " + typeStr + "] (R-Click -> Pad)";
                g.drawText(info, rect.withTrimmedRight(10.0f).withWidth(460.0f).withX(rect.getRight() - 470.0f),
                           juce::Justification::centredRight, true);
            }
        }

        void updateHistoryHover(juce::Point<float> pos)
        {
            const float startX = 82.0f;
            const float stepX = 22.0f;
            int foundSampleIdx = -1;

            for (int i = 0; i < (int)historyIndices.size(); ++i)
            {
                float cx = startX + (float)i * stepX;
                if (std::abs(pos.x - cx) < 10.0f)
                {
                    foundSampleIdx = historyIndices[(size_t)i];
                    break;
                }
            }

            if (foundSampleIdx != highlightedHistorySampleIndex)
            {
                highlightedHistorySampleIndex = foundSampleIdx;
                repaint();
            }
        }

        void handleHistoryClick(juce::Point<float> pos)
        {
            updateHistoryHover(pos);
            if (highlightedHistorySampleIndex >= 0)
            {
                startSampleHold(highlightedHistorySampleIndex);
                isHoldingPoint = false;
            }
        }

        SamplerEngine& sampler;
        const std::vector<SampleRecord>* database = nullptr;

        bool loopsTabActive = false;
        int clusterFilterIdx = -1;
        uint32_t subFilterBitMask = 0;

        int activeSampleIndex = -1;
        int highlightedHistorySampleIndex = -1;
        std::deque<int> historyIndices;
        juce::Rectangle<float> lastCalloutBox;
        juce::Rectangle<float> btnZoomIn, btnZoomOut, btnZoomReset;

        float zoomLevel = 1.0f;
        juce::Point<float> panOffset { 0.0f, 0.0f };
        juce::Point<float> lastPanPos;
        juce::Point<float> dragStartPos;
        juce::Point<float> currentDragLocalPos;

        bool isHoldingPoint = false;
        bool isPanningCanvas = false;
        bool startedOnCalloutCard = false;
        bool isDraggingCalloutCard = false;
        float playheadProgress = 0.0f;
        float pulsePhase = 0.0f;
    };
}
