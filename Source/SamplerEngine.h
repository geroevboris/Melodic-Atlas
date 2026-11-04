#pragma once
#include "SamplerEngine.h"
#include <unordered_map>

namespace Atlas
{
    class MapCanvasComponent : public juce::Component
    {
    public:
        explicit MapCanvasComponent(SamplerEngine& engine)
            : samplerEngine(engine)
        {
        }

        ~MapCanvasComponent() override {}

        void setSampleDatabase(const std::vector<SampleRecord>* dbPtr)
        {
            database = dbPtr;
            activeSampleIndex = -1;
            hoveredSampleIndex = -1;
            auditionHistory.clear();
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

        void selectSampleFromPad(int sampleIdx)
        {
            if (database == nullptr || sampleIdx < 0 || sampleIdx >= (int)database->size())
                return;

            activeSampleIndex = sampleIdx;
            repaint();
        }

        std::function<void(int sampleIdx)> onSampleRightClicked;
        std::function<void(int sampleIdx, juce::Point<int> globalMousePos, bool dragFinished)> onSampleDragged;

        void paint(juce::Graphics& g) override
        {
            const float w = (float)getWidth();
            const float h = (float)getHeight();
            const float mapBottomY = h - 26.0f;

            // 1. Фон и координатная сетка
            g.setColour(juce::Colour(0xff0f1319));
            g.fillRect(0.0f, 0.0f, w, mapBottomY);

            g.setColour(juce::Colour(0xff171d26));
            for (int c = 1; c < 16; ++c)
                g.drawVerticalLine((int)((w / 16.0f) * (float)c), 0.0f, mapBottomY);
            for (int r = 1; r < 8; ++r)
                g.drawHorizontalLine((int)((mapBottomY / 8.0f) * (float)r), 0.0f, w);

            // 2. Отрисовка точек сэмплов (стиль XO - четкие, без свечения, скрытие неактивных)
            if (database != nullptr)
            {
                for (int i = 0; i < (int)database->size(); ++i)
                {
                    const auto& rec = (*database)[(size_t)i];
                    if (rec.isLoop != showLoopsOnly) continue;
                    
                    bool passesFilter = matchesCurrentFilter(rec);
                    if (!passesFilter) continue; // Точки других фильтров полностью скрыты!

                    auto pt = mapToScreen(rec.mapX, rec.mapY);
                    if (pt.x < -4.0f || pt.x > w + 4.0f || pt.y < 24.0f || pt.y > mapBottomY - 4.0f)
                        continue;

                    bool isActive = (i == activeSampleIndex);
                    bool isHovered = (i == hoveredSampleIndex);

                    juce::Colour dotCol = MusicMath::getClusterColour(rec.cluster, rec.toneShadeBucket);
                    float radius = isActive ? 3.5f : (isHovered ? 2.8f : 1.8f); // Мелкие контрастные точки

                    if (isActive)
                    {
                        g.setColour(dotCol.withAlpha(0.4f));
                        g.fillEllipse(pt.x - 7.0f, pt.y - 7.0f, 14.0f, 14.0f);
                    }

                    g.setColour(isActive ? juce::Colours::white : dotCol.brighter(0.2f));
                    g.fillEllipse(pt.x - radius, pt.y - radius, radius * 2.0f, radius * 2.0f);
                }
            }

            // 3. Призрак при Drag-and-Drop
            if (isDraggingSample && activeSampleIndex >= 0 && database != nullptr)
            {
                const auto& rec = (*database)[(size_t)activeSampleIndex];
                juce::Rectangle<float> ghost(currentDragPos.x - 60.0f, currentDragPos.y - 12.0f, 120.0f, 24.0f);
                g.setColour(juce::Colour(0xcc182029));
                g.fillRoundedRectangle(ghost, 4.0f);
                g.setColour(MusicMath::getClusterColour(rec.cluster, rec.toneShadeBucket));
                g.drawRoundedRectangle(ghost, 4.0f, 1.5f);
                g.setColour(juce::Colours::white);
                g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::bold));
                g.drawText(rec.name.substring(0, 16), ghost, juce::Justification::centred, true);
            }

            drawTopOverlayAndZoomControls(g, w);
            drawBottomHistoryBar(g, w, h, mapBottomY);

            g.setColour(juce::Colour(0xff262e3b));
            g.drawRect(0.0f, 0.0f, w, mapBottomY, 1.0f);
        }

        void mouseMove(const juce::MouseEvent& e) override
        {
            if (zoomOutRect.contains(e.position) || zoomInRect.contains(e.position) || zoomResetRect.contains(e.position))
            {
                if (hoveredSampleIndex != -1) { hoveredSampleIndex = -1; repaint(); }
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
            isDraggingSample = false;
            isPanningMap = false;
            isAuditionSweeping = false;
            didPanDuringDrag = false;
            lastPanPos = pt;

            if (zoomOutRect.contains(pt)) { setZoomAroundCenter(zoomFactor - 0.25f); return; }
            if (zoomInRect.contains(pt))  { setZoomAroundCenter(zoomFactor + 0.25f); return; }
            if (zoomResetRect.contains(pt)){ zoomFactor = 1.0f; panOffset = {0,0}; repaint(); return; }

            const float mapBottomY = (float)getHeight() - 26.0f;
            if (pt.y >= mapBottomY)
            {
                for (size_t i = 0; i < historyNodeRects.size(); ++i)
                    if (historyNodeRects[i].contains(pt) && i < auditionHistory.size())
                        triggerSamplePreview(auditionHistory[i]);
                return;
            }

            int hit = findNearestSample(pt, 12.0f);

            // Правый клик / Зажатие двух пальцев на тачпаде
            if (e.mods.isPopupMenu() || e.mods.isMiddleButtonDown())
            {
                isPanningMap = true;
                rightClickSampleIdx = hit;
                return;
            }

            // Левый клик по точке
            if (hit >= 0)
            {
                isAuditionSweeping = true;
                lastSweptSampleIndex = hit;
                triggerSamplePreview(hit);
                activeSampleIndex = hit;
                currentDragPos = pt;
            }
            else
            {
                // Клик в пустоту - начало выделения рамкой (в будущих версиях) или просто свайп
                isAuditionSweeping = true;
            }
        }

        void mouseDrag(const juce::MouseEvent& e) override
        {
            auto pt = e.position;

            if (isPanningMap)
            {
                if (pt.getDistanceFrom(lastPanPos) > 3.0f) didPanDuringDrag = true;
                panOffset.x += (pt.x - lastPanPos.x);
                panOffset.y -= (pt.y - lastPanPos.y); // Инверсия (естественный скролл)
                lastPanPos = pt;
                clampPanOffset();
                repaint();
                return;
            }

            if (activeSampleIndex >= 0 && pt.getDistanceFrom(e.getMouseDownPosition()) > 5.0f)
            {
                isAuditionSweeping = false;
                isDraggingSample = true;
                currentDragPos = pt;
                if (onSampleDragged) onSampleDragged(activeSampleIndex, e.getScreenPosition(), false);
                repaint();
                return;
            }

            if (isAuditionSweeping)
            {
                int hit = findNearestSample(pt, 12.0f);
                if (hit >= 0 && hit != lastSweptSampleIndex)
                {
                    lastSweptSampleIndex = hit;
                    activeSampleIndex = hit;
                    triggerSamplePreview(hit);
                }
            }
        }

        void mouseUp(const juce::MouseEvent& e) override
        {
            if (isDraggingSample && activeSampleIndex >= 0)
            {
                isDraggingSample = false;
                if (onSampleDragged) onSampleDragged(activeSampleIndex, e.getScreenPosition(), true);
                samplerEngine.stopPreview();
                repaint();
                return;
            }

            // Назначение по правому клику (без панорамирования)
            if (isPanningMap && !didPanDuringDrag && rightClickSampleIdx >= 0 && onSampleRightClicked)
            {
                activeSampleIndex = rightClickSampleIdx;
                onSampleRightClicked(rightClickSampleIdx);
                repaint();
            }

            isPanningMap = false;
            isAuditionSweeping = false;
            lastSweptSampleIndex = -1;
            samplerEngine.stopPreview();
        }

        // Плавный зум (свайп вверх/вниз двумя пальцами на тачпаде)
        void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
        {
            // На маке свайп вверх дает положительный deltaY. 
            // Приближение (zoom in) при свайпе вверх, отдаление (zoom out) при свайпе вниз.
            float step = wheel.deltaY * 0.8f; 
            
            // Если есть горизонтальный скролл - используем для панорамы
            if (std::abs(wheel.deltaX) > std::abs(wheel.deltaY))
            {
                panOffset.x += wheel.deltaX * 200.0f;
            }
            else
            {
                setZoomAroundCenter(zoomFactor + step);
            }
            
            clampPanOffset();
            repaint();
        }

    private:
        void drawTopOverlayAndZoomControls(juce::Graphics& g, float w)
        {
            const juce::Font monoSmall(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::bold);
            g.setColour(juce::Colour(0xe60f1319));
            g.fillRect(1.0f, 1.0f, w - 2.0f, 26.0f);

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

            zoomOutRect   = { w - 106.0f, 4.0f, 24.0f, 20.0f };
            zoomInRect    = { w - 78.0f,  4.0f, 24.0f, 20.0f };
            zoomResetRect = { w - 50.0f,  4.0f, 42.0f, 20.0f };

            auto drawZoomBtn = [&](juce::Rectangle<float> r, const juce::String& txt) {
                g.setColour(juce::Colour(0xff1e2530)); g.fillRoundedRectangle(r, 3.0f);
                g.setColour(juce::Colour(0xff455264)); g.drawRoundedRectangle(r, 3.0f, 1.0f);
                g.setColour(juce::Colour(0xffe2eaf2)); g.setFont(monoSmall);
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
            g.drawText("HISTORY:", 8, (int)mapBottomY, 60, 26, juce::Justification::centredLeft, false);

            historyNodeRects.clear();
            float hx = 74.0f;
            float hy = mapBottomY + 13.0f;

            for (size_t i = 0; i < auditionHistory.size(); ++i)
            {
                int sIdx = auditionHistory[i];
                const auto& rec = (*database)[(size_t)sIdx];
                juce::Colour c = MusicMath::getClusterColour(rec.cluster, rec.toneShadeBucket);

                juce::Rectangle<float> nodeRect(hx - 4.0f, hy - 4.0f, 8.0f, 8.0f);
                historyNodeRects.push_back(nodeRect.expanded(4.0f));

                g.setColour(c);
                g.fillEllipse(nodeRect);
                hx += 16.0f;
            }

            if (database != nullptr && activeSampleIndex >= 0 && activeSampleIndex < (int)database->size())
            {
                const auto& rec = (*database)[(size_t)activeSampleIndex];
                juce::String info = rec.name + " [" + MusicMath::getNoteName(rec.pitchClass) + " | "
                                  + MusicMath::getClusterName(rec.cluster) + "]";
                g.setColour(juce::Colour(0xffb8c4d4));
                g.drawText(info, (int)(w * 0.45f), (int)mapBottomY, (int)(w * 0.54f) - 8, 26,
                           juce::Justification::centredRight, true);
            }
        }

        void triggerSamplePreview(int sampleIdx)
        {
            if (database == nullptr || sampleIdx < 0 || sampleIdx >= (int)database->size()) return;

            auto it = std::find(auditionHistory.begin(), auditionHistory.end(), sampleIdx);
            if (it != auditionHistory.end()) auditionHistory.erase(it);
            auditionHistory.push_back(sampleIdx);
            if (auditionHistory.size() > 24) auditionHistory.erase(auditionHistory.begin());

            samplerEngine.startPreview(sampleIdx, (*database)[(size_t)sampleIdx]);
            repaint();
        }

        bool matchesCurrentFilter(const SampleRecord& rec) const
        {
            if (activeClusterFilter >= 0 && (int)rec.cluster != activeClusterFilter) return false;
            // Мультивыбор фильтров: проверяем пересечение масок
            if (activeSubMask != 0 && (rec.subMask & activeSubMask) == 0) return false;
            return true;
        }

        juce::Point<float> mapToScreen(float mx, float my) const
        {
            const float w = (float)getWidth();
            const float h = (float)getHeight() - 26.0f;
            float baseX = 28.0f + mx * (w - 56.0f);
            float baseY = 34.0f + my * (h - 50.0f);
            return { w * 0.5f + (baseX - w * 0.5f) * zoomFactor + panOffset.x,
                     h * 0.5f + (baseY - h * 0.5f) * zoomFactor + panOffset.y };
        }

        int findNearestSample(juce::Point<float> mousePt, float maxRadiusPx) const
        {
            if (database == nullptr) return -1;
            int bestIdx = -1;
            float bestDistSq = maxRadiusPx * maxRadiusPx;

            for (int i = 0; i < (int)database->size(); ++i)
            {
                const auto& rec = (*database)[(size_t)i];
                if (rec.isLoop != showLoopsOnly) continue;
                if (!matchesCurrentFilter(rec)) continue;

                float dSq = mousePt.getDistanceSquaredFrom(mapToScreen(rec.mapX, rec.mapY));
                if (dSq < bestDistSq) { bestDistSq = dSq; bestIdx = i; }
            }
            return bestIdx;
        }

        void setZoomAroundCenter(float newZoom)
        {
            zoomFactor = juce::jlimit(1.0f, 6.0f, newZoom); // Глубокий зум
            clampPanOffset();
            repaint();
        }

        void clampPanOffset()
        {
            float maxPanX = (float)getWidth() * (zoomFactor - 1.0f) * 0.5f + 40.0f;
            float maxPanY = (float)getHeight() * (zoomFactor - 1.0f) * 0.5f + 40.0f;
            panOffset.x = juce::jlimit(-maxPanX, maxPanX, panOffset.x);
            panOffset.y = juce::jlimit(-maxPanY, maxPanY, panOffset.y);
        }

        SamplerEngine& samplerEngine;
        const std::vector<SampleRecord>* database = nullptr;

        bool showLoopsOnly = false;
        int activeClusterFilter = -1;
        uint32_t activeSubMask = 0;

        int activeSampleIndex = -1;
        int hoveredSampleIndex = -1;
        int lastSweptSampleIndex = -1;
        int rightClickSampleIdx = -1;

        float zoomFactor = 1.0f;
        juce::Point<float> panOffset { 0.0f, 0.0f };
        juce::Point<float> lastPanPos;
        juce::Point<float> currentDragPos;

        bool isDraggingSample = false;
        bool isPanningMap = false;
        bool isAuditionSweeping = false;
        bool didPanDuringDrag = false;

        juce::Rectangle<float> zoomOutRect, zoomInRect, zoomResetRect;
        std::vector<int> auditionHistory;
        std::vector<juce::Rectangle<float>> historyNodeRects;
    };
}
