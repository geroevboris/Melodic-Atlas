#pragma once
#include "SamplerEngine.h"
#include "PercussionGate.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
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
            clusterSubMasks.fill(0);
        }

        ~MapCanvasComponent() override
        {
            stopTimer();
        }

        void setSampleDatabase(std::shared_ptr<const std::vector<SampleRecord>> dbSnapshot)
        {
            database = std::move(dbSnapshot);
            // Require complete coverage: a partial imported projection must
            // not mix neural and legacy coordinates on the same map.
            neuralMapActive = database != nullptr && !database->empty();
            if (neuralMapActive)
                for (const auto& record : *database)
                    if (!record.neuralMapValid)
                    {
                        neuralMapActive = false;
                        break;
                    }
            candidateOrderDirty = true;
            visibleSamplesDirty = true;
            activeSampleIndex = -1;
            hoveredSampleIndex = -1;
            auditionHistory.clear();
            repaint();
        }

        void setViewFilter(bool loopsTab,
                           uint32_t clustersMask,
                           const std::array<uint32_t, 7>& perClusterSubs,
                           uint32_t moodMask)
        {
            showLoopsOnly = loopsTab;
            activeClustersMask = clustersMask;
            clusterSubMasks = perClusterSubs;
            activeMoodMask = moodMask;
            candidateOrderDirty = true;
            visibleSamplesDirty = true;
            repaint();
        }

        int getActiveSampleIndex() const
        {
            return activeSampleIndex;
        }

        void selectSampleFromPad(int sampleIdx)
        {
            if (database == nullptr || sampleIdx < 0 || sampleIdx >= (int)database->size())
                return;

            activeSampleIndex = sampleIdx;
            repaint();
        }

        std::function<void(int sampleIdx)> onSampleRightClicked;

        void resized() override
        {
            visibleSamplesDirty = true;
        }

        void paint(juce::Graphics& g) override
        {
            const float w = (float)getWidth();
            const float h = (float)getHeight();
            const float mapBottomY = h - 26.0f;

            g.setColour(juce::Colour(0xff0f1319));
            g.fillRect(0.0f, 0.0f, w, mapBottomY);

            g.setColour(juce::Colour(0xff161c24));
            for (int c = 1; c < 16; ++c)
                g.drawVerticalLine((int)((w / 16.0f) * (float)c), 0.0f, mapBottomY);
            for (int r = 1; r < 8; ++r)
                g.drawHorizontalLine((int)((mapBottomY / 8.0f) * (float)r), 0.0f, w);

            if (crosshairAlpha > 0.01f)
            {
                g.setColour(juce::Colour(0xff7ec8a3).withAlpha(crosshairAlpha * 0.35f));
                g.drawHorizontalLine((int)lastClickPos.y, 0.0f, w);
                g.drawVerticalLine((int)lastClickPos.x, 26.0f, mapBottomY);
                float rippleRadius = (1.0f - crosshairAlpha) * 36.0f;
                g.drawEllipse(lastClickPos.x - rippleRadius,
                              lastClickPos.y - rippleRadius,
                              rippleRadius * 2.0f,
                              rippleRadius * 2.0f,
                              1.4f);
            }

            if (database != nullptr)
            {
                ensureVisibleSamples();
                for (int i : visibleSampleIndices)
                {
                    const auto& rec = (*database)[(size_t)i];
                    auto pt = mapToScreen(rec);
                    if (pt.x < -4.0f || pt.x > w + 4.0f ||
                        pt.y < 26.0f || pt.y > mapBottomY - 4.0f)
                    {
                        continue;
                    }

                    bool isActive = (i == activeSampleIndex);
                    bool isHovered = (i == hoveredSampleIndex);

                    juce::Colour dotCol = MusicMath::getTimbreColour(rec);

                    float radius = isActive ? 3.6f : (isHovered ? 2.8f : 1.9f);

                    if (isActive)
                    {
                        g.setColour(dotCol.withAlpha(isHoldingSample ? 0.55f : 0.30f));
                        float haloR = isHoldingSample ? 9.5f : 6.5f;
                        g.fillEllipse(pt.x - haloR, pt.y - haloR, haloR * 2.0f, haloR * 2.0f);

                        if (isHoldingSample)
                        {
                            g.setColour(juce::Colours::white.withAlpha(0.85f));
                            g.drawEllipse(pt.x - 11.0f, pt.y - 11.0f, 22.0f, 22.0f, 1.4f);
                        }
                    }

                    g.setColour(isActive ? juce::Colours::white : dotCol.brighter(0.18f));
                    g.fillEllipse(pt.x - radius, pt.y - radius, radius * 2.0f, radius * 2.0f);
                }
            }

            drawTopOverlayAndZoomControls(g, w);
            drawBottomHistoryBar(g, w, h, mapBottomY);

            g.setColour(juce::Colour(0xff262e3b));
            g.drawRect(0.0f, 0.0f, w, mapBottomY, 1.0f);
        }

        void mouseMove(const juce::MouseEvent& e) override
        {
            if (zoomOutRect.contains(e.position) ||
                zoomInRect.contains(e.position) ||
                zoomResetRect.contains(e.position))
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
            isPanningMap = false;
            isAuditionSweeping = false;
            isHoldingSample = false;
            lastPanPos = pt;

            juce::Point<float> centerPt((float)getWidth() * 0.5f,
                                        ((float)getHeight() - 26.0f) * 0.5f);

            if (zoomOutRect.contains(pt))
            {
                setZoomAroundPoint(zoomFactor / 1.35f, centerPt);
                return;
            }
            if (zoomInRect.contains(pt))
            {
                setZoomAroundPoint(zoomFactor * 1.35f, centerPt);
                return;
            }
            if (zoomResetRect.contains(pt))
            {
                zoomFactor = 1.0f;
                visibleSamplesDirty = true;
                panOffset = { 0.0f, 0.0f };
                repaint();
                return;
            }

            const float mapBottomY = (float)getHeight() - 26.0f;

            if (pt.y >= mapBottomY)
            {
                for (size_t i = 0; i < historyNodeRects.size(); ++i)
                {
                    if (historyNodeRects[i].contains(pt) && i < auditionHistory.size())
                    {
                        int histSampleIdx = auditionHistory[i];
                        activeSampleIndex = histSampleIdx;
                        isHoldingSample = true;
                        triggerSamplePreview(histSampleIdx, false);
                        break;
                    }
                }
                return;
            }

            int hit = findNearestSample(pt, 12.0f);

            // Правый клик отправляет в пэд БЕЗ звука предпрослушки
            if (e.mods.isPopupMenu() || e.mods.isMiddleButtonDown())
            {
                if (hit >= 0 && onSampleRightClicked && e.mods.isPopupMenu())
                {
                    activeSampleIndex = hit;
                    onSampleRightClicked(hit);
                    repaint();
                }
                else
                {
                    isPanningMap = true;
                }
                return;
            }

            if (hit >= 0)
            {
                // Клик по точке: запуск прослушивания
                isAuditionSweeping = true;
                lastSweptSampleIndex = hit;
                activeSampleIndex = hit;
                isHoldingSample = true;
                triggerSamplePreview(hit, true);
            }
            else
            {
                // Клик в пустое место: включение панорамирования (Grab & Drag) + радарный отклик
                isPanningMap = true;
                lastClickPos = pt;
                crosshairAlpha = 1.0f;
                startTimerHz(30);
                repaint();
            }
        }

        void mouseDrag(const juce::MouseEvent& e) override
        {
            auto pt = e.position;

            if (isPanningMap)
            {
                panOffset.x += (pt.x - lastPanPos.x);
                panOffset.y += (pt.y - lastPanPos.y);
                lastPanPos = pt;
                clampPanOffset();
                repaint();
                return;
            }

            if (isAuditionSweeping)
            {
                const float mapBottomY = (float)getHeight() - 26.0f;
                if (pt.y >= mapBottomY)
                    return;

                int hit = findNearestSample(pt, 12.0f);
                if (hit >= 0 && hit != lastSweptSampleIndex)
                {
                    uint32_t nowMs = juce::Time::getMillisecondCounter();
                    if (nowMs - lastPreviewTriggerMs >= 18)
                    {
                        lastPreviewTriggerMs = nowMs;
                        lastSweptSampleIndex = hit;
                        activeSampleIndex = hit;
                        isHoldingSample = true;
                        triggerSamplePreview(hit, true);
                    }
                }
                else if (hit < 0 && lastSweptSampleIndex >= 0)
                {
                    lastSweptSampleIndex = -1;
                    isHoldingSample = false;
                    samplerEngine.stopPreview();
                    repaint();
                }
            }
        }

        void mouseUp(const juce::MouseEvent&) override
        {
            isAuditionSweeping = false;
            isPanningMap = false;
            isHoldingSample = false;
            lastSweptSampleIndex = -1;
            samplerEngine.stopPreview();
            repaint();
        }

        void mouseWheelMove(const juce::MouseEvent& e,
                            const juce::MouseWheelDetails& wheel) override
        {
            if (std::abs(wheel.deltaY) > 0.0001f)
            {
                // Smaller steps on trackpads avoid abrupt density changes.
                float zoomMultiplier = std::pow(1.16f, wheel.deltaY * 3.2f);
                setZoomAroundPoint(zoomFactor * zoomMultiplier, e.position);
            }

            if (std::abs(wheel.deltaX) > 0.001f && zoomFactor > 1.01f)
            {
                panOffset.x += wheel.deltaX * 180.0f;
                clampPanOffset();
                repaint();
            }
        }

        void timerCallback() override
        {
            if (crosshairAlpha > 0.0f)
            {
                crosshairAlpha -= 0.06f;
                if (crosshairAlpha <= 0.0f)
                {
                    crosshairAlpha = 0.0f;
                    stopTimer();
                }
                repaint();
            }
        }

    private:
        void drawTopOverlayAndZoomControls(juce::Graphics& g, float w)
        {
            const juce::Font monoSmall(
                juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::bold);

            g.setColour(juce::Colour(0xe60f1319));
            g.fillRect(1.0f, 1.0f, w - 2.0f, 26.0f);

            // Legend describes the colour axis (timbre), not the
            // independent instrument family filters above the map.
            const juce::Colour colours[] = {
                juce::Colour(0xffa789ca), juce::Colour(0xff5b85c8),
                juce::Colour(0xffe2ad68), juce::Colour(0xffce7b76),
                juce::Colour(0xff65b7a5), juce::Colour(0xffb4a16e)
            };
            const char* names[] = {
                "TEXTURE", "DEEP", "BRIGHT", "PLUCK",
                "SOFT", "WARM"
            };
            float lx = 10.0f;
            for (int c = 0; c < 6; ++c)
            {
                g.setColour(colours[c]);
                g.fillEllipse(lx, 10.0f, 6.0f, 6.0f);
                lx += 9.0f;
                g.setColour(juce::Colour(0xff9aa7b8));
                g.setFont(monoSmall);
                const juce::String name(names[c]);
                const float tw = (float)name.length() * 6.5f + 8.0f;
                g.drawText(name, (int)lx, 4, (int)tw, 18,
                           juce::Justification::centredLeft, false);
                lx += tw;
            }

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
            const juce::Font monoSmall(
                juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain);

            g.setColour(juce::Colour(0xff131820));
            g.fillRect(0.0f, mapBottomY, w, h - mapBottomY);

            g.setColour(juce::Colour(0xff7f8c9d));
            g.setFont(monoSmall);
            g.drawText("HISTORY:", 8, (int)mapBottomY, 60, 26,
                       juce::Justification::centredLeft, false);

            historyNodeRects.clear();
            float hx = 74.0f;
            float hy = mapBottomY + 13.0f;

            if (database != nullptr)
            {
                for (size_t i = 0; i < auditionHistory.size(); ++i)
                {
                    int sIdx = auditionHistory[i];
                    if (sIdx < 0 || sIdx >= (int)database->size())
                        continue;

                    const auto& rec = (*database)[(size_t)sIdx];
                    juce::Rectangle<float> nodeRect(hx - 4.5f, hy - 4.5f, 9.0f, 9.0f);
                    historyNodeRects.push_back(nodeRect.expanded(4.0f));

                    juce::Colour c = MusicMath::getTimbreColour(rec);
                    g.setColour(c);
                    g.fillEllipse(nodeRect);

                    if (sIdx == activeSampleIndex)
                    {
                        g.setColour(juce::Colours::white);
                        g.drawEllipse(nodeRect.expanded(1.5f), 1.3f);
                    }

                    hx += 16.0f;
                }
            }

            if (database != nullptr &&
                activeSampleIndex >= 0 &&
                activeSampleIndex < (int)database->size())
            {
                const auto& rec = (*database)[(size_t)activeSampleIndex];
                juce::String info = rec.name + " [" +
                                    MusicMath::getNoteName(rec.pitchClass) + " | " +
                                    MusicMath::getClusterName(displayCluster(rec)) + "]";
                g.setColour(juce::Colour(0xffb8c4d4));
                g.drawText(info,
                           (int)(w * 0.45f), (int)mapBottomY,
                           (int)(w * 0.54f) - 8, 26,
                           juce::Justification::centredRight, true);
            }
        }

        void triggerSamplePreview(int sampleIdx, bool recordInHistory)
        {
            if (database == nullptr || sampleIdx < 0 || sampleIdx >= (int)database->size())
                return;

            if (recordInHistory)
            {
                if (auditionHistory.empty() || auditionHistory.back() != sampleIdx)
                {
                    auto it = std::find(auditionHistory.begin(),
                                        auditionHistory.end(), sampleIdx);
                    if (it != auditionHistory.end())
                        auditionHistory.erase(it);

                    auditionHistory.push_back(sampleIdx);
                    if (auditionHistory.size() > 24)
                        auditionHistory.erase(auditionHistory.begin());
                }
            }

            samplerEngine.startPreview(sampleIdx, (*database)[(size_t)sampleIdx]);
            repaint();
        }

        static ClusterGroup displayCluster(const SampleRecord& rec)
        {
            if (rec.identityState == IdentityState::Identified
                && (int)rec.identityCluster >= 0
                && (int)rec.identityCluster < 7)
                return rec.identityCluster;
            return ClusterGroup::Other;
        }

        bool matchesCurrentFilter(const SampleRecord& rec) const
        {
            // A cached legacy entry may be present in the database for old
            // projects/pad history, but must not appear on the melodic map.
            if (PercussionGate::shouldExcludeFromAtlas(rec))
                return false;

            // Only a validated instrument identity can satisfy instrument
            // filters. Unidentified/legacy samples are still available under
            // ALL as neutral points, not falsely labelled as synthesizers.
            const bool identified =
                rec.identityState == IdentityState::Identified
                && (int)rec.identityCluster >= 0
                && (int)rec.identityCluster < 7;
            const int cIdx = identified ? (int)rec.identityCluster : 7;

            if (activeClustersMask != 0)
            {
                if (cIdx == 7 || (activeClustersMask & (1u << cIdx)) == 0)
                    return false;

                uint32_t reqClusterSub = clusterSubMasks[(size_t)cIdx];
                if (reqClusterSub != 0 && (rec.subMask & reqClusterSub) == 0)
                    return false;
            }

            if (activeMoodMask != 0)
            {
                // Category selection ORs instrument groups; character tags
                // independently intersect with those groups (AND).
                // Unrescanned legacy cache entries retain the old mood tags.
                const uint32_t tags = rec.acoustic.valid
                    ? rec.characterMask : rec.subMask;
                const uint32_t matched = tags & activeMoodMask;
                // Strict intersection: every selected quality must match.
                // An empty map is an intentional and meaningful result.
                if (matched != activeMoodMask)
                    return false;
            }

            return true;
        }

        juce::Point<float> effectiveMapCoordinates(const SampleRecord& rec) const
        {
            return neuralMapActive
                ? juce::Point<float>(rec.neuralMapX, rec.neuralMapY)
                : juce::Point<float>(rec.mapX, rec.mapY);
        }

        juce::Point<float> mapToScreen(const SampleRecord& rec) const
        {
            const auto pt = effectiveMapCoordinates(rec);
            return mapToScreen(pt.x, pt.y);
        }

        juce::Point<float> mapToScreen(float mx, float my) const
        {
            const float w = (float)getWidth();
            const float h = (float)getHeight() - 26.0f;
            float baseX = 28.0f + mx * (w - 56.0f);
            float baseY = 34.0f + my * (h - 50.0f);
            return {
                w * 0.5f + (baseX - w * 0.5f) * zoomFactor + panOffset.x,
                h * 0.5f + (baseY - h * 0.5f) * zoomFactor + panOffset.y
            };
        }

        // Semantic zoom: show a deterministic, collision-free subset at each
        // zoom level. Every marker is a real playable sample. Zooming reveals
        // additional samples without changing any sample's map coordinates.
        // Use world-space pixels (no pan offset) so moving the viewport does
        // not reshuffle which samples exist at a given zoom level.
        void ensureVisibleSamples() const
        {
            if (!visibleSamplesDirty)
                return;
            visibleSamplesDirty = false;
            visibleSampleIndices.clear();
            if (database == nullptr || getWidth() <= 0 || getHeight() <= 26)
                return;

            // Sorting and category checks are O(N log N). Cache that work
            // across trackpad zoom events; rebuild only after scan/filter changes.
            if (candidateOrderDirty)
            {
                candidateOrderDirty = false;
                sortedCandidateIndices.clear();
                sortedCandidateIndices.reserve(database->size());
                for (int i = 0; i < (int)database->size(); ++i)
                {
                    const auto& r = (*database)[(size_t)i];
                    if (r.isLoop == showLoopsOnly && matchesCurrentFilter(r))
                        sortedCandidateIndices.push_back(i);
                }
                auto priorityFor = [](int i) -> uint32_t
                {
                    uint32_t key = (uint32_t)i + 0x9e3779b9u;
                    key ^= key >> 16;
                    key *= 0x7feb352du;
                    key ^= key >> 15;
                    key *= 0x846ca68bu;
                    key ^= key >> 16;
                    return key;
                };
                std::sort(sortedCandidateIndices.begin(), sortedCandidateIndices.end(),
                    [&](int a, int b)
                    {
                        const auto pa = priorityFor(a), pb = priorityFor(b);
                        return pa == pb ? a < b : pa < pb;
                    });
            }

            // Increase separation for click/drag audition. This is display-
            // only LOD; the persistent semantic coordinates never move.
            constexpr float minSpacingPx = 15.0f;
            const float width = (float)getWidth();
            const float height = (float)getHeight() - 26.0f;
            const float cellSize = minSpacingPx;
            const float minDistSq = minSpacingPx * minSpacingPx;
            using CellKey = uint64_t;
            struct Occupant { float x, y; };
            std::unordered_map<CellKey, std::vector<Occupant>> cells;
            cells.reserve(sortedCandidateIndices.size());

            auto cellKey = [](int x, int y) -> CellKey
            {
                return ((uint64_t)(uint32_t)x << 32) | (uint32_t)y;
            };

            for (int candidateIndex : sortedCandidateIndices)
            {
                const auto& rec = (*database)[(size_t)candidateIndex];
                // Use the very same transform as the renderer, minus pan.
                const auto coords = effectiveMapCoordinates(rec);
                const float x = (width * 0.5f +
                    (28.0f + coords.x * (width - 56.0f) - width * 0.5f) * zoomFactor);
                const float y = (height * 0.5f +
                    (34.0f + coords.y * (height - 50.0f) - height * 0.5f) * zoomFactor);
                if (!std::isfinite(x) || !std::isfinite(y))
                    continue;

                const int cellX = (int)std::floor(x / cellSize);
                const int cellY = (int)std::floor(y / cellSize);
                bool overlaps = false;
                for (int dy = -1; dy <= 1 && !overlaps; ++dy)
                    for (int dx = -1; dx <= 1 && !overlaps; ++dx)
                    {
                        const auto it = cells.find(cellKey(cellX + dx, cellY + dy));
                        if (it == cells.end())
                            continue;
                        for (const auto& other : it->second)
                        {
                            const float ddx = x - other.x, ddy = y - other.y;
                            if (ddx * ddx + ddy * ddy < minDistSq)
                            {
                                overlaps = true;
                                break;
                            }
                        }
                    }
                if (!overlaps)
                {
                    cells[cellKey(cellX, cellY)].push_back({ x, y });
                    visibleSampleIndices.push_back(candidateIndex);
                }
            }
        }

        int findNearestSample(juce::Point<float> mousePt, float maxRadiusPx) const
        {
            if (database == nullptr)
                return -1;

            int bestIdx = -1;
            float bestDistSq = maxRadiusPx * maxRadiusPx;

            // Interaction follows the exact same LOD selection as painting:
            // invisible samples must never intercept a mouse click.
            ensureVisibleSamples();
            for (int i : visibleSampleIndices)
            {
                const auto& rec = (*database)[(size_t)i];
                float dSq = mousePt.getDistanceSquaredFrom(mapToScreen(rec));
                if (dSq < bestDistSq)
                {
                    bestDistSq = dSq;
                    bestIdx = i;
                }
            }
            return bestIdx;
        }

        void setZoomAroundPoint(float newZoom, juce::Point<float> anchorScreenPt)
        {
            float clampedZoom = juce::jlimit(1.0f, 18.0f, newZoom);
            if (std::abs(clampedZoom - zoomFactor) < 0.0001f)
                return;

            const float cx = (float)getWidth() * 0.5f;
            const float cy = ((float)getHeight() - 26.0f) * 0.5f;

            float ratio = clampedZoom / zoomFactor;
            panOffset.x = (anchorScreenPt.x - cx) * (1.0f - ratio) + panOffset.x * ratio;
            panOffset.y = (anchorScreenPt.y - cy) * (1.0f - ratio) + panOffset.y * ratio;

            zoomFactor = clampedZoom;
            visibleSamplesDirty = true;
            clampPanOffset();
            repaint();
        }

        void clampPanOffset()
        {
            float maxPanX = (float)getWidth() * (zoomFactor - 1.0f) * 0.55f + 40.0f;
            float maxPanY = ((float)getHeight() - 26.0f) * (zoomFactor - 1.0f) * 0.55f + 40.0f;
            panOffset.x = juce::jlimit(-maxPanX, maxPanX, panOffset.x);
            panOffset.y = juce::jlimit(-maxPanY, maxPanY, panOffset.y);
        }

        SamplerEngine& samplerEngine;
        std::shared_ptr<const std::vector<SampleRecord>> database;

        bool showLoopsOnly = false;
        uint32_t activeClustersMask = 0;
        std::array<uint32_t, 7> clusterSubMasks;
        uint32_t activeMoodMask = 0;

        int activeSampleIndex = -1;
        int hoveredSampleIndex = -1;
        int lastSweptSampleIndex = -1;
        uint32_t lastPreviewTriggerMs = 0;

        // Recomputed only when data, filters, viewport size or zoom changes.
        // Panning and auditioning do not alter the visible representatives.
        bool neuralMapActive = false;
        mutable bool candidateOrderDirty = true;
        mutable std::vector<int> sortedCandidateIndices;
        mutable bool visibleSamplesDirty = true;
        mutable std::vector<int> visibleSampleIndices;

        float zoomFactor = 1.0f;
        juce::Point<float> panOffset { 0.0f, 0.0f };
        juce::Point<float> lastPanPos;

        bool isPanningMap = false;
        bool isAuditionSweeping = false;
        bool isHoldingSample = false;

        juce::Point<float> lastClickPos { -100.0f, -100.0f };
        float crosshairAlpha = 0.0f;

        juce::Rectangle<float> zoomOutRect, zoomInRect, zoomResetRect;
        std::vector<int> auditionHistory;
        std::vector<juce::Rectangle<float>> historyNodeRects;
    };
}
