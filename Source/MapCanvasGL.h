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
        std::function<void(int sampleIdx, juce::Point<int> screenPos)> onSampleDragged;

        MapCanvasComponent(SamplerEngine& engineRef)
            : sampler(engineRef)
        {
            setOpaque(true);
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

            // 1. Глубокий графитовый фон карты
            g.setColour(juce::Colour(0xff0b0e12));
            g.fillRect(mapRect);

            // 2. Тонкая перспективная координатная сетка
            drawSubtleGrid(g, mapRect);

            // 3. Отрисовка созвездий сэмплов
            if (database != nullptr && !database->empty())
            {
                const auto& db = *database;
                for (int i = 0; i < (int)db.size(); ++i)
                {
                    const auto& rec = db[i];
                    if (rec.isLoop != loopsTabActive)
                        continue;

                    auto pt = mapToScreen(rec.mapX, rec.mapY, mapRect);
                    if (!mapRect.contains(pt))
                        continue;

                    bool passesFilter = checkFilterMatch(rec);
                    juce::Colour baseCol = MusicMath::getKeyColour(rec.pitchClass, rec.spectralCentroidNorm);

                    if (!passesFilter)
                    {
                        g.setColour(baseCol.withAlpha(0.11f));
                        g.fillEllipse(pt.x - 2.0f, pt.y - 2.0f, 4.0f, 4.0f);
                        continue;
                    }

                    bool isHoveredOrActive = (i == activeSampleIndex || i == highlightedHistorySampleIndex);
                    if (isHoveredOrActive)
                    {
                        // Внешний пульсирующий ореол вокруг выбранной точки
                        float pulseRadius = 14.0f + 3.0f * std::sin(pulsePhase);
                        g.setColour(baseCol.withAlpha(0.22f));
                        g.fillEllipse(pt.x - pulseRadius, pt.y - pulseRadius, pulseRadius * 2.0f, pulseRadius * 2.0f);

                        g.setColour(baseCol.withAlpha(0.75f));
                        g.drawEllipse(pt.x - 10.0f, pt.y - 10.0f, 20.0f, 20.0f, 1.2f);

                        g.setColour(juce::Colours::white);
                        g.fillEllipse(pt.x - 4.0f, pt.y - 4.0f, 8.0f, 8.0f);
                    }
                    else
                    {
                        // Мягкое свечение точки
                        g.setColour(baseCol.withAlpha(0.25f));
                        g.fillEllipse(pt.x - 5.0f, pt.y - 5.0f, 10.0f, 10.0f);

                        g.setColour(baseCol.withAlpha(0.92f));
                        g.fillEllipse(pt.x - 2.6f, pt.y - 2.6f, 5.2f, 5.2f);
                    }
                }

                // 4. Отрисовка всплывающего бокса с волной над активной точкой
                if (activeSampleIndex >= 0 && activeSampleIndex < (int)db.size())
                {
                    const auto& rec = db[activeSampleIndex];
                    if (rec.isLoop == loopsTabActive)
                    {
                        auto pt = mapToScreen(rec.mapX, rec.mapY, mapRect);
                        drawWaveformCallout(g, pt, rec, mapRect);
                    }
                }
            }
            else
            {
                // Заглушка до загрузки папки
                g.setColour(juce::Colour(0xff687380));
                g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain));
                g.drawText("[ DROP SAMPLE FOLDER HERE OR CLICK 'SCAN FOLDER' IN TOP RIGHT ]",
                           mapRect, juce::Justification::centred, false);
            }

            // 5. Отрисовка ленты HISTORY TRACK внизу полотна
            drawHistoryTrack(g, historyRect);
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
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
            surfAtPosition(e.position);
        }

        void mouseDrag(const juce::MouseEvent& e) override
        {
            const float historyHeight = 28.0f;
            if (e.position.y >= getHeight() - historyHeight)
                return;

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

            // Если зажат Command/Alt или Shift — перетаскиваем активный сэмпл в пэд или в DAW
            if ((e.mods.isCommandDown() || e.mods.isAltDown() || e.mods.isShiftDown()) && activeSampleIndex >= 0)
            {
                if (!isDraggingSample && e.position.getDistanceFrom(dragStartPos) > 6.0f)
                {
                    isDraggingSample = true;
                    if (onSampleDragged)
                        onSampleDragged(activeSampleIndex, localPointToGlobal(e.position.toInt()));
                }
                return;
            }

            // Бесшовный серфинг по созвездиям
            surfAtPosition(e.position);
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

        void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
        {
            float factor = (wheel.deltaY > 0.0f) ? 1.15f : 0.87f;
            zoomLevel = juce::jlimit(1.0f, 7.0f, zoomLevel * factor);
            if (zoomLevel <= 1.02f)
                panOffset = { 0.0f, 0.0f };
            repaint();
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
            float zy = (1.0f - normY - 0.5f - panOffset.y) * zoomLevel + 0.5f; // Яркие сверху, темные снизу

            float px = rect.getX() + 28.0f + zx * (rect.getWidth() - 56.0f);
            float py = rect.getY() + 24.0f + zy * (rect.getHeight() - 48.0f);
            return { px, py };
        }

        void surfAtPosition(juce::Point<float> mousePos)
        {
            if (database == nullptr || database->empty()) return;

            const auto mapRect = getLocalBounds().toFloat().withTrimmedBottom(28.0f);
            const auto& db = *database;

            int closestIdx = -1;
            float minDistSq = 22.0f * 22.0f; // Радиус захвата курсором 22 пикселя

            for (int i = 0; i < (int)db.size(); ++i)
            {
                const auto& rec = db[i];
                if (rec.isLoop != loopsTabActive || !checkFilterMatch(rec))
                    continue;

                auto pt = mapToScreen(rec.mapX, rec.mapY, mapRect);
                float dSq = pt.getDistanceSquaredFrom(mousePos);
                if (dSq < minDistSq)
                {
                    minDistSq = dSq;
                    closestIdx = i;
                }
            }

            if (closestIdx >= 0 && closestIdx != activeSampleIndex)
            {
                triggerSamplePoint(closestIdx);
            }
        }

        void triggerSamplePoint(int idx)
        {
            if (database == nullptr || idx < 0 || idx >= (int)database->size())
                return;

            activeSampleIndex = idx;
            sampler.auditionSample(idx, (*database)[idx]);

            // Добавляем в историю (до 15 точек без дублирования подряд)
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
            g.setColour(juce::Colour(0xff161b22));
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

        void drawWaveformCallout(juce::Graphics& g, juce::Point<float> pt,
                                 const SampleRecord& rec, const juce::Rectangle<float>& mapRect)
        {
            const float boxW = 168.0f;
            const float boxH = 54.0f;

            float bx = pt.x + 16.0f;
            float by = pt.y - boxH - 10.0f;
            if (bx + boxW > mapRect.getRight() - 8.0f) bx = pt.x - boxW - 16.0f;
            if (by < mapRect.getY() + 8.0f) by = pt.y + 16.0f;

            juce::Rectangle<float> box(bx, by, boxW, boxH);

            // Тонкая соединительная линия от точки к карточке волны
            g.setColour(juce::Colour(0xaa8b98a9));
            g.drawLine(pt.x, pt.y, box.getCentreX(), box.getBottom(), 1.0f);

            g.setColour(juce::Colour(0xe8141920));
            g.fillRoundedRectangle(box, 3.0f);
            g.setColour(juce::Colour(0xff384454));
            g.drawRoundedRectangle(box, 3.0f, 1.0f);

            // Рисуем мини-волну
            auto audio = sampler.getAuditionAudio();
            if (audio != nullptr)
            {
                auto waveArea = box.reduced(8.0f, 6.0f).withTrimmedBottom(16.0f);
                juce::Colour waveCol = MusicMath::getKeyColour(rec.pitchClass, 0.8f);
                g.setColour(waveCol.withAlpha(0.85f));

                const auto& peaks = audio->waveformPeaks;
                const float barW = waveArea.getWidth() / (float)peaks.size();
                const float midY = waveArea.getCentreY();

                for (size_t k = 0; k < peaks.size(); ++k)
                {
                    float h = std::max(1.5f, peaks[k] * waveArea.getHeight() * 0.9f);
                    g.fillRect(waveArea.getX() + (float)k * barW, midY - h * 0.5f, std::max(1.0f, barW - 0.5f), h);
                }
            }

            // Имя сэмпла и его нота
            g.setColour(juce::Colour(0xffd5dde6));
            g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::plain));
            juce::String label = rec.name.substring(0, 18) + " [" + MusicMath::getNoteName(rec.pitchClass) + "]";
            g.drawText(label, box.reduced(6.0f, 3.0f).removeFromBottom(16.0f),
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

            // Отрисовка 15 узлов истории
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
                    int sIdx = historyIndices[i];
                    const auto& rec = (*database)[sIdx];
                    bool isHovered = (sIdx == highlightedHistorySampleIndex);
                    bool isLatest  = (i == (int)historyIndices.size() - 1);

                    if (isHovered)
                    {
                        juce::Colour keyCol = MusicMath::getKeyColour(rec.pitchClass, 0.85f);
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

            // Справа выводим имя последнего прослушанного сэмпла
            if (database != nullptr && activeSampleIndex >= 0 && activeSampleIndex < (int)database->size())
            {
                const auto& rec = (*database)[activeSampleIndex];
                g.setColour(juce::Colour(0xff8c99a8));
                juce::String info = rec.name + ".wav  [" + MusicMath::getNoteName(rec.pitchClass) + " | -6dB]";
                g.drawText(info, rect.withTrimmedRight(12.0f).withWidth(280.0f).withX(rect.getRight() - 292.0f),
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
                if ( std::abs(pos.x - cx) < 10.0f )
                {
                    foundSampleIdx = historyIndices[i];
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

        float zoomLevel = 1.0f;
        juce::Point<float> panOffset { 0.0f, 0.0f };
        juce::Point<float> lastPanPos;
        juce::Point<float> dragStartPos;
        bool isDraggingSample = false;
        float pulsePhase = 0.0f;
    };
}
