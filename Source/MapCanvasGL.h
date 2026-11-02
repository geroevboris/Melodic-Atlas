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
            historyIndices.clear();
            repaint();
        }

        void setViewFilter(bool showLoopsTab, FilterCategory cat, uint32_t activeMask)
        {
            if (loopsTabActive != showLoopsTab)
                sampler.stopAudition();

            loopsTabActive = showLoopsTab;
            filterCategory = cat;
            filterBitMask  = activeMask;
            repaint();
        }

        int getActiveSampleIndex() const { return activeSampleIndex; }

        void paint(juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat();
            const float historyHeight = 28.0f;
            const auto historyRect = bounds.removeFromBottom(historyHeight);
            const auto mapRect = bounds;

            // 1. Фон карты
            g.setColour(juce::Colour(0xff0b0e13));
            g.fillRect(mapRect);

            // 2. Координатная сетка
            drawSubtleGrid(g, mapRect);

            // 3. Цветовая легенда кластеров сверху карты
            drawClusterLegend(g, mapRect);

            // 4. Чёткая отрисовка точек с XO-подобным Spatial LOD (прореживание в отдалении, детализация при зуме)
            if (database != nullptr && !database->empty())
            {
                const auto& db = *database;

                // Сетка занятости экрана, чтобы точки не слипались в мутную кашу на малом зуме
                const int gridCellPx = (zoomLevel < 1.8f) ? 6 : ((zoomLevel < 3.5f) ? 4 : 2);
                const int gridW = std::max(1, (int)mapRect.getWidth() / gridCellPx + 2);
                const int gridH = std::max(1, (int)mapRect.getHeight() / gridCellPx + 2);
                std::vector<uint8_t> occupied((size_t)(gridW * gridH), 0);

                // Сначала рисуем фоновые (отфильтрованные) точки
                if (filterBitMask != 0)
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
                        if (occupied[(size_t)(gy * gridW + gx)]) continue;
                        occupied[(size_t)(gy * gridW + gx)] = 1;

                        juce::Colour baseCol = MusicMath::getClusterColour(rec.cluster, rec.spectralCentroidNorm);
                        g.setColour(baseCol.withAlpha(0.14f));
                        g.fillEllipse(pt.x - 2.0f, pt.y - 2.0f, 4.0f, 4.0f);
                    }
                    std::fill(occupied.begin(), occupied.end(), 0);
                }

                // Рисуем активные точки с идеальной резкостью
                const float dotRadius = 2.6f;
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

                    juce::Colour col = MusicMath::getClusterColour(rec.cluster, rec.spectralCentroidNorm);

                    if (isHoveredOrActive)
                    {
                        float pulseRadius = 11.0f + 2.5f * std::sin(pulsePhase);
                        g.setColour(col.withAlpha(0.28f));
                        g.fillEllipse(pt.x - pulseRadius, pt.y - pulseRadius, pulseRadius * 2.0f, pulseRadius * 2.0f);

                        g.setColour(col);
                        g.drawEllipse(pt.x - 7.5f, pt.y - 7.5f, 15.0f, 15.0f, 1.5f);

                        g.setColour(juce::Colours::white);
                        g.fillEllipse(pt.x - 3.5f, pt.y - 3.5f, 7.0f, 7.0f);
                    }
                    else
                    {
                        // Чёткая векторная точка с тонким тёмным контуром (как в XO)
                        g.setColour(juce::Colour(0xcc07090c));
                        g.fillEllipse(pt.x - (dotRadius + 0.8f), pt.y - (dotRadius + 0.8f),
                                      (dotRadius + 0.8f) * 2.0f, (dotRadius + 0.8f) * 2.0f);

                        g.setColour(col.withAlpha(0.96f));
                        g.fillEllipse(pt.x - dotRadius, pt.y - dotRadius, dotRadius * 2.0f, dotRadius * 2.0f);
                    }
                }

                // 5. Карточка волны над выбранной точкой
                if (activeSampleIndex >= 0 && activeSampleIndex < (int)db.size())
                {
                    const auto& rec = db[(size_t)activeSampleIndex];
                    if (rec.isLoop == loopsTabActive)
                    {
                        auto pt = mapToScreen(rec.mapX, rec.mapY, mapRect);
                        drawWaveformCallout(g, pt, rec, mapRect);
                    }
                }
            }
            else
            {
                g.setColour(juce::Colour(0xff687380));
                g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain));
                g.drawText("[ DROP SAMPLE FOLDER HERE OR CLICK 'SCAN SAMPLE FOLDER' ]",
                           mapRect, juce::Justification::centred, false);
            }

            // 6. Лента истории
            drawHistoryTrack(g, historyRect);
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            grabKeyboardFocus();
            const float historyHeight = 28.0f;

            if (e.position.y >= getHeight() - historyHeight)
            {
                handleHistoryClick(e.position);
                return;
            }

            if (e.mods.isRightButtonDown() || e.mods.isMiddleButtonDown())
            {
                lastPanPos = e.position;
                return;
            }

            dragStartPos = e.position;
            isDraggingSample = false;

            // Если кликнули по карточке волны — можно сразу перетаскивать сэмпл или остановить луп
            if (activeSampleIndex >= 0 && lastCalloutBox.contains(e.position))
            {
                startedOnActivePoint = true;
                sampler.triggerPadFromUI(-1); // no-op
                triggerSamplePoint(activeSampleIndex); // Повторный клик перезапускает звук
                return;
            }

            // Проверяем, кликнули ли прямо в уже выбранную точку или новую точку (forceRetrigger = true!)
            int prevActive = activeSampleIndex;
            int hitIdx = findClosestSample(e.position);

            if (hitIdx >= 0)
            {
                startedOnActivePoint = (hitIdx == prevActive);
                triggerSamplePoint(hitIdx); // Всегда проигрываем повторно при клике!
            }
            else
            {
                startedOnActivePoint = false;
                sampler.stopAudition();
                repaint();
            }
        }

        void mouseDrag(const juce::MouseEvent& e) override
        {
            const float historyHeight = 28.0f;

            if (e.mods.isRightButtonDown() || e.mods.isMiddleButtonDown())
            {
                auto delta = e.position - lastPanPos;
                lastPanPos = e.position;
                panOffset += delta / (zoomLevel * 400.0f);
                panOffset.x = juce::jlimit(-0.45f, 0.45f, panOffset.x);
                panOffset.y = juce::jlimit(-0.45f, 0.45f, panOffset.y);
                repaint();
                return;
            }

            // Перетаскивание звука в банк пэдов или в Ableton (срабатывает при вытягивании вниз к пэдам,
            // за пределы карты, при перетаскивании выбранной точки/плашки или с зажатым Cmd/Shift/Alt)
            bool pulledTowardsBankOrDaw = (e.position.y > (float)getHeight() - 24.0f) ||
                                          (e.position.x < 0.0f || e.position.x > (float)getWidth() || e.position.y < 0.0f);
            bool modifierDrag = (e.mods.isCommandDown() || e.mods.isAltDown() || e.mods.isShiftDown());

            if (activeSampleIndex >= 0 && (isDraggingSample || startedOnActivePoint || pulledTowardsBankOrDaw || modifierDrag))
            {
                if (e.position.getDistanceFrom(dragStartPos) > 7.0f)
                {
                    isDraggingSample = true;
                    if (onSampleDragged)
                        onSampleDragged(activeSampleIndex, localPointToGlobal(e.position.toInt()), false);
                    return;
                }
            }

            if (e.position.y < (float)getHeight() - historyHeight)
            {
                // При серфинге с зажатой мышью переключаем только при переходе на другую точку
                int hitIdx = findClosestSample(e.position);
                if (hitIdx >= 0 && hitIdx != activeSampleIndex)
                    triggerSamplePoint(hitIdx);
            }
        }

        void mouseUp(const juce::MouseEvent& e) override
        {
            if (isDraggingSample && activeSampleIndex >= 0)
            {
                isDraggingSample = false;
                if (onSampleDragged)
                    onSampleDragged(activeSampleIndex, localPointToGlobal(e.position.toInt()), true);
            }
            startedOnActivePoint = false;
        }

        void mouseMove(const juce::MouseEvent& e) override
        {
            const float historyHeight = 28.0f;
            if (e.position.y >= getHeight() - historyHeight)
            {
                updateHistoryHover(e.position);
            }
            else if (highlightedHistorySampleIndex != -1)
            {
                highlightedHistorySampleIndex = -1;
                repaint();
            }
        }

        void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
        {
            const auto mapRect = getLocalBounds().toFloat().withTrimmedBottom(28.0f);
            float oldZoom = zoomLevel;
            float factor = (wheel.deltaY > 0.0f) ? 1.18f : 0.85f;
            zoomLevel = juce::jlimit(1.0f, 10.0f, zoomLevel * factor);

            if (zoomLevel <= 1.02f)
            {
                panOffset = { 0.0f, 0.0f };
            }
            else if (mapRect.contains(e.position))
            {
                // Зум относительно позиции курсора мыши
                float relX = (e.position.x - mapRect.getCentreX()) / mapRect.getWidth();
                float relY = (e.position.y - mapRect.getCentreY()) / mapRect.getHeight();
                panOffset.x += relX * (1.0f / oldZoom - 1.0f / zoomLevel);
                panOffset.y += relY * (1.0f / oldZoom - 1.0f / zoomLevel);
                panOffset.x = juce::jlimit(-0.48f, 0.48f, panOffset.x);
                panOffset.y = juce::jlimit(-0.48f, 0.48f, panOffset.y);
            }
            repaint();
        }

        bool keyPressed(const juce::KeyPress& key) override
        {
            if (key == juce::KeyPress::spaceKey)
            {
                sampler.stopAudition();
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

            if (activeSampleIndex >= 0)
                repaint();
        }

        bool checkFilterMatch(const SampleRecord& rec) const
        {
            if (filterBitMask == 0)
                return true;

            if (filterCategory == FilterCategory::Timbre)
                return (rec.timbreMask & filterBitMask) != 0;
            else
                return (rec.characterMask & filterBitMask) != 0;
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

            const auto mapRect = getLocalBounds().toFloat().withTrimmedBottom(28.0f);
            const auto& db = *database;

            int closestIdx = -1;
            float minDistSq = 18.0f * 18.0f;

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

        void triggerSamplePoint(int idx)
        {
            if (database == nullptr || idx < 0 || idx >= (int)database->size())
                return;

            activeSampleIndex = idx;
            sampler.auditionSample(idx, (*database)[(size_t)idx]);

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
            g.setColour(juce::Colour(0xff141920));
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
            static const LegendItem items[6] = {
                { ClusterGroup::DeepBass,    "DEEP/BASS" },
                { ClusterGroup::Keys,        "KEYS" },
                { ClusterGroup::GuitarPluck, "GUITAR/PLUCK" },
                { ClusterGroup::SynthLead,   "SYNTH" },
                { ClusterGroup::Vocal,       "VOX" },
                { ClusterGroup::StringsPad,  "STRINGS/PAD" }
            };

            g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::plain));
            float x = mapRect.getX() + 10.0f;
            float y = mapRect.getY() + 6.0f;

            for (int i = 0; i < 6; ++i)
            {
                juce::Colour c = MusicMath::getClusterColour(items[i].group, 0.6f);
                g.setColour(c);
                g.fillEllipse(x, y + 3.0f, 6.5f, 6.5f);

                g.setColour(juce::Colour(0xff7f8c9b));
                juce::String lbl(items[i].title);
                float tw = (float)lbl.length() * 6.5f + 16.0f;
                g.drawText(lbl, (int)(x + 10.0f), (int)y - 1, (int)tw, 14, juce::Justification::centredLeft, false);
                x += tw + 8.0f;
            }

            if (zoomLevel > 1.05f)
            {
                g.setColour(juce::Colour(0xff7ec8a3));
                g.drawText("ZOOM: " + juce::String(zoomLevel, 1) + "x (R-Click Pan)",
                           (int)mapRect.getRight() - 165, (int)y - 1, 155, 14, juce::Justification::centredRight, false);
            }
        }

        void drawWaveformCallout(juce::Graphics& g, juce::Point<float> pt,
                                 const SampleRecord& rec, const juce::Rectangle<float>& mapRect)
        {
            const float boxW = 192.0f;
            const float boxH = 56.0f;

            float bx = pt.x + 14.0f;
            float by = pt.y - boxH - 10.0f;
            if (bx + boxW > mapRect.getRight() - 8.0f) bx = pt.x - boxW - 14.0f;
            if (by < mapRect.getY() + 20.0f) by = pt.y + 14.0f;

            lastCalloutBox = { bx, by, boxW, boxH };

            g.setColour(juce::Colour(0xaa8b98a9));
            g.drawLine(pt.x, pt.y, lastCalloutBox.getCentreX(), lastCalloutBox.getBottom(), 1.0f);

            g.setColour(juce::Colour(0xf0131820));
            g.fillRoundedRectangle(lastCalloutBox, 3.5f);
            juce::Colour col = MusicMath::getClusterColour(rec.cluster, 0.75f);
            g.setColour(col.withAlpha(0.85f));
            g.drawRoundedRectangle(lastCalloutBox, 3.5f, 1.2f);

            auto audio = sampler.getAuditionAudio();
            if (audio != nullptr)
            {
                auto waveArea = lastCalloutBox.reduced(8.0f, 6.0f).withTrimmedBottom(17.0f);
                g.setColour(col.withAlpha(0.92f));

                const auto& peaks = audio->waveformPeaks;
                const float barW = waveArea.getWidth() / (float)peaks.size();
                const float midY = waveArea.getCentreY();

                for (size_t k = 0; k < peaks.size(); ++k)
                {
                    float h = std::max(1.5f, peaks[k] * waveArea.getHeight() * 0.92f);
                    g.fillRect(waveArea.getX() + (float)k * barW, midY - h * 0.5f, std::max(1.0f, barW - 0.4f), h);
                }
            }

            g.setColour(juce::Colour(0xffdce5f0));
            g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
            juce::String label = rec.name.substring(0, 16) + " [" + MusicMath::getNoteName(rec.pitchClass) + "] DRAG->";
            g.drawText(label, lastCalloutBox.reduced(7.0f, 3.0f).removeFromBottom(16.0f),
                       juce::Justification::centredLeft, true);
        }

        void drawHistoryTrack(juce::Graphics& g, const juce::Rectangle<float>& rect)
        {
            g.setColour(juce::Colour(0xff11151b));
            g.fillRect(rect);
            g.setColour(juce::Colour(0xff232a34));
            g.drawHorizontalLine((int)rect.getY(), rect.getX(), rect.getRight());

            g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::plain));
            g.setColour(juce::Colour(0xff7f8b99));
            g.drawText("HISTORY TRACK:", rect.withX(10.0f).withWidth(110.0f), juce::Justification::centredLeft, false);

            const float startX = 122.0f;
            const float stepX = 24.0f;
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
                        juce::Colour keyCol = MusicMath::getClusterColour(rec.cluster, 0.8f);
                        g.setColour(keyCol);
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
                juce::String info = rec.name + " [" + MusicMath::getNoteName(rec.pitchClass) + " | " + typeStr + "]";
                g.drawText(info, rect.withTrimmedRight(12.0f).withWidth(340.0f).withX(rect.getRight() - 352.0f),
                           juce::Justification::centredRight, true);
            }
        }

        void updateHistoryHover(juce::Point<float> pos)
        {
            const float startX = 122.0f;
            const float stepX = 24.0f;
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
                triggerSamplePoint(highlightedHistorySampleIndex);
        }

        SamplerEngine& sampler;
        const std::vector<SampleRecord>* database = nullptr;

        bool loopsTabActive = false;
        FilterCategory filterCategory = FilterCategory::Timbre;
        uint32_t filterBitMask = 0;

        int activeSampleIndex = -1;
        int highlightedHistorySampleIndex = -1;
        std::deque<int> historyIndices;
        juce::Rectangle<float> lastCalloutBox;

        float zoomLevel = 1.0f;
        juce::Point<float> panOffset { 0.0f, 0.0f };
        juce::Point<float> lastPanPos;
        juce::Point<float> dragStartPos;
        bool isDraggingSample = false;
        bool startedOnActivePoint = false;
        float pulsePhase = 0.0f;
    };
}
