#pragma once
#include "SamplerEngine.h"
#include "PercussionGate.h"
#include "AtlasDurationPolicy.h"
#include "SoundIslandLayout.h"
#include "AtlasPointCoverage10.h"
#include "OrganicIslandCloud11.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <unordered_set>

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
            // Library changes invalidate temporary sample indices.
            discoveryActive = false;
            discoveryReturning = false;
            discoveryIndices.clear();
            discoveryPositions.clear();
            discoveryExitStartPixels.clear();
            returningNormalIndices.clear();
            discoveryMorph = 1.0f;
            // Keep the existing LOD selection when the ONLY change is a
            // manual Hide, otherwise one removed marker makes more appear.
            // Reuse packed coordinates so manual curation does not shuffle
            // neighbours at the same zoom.
            bool hideOnly = database && dbSnapshot &&
                database->size() == dbSnapshot->size() &&
                !visibleSamplesDirty &&
                coveragePositions.size() == database->size();
            bool anyNewHide = false;
            if (hideOnly)
                for(size_t i=0;i<database->size();++i)
                {
                    const auto& before=(*database)[i];
                    const auto& after=(*dbSnapshot)[i];
                    if(before.file != after.file
                       ||before.mapX!=after.mapX
                       ||before.mapY!=after.mapY
                       ||before.neuralMapX!=after.neuralMapX
                       ||before.neuralMapY!=after.neuralMapY
                       ||before.isLoop!=after.isLoop
                       ||before.identityCluster!=after.identityCluster
                       ||before.identityState!=after.identityState
                       ||before.dynamicSubgroup!=after.dynamicSubgroup
                       ||before.characterMask!=after.characterMask
                       ||before.hiddenFromAtlas && !after.hiddenFromAtlas)
                    {
                        hideOnly=false;
                        break;
                    }
                    if(!before.hiddenFromAtlas && after.hiddenFromAtlas)
                        anyNewHide=true;
                }
            hideOnly = hideOnly && anyNewHide;
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
            if (!hideOnly)
                rebuildCoveragePositions();
            rebuildIslandOverlays();
            candidateOrderDirty = !hideOnly;
            if (hideOnly)
            {
                sortedCandidateIndices.erase(
                    std::remove_if(sortedCandidateIndices.begin(),
                        sortedCandidateIndices.end(),
                        [this](int i) {
                            return i<0 ||i>=(int)database->size()
                                ||(*database)[(size_t)i].hiddenFromAtlas;
                        }),sortedCandidateIndices.end());
            }
            if (hideOnly)
            {
                visibleSampleIndices.erase(
                    std::remove_if(visibleSampleIndices.begin(),
                        visibleSampleIndices.end(),
                        [this](int i) {
                            return i<0 ||i>=(int)database->size()
                                ||(*database)[(size_t)i].hiddenFromAtlas;
                        }),visibleSampleIndices.end());
                visibleSamplesDirty = false;
            }
            else visibleSamplesDirty = true;
            activeSampleIndex = -1;
            hoveredSampleIndex = -1;
            auditionHistory.clear();
            repaint();
        }

        void setViewFilter(bool loopsTab,
                           uint32_t clustersMask,
                           const std::array<uint32_t, 7>& perClusterSubs,
                           const std::array<juce::String, 7>& perClusterDynamicSubs,
                           uint32_t moodMask)
        {
            if (discoveryActive)
                endDiscovery();
            showLoopsOnly = loopsTab;
            activeClustersMask = clustersMask;
            clusterSubMasks = perClusterSubs;
            activeDynamicSubgroups = perClusterDynamicSubs;
            activeMoodMask = moodMask;
            rebuildIslandOverlays();
            candidateOrderDirty = true;
            visibleSamplesDirty = true;
            repaint();
        }

        bool isDiscoveryActive() const { return discoveryActive; }

        // Choose random playable samples from the WHOLE library, ignoring
        // instrument, character and one-shot/loop filters. No database
        // mutation, no recomputation of neural geometry or saved state.
        void rollDiscovery(int count = 200)
        {
            if (!database) return;
            std::vector<int> choices;
            choices.reserve(database->size());
            for (int i = 0; i < (int)database->size(); ++i)
            {
                const auto& rec = (*database)[(size_t)i];
                if (!rec.hiddenFromAtlas &&
                    AtlasDurationPolicy::isEligible(rec.durationSec) &&
                    !PercussionGate::shouldExcludeFromAtlas(rec))
                    choices.push_back(i);
            }
            if (choices.empty()) return;

            if (!discoveryActive && !discoveryReturning)
            {
                originalZoom = zoomFactor;
                originalPan = panOffset;
            }
            discoveryActive = true;
            discoveryReturning = false;
            discoveryExitStartPixels.clear();
            zoomFactor = 1.0f;
            panOffset = {0.0f, 0.0f};
            auto& random = juce::Random::getSystemRandom();
            for (int i = 0; i < (int)choices.size(); ++i)
            {
                const int j = i + random.nextInt((int)choices.size() - i);
                std::swap(choices[(size_t)i], choices[(size_t)j]);
            }
            const int n = juce::jmin(count, (int)choices.size());
            discoveryIndices.assign(choices.begin(), choices.begin() + n);
            discoveryPositions.clear();
            // Low-discrepancy radial positions: every prefix (60, 100, 140,
            // 180, 200) fills the cloud. Zoom reveals markers without
            // relocating previous ones. Keep circular proportions in pixels.
            const float visibleH = juce::jmax(1.0f,
                (float)getHeight() - 26.0f);
            const float visibleW = juce::jmax(1.0f, (float)getWidth());
            const float ratio = visibleH / visibleW;
            const float angleOffset = random.nextFloat() *
                juce::MathConstants<float>::twoPi;
            for (int i = 0; i < n; ++i)
            {
                uint32_t index=(uint32_t)(i + 1);
                float radical=0.0f, place=0.5f;
                while(index>0u)
                {
                    radical += place*(float)(index&1u);
                    index >>= 1u;
                    place *= 0.5f;
                }
                const float radius=std::sqrt(radical);
                const float angle=angleOffset+(float)i*2.39996323f;
                discoveryPositions.emplace(discoveryIndices[(size_t)i],
                    juce::Point<float>(
                        0.5f + 0.30f * ratio * radius * std::cos(angle),
                        0.5f + 0.30f * radius * std::sin(angle)));
            }
            discoveryMorph = 0.0f;
            candidateOrderDirty = true;
            visibleSamplesDirty = true;
            startTimerHz(30);
            repaint();
        }

        // Return by dispersing the SAME visible random points to the
        // original CLAP islands; no changes to persistent sample geometry.
        void endDiscovery()
        {
            if (!discoveryActive) return;
            discoveryExitStartPixels.clear();
            discoveryExitCount = discoveryVisibleCount();
            for (int i=0; i<discoveryExitCount; ++i)
            {
                const int idx=discoveryIndices[(size_t)i];
                discoveryExitStartPixels[idx]=mapToScreenForIndex(idx);
            }
            discoveryActive = false;
            discoveryReturning = true;
            discoveryMorph = 1.0f;
            discoveryReturnStartZoom = zoomFactor;
            discoveryReturnStartPan = panOffset;
            discoveryReturnStartMs = juce::Time::getMillisecondCounter();
            candidateOrderDirty = true;
            visibleSamplesDirty = true;
            // Animate camera and points together; never jump to the saved
            // zoom/pan before the first return frame.
            startTimerHz(60);
            repaint();
        }

        int discoveryVisibleCount() const
        {
            const int pool=(int)discoveryIndices.size();
            if(pool==0)return 0;
            // 60 at 1x, 100 at 1.5x, 140 at 2x, 180 at 3x, 200 at 4x+.
            static constexpr float levels[]={1.0f,1.5f,2.0f,3.0f,4.0f};
            static constexpr int counts[]={60,100,140,180,200};
            int shown=200;
            if(zoomFactor<=levels[0])shown=counts[0];
            else for(int i=1;i<5;++i)
                if(zoomFactor<=levels[i])
                {
                    const float alpha=(zoomFactor-levels[i-1])
                        /(levels[i]-levels[i-1]);
                    shown=juce::roundToInt(juce::jmap(alpha,
                        (float)counts[i-1],(float)counts[i]));
                    break;
                }
            return juce::jmin(pool,shown);
        }

        void showParameterValue(const juce::String& message)
        {
            temporaryParameterText = message;
            parameterUntil = juce::Time::getMillisecondCounter() + 1400u;
            startTimerHz(30);
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
        std::function<void(int sampleIdx, juce::Point<int> screenPoint)> onSampleManageRequested;
        std::function<void(juce::Point<int> screenPoint)> onEmptyMapRightClicked;

        void resized() override
        {
            rebuildCoveragePositions();
            rebuildIslandOverlays();
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
                if (!discoveryActive)
                {
                    const float t = 1.0f - discoveryMorph;
                    const float fade = discoveryReturning
                        ? t * t * (3.0f - 2.0f * t) : 1.0f;
                    drawSoundIslands(g, fade);
                }
                for (int i : visibleSampleIndices)
                {
                    const auto& rec = (*database)[(size_t)i];
                    auto pt = mapToScreenForIndex(i);
                    if (pt.x < -4.0f || pt.x > w + 4.0f ||
                        pt.y < 26.0f || pt.y > mapBottomY - 4.0f)
                    {
                        continue;
                    }

                    bool isActive = (i == activeSampleIndex);
                    bool isHovered = (i == hoveredSampleIndex);

                    // Random samples travel to their saved map positions.
                    // New normal samples fade in, while random-only samples
                    // fade away. Neither layer suddenly appears at the end.
                    float dotOpacity = 1.0f;
                    if (discoveryReturning)
                    {
                        const float t = 1.0f - discoveryMorph;
                        const float fade = t * t * (3.0f - 2.0f * t);
                        const bool wasRandom =
                            discoveryExitStartPixels.find(i) != discoveryExitStartPixels.end();
                        const bool inNormal =
                            returningNormalIndices.find(i) != returningNormalIndices.end();
                        dotOpacity = wasRandom
                            ? (inNormal ? 1.0f : 1.0f - fade) : fade;
                    }
                    if (dotOpacity <= 0.001f)
                        continue;

                    juce::Colour dotCol = MusicMath::getClapFamilyColour(rec);

                    // Three stable, deliberately restrained point sizes
                    // (88% of the prototype scale). No glow or soft halos.
                    const uint32_t hash = (uint32_t)
                        rec.file.getFullPathName().hashCode();
                    const unsigned sizeGroup = hash % 100u;
                    float radius = sizeGroup < 62u ? 1.45f
                        : (sizeGroup < 88u ? 1.92f : 2.55f);
                    if (isHovered) radius = juce::jmax(radius, 2.85f);
                    if (isActive)
                    {
                        radius = 3.35f;
                        g.setColour(juce::Colours::white.withAlpha(0.86f * dotOpacity));
                        g.drawEllipse(pt.x - radius - 2.4f,
                                      pt.y - radius - 2.4f,
                                      2.0f * (radius + 2.4f),
                                      2.0f * (radius + 2.4f), 1.0f);
                    }
                    g.setColour((isActive ? juce::Colours::white : dotCol)
                        .withMultipliedAlpha(dotOpacity));
                    g.fillEllipse(pt.x - radius, pt.y - radius,
                                  radius * 2.0f, radius * 2.0f);
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

            if (e.mods.isPopupMenu())
            {
                if (hit >= 0)
                {
                    activeSampleIndex = hit;
                    // Normal right-click ALWAYS assigns the sound to a pad.
                    // Shift + right-click is the separate management menu.
                    if (e.mods.isShiftDown() && onSampleManageRequested)
                        onSampleManageRequested(hit, e.getScreenPosition());
                    else if (onSampleRightClicked)
                        onSampleRightClicked(hit);
                }
                else if (e.mods.isShiftDown() && onEmptyMapRightClicked)
                    onEmptyMapRightClicked(e.getScreenPosition());
                else
                    isPanningMap = true;
                repaint();
                return;
            }
            if (e.mods.isMiddleButtonDown())
            {
                isPanningMap = true;
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
            bool active = false;
            if (crosshairAlpha > 0.0f)
            {
                crosshairAlpha = juce::jmax(0.0f, crosshairAlpha - 0.06f);
                active = crosshairAlpha > 0.0f;
                repaint();
            }
            if (discoveryActive && discoveryMorph < 1.0f)
            {
                discoveryMorph = juce::jmin(1.0f, discoveryMorph + 0.13f);
                active = discoveryMorph < 1.0f || active;
                repaint();
            }
            if (discoveryReturning)
            {
                // Clock-based 550 ms ease-in/ease-out: frame-rate independent.
                const uint32_t elapsed =
                    juce::Time::getMillisecondCounter() - discoveryReturnStartMs;
                const float t = juce::jlimit(0.0f, 1.0f,
                    (float)elapsed / 550.0f);
                const float eased = t * t * (3.0f - 2.0f * t);
                discoveryMorph = 1.0f - t;
                zoomFactor = juce::jmap(eased,
                    discoveryReturnStartZoom, originalZoom);
                panOffset = {
                    juce::jmap(eased, discoveryReturnStartPan.x, originalPan.x),
                    juce::jmap(eased, discoveryReturnStartPan.y, originalPan.y)
                };
                visibleSamplesDirty = true;
                if (t >= 1.0f)
                {
                    discoveryReturning = false;
                    discoveryIndices.clear();
                    discoveryPositions.clear();
                    discoveryExitStartPixels.clear();
                    returningNormalIndices.clear();
                    discoveryExitCount = 0;
                    candidateOrderDirty = true;
                    visibleSamplesDirty = true;
                    rebuildIslandOverlays();
                }
                else active = true;
                repaint();
            }
            if (temporaryParameterText.isNotEmpty())
            {
                if ((int32_t)(juce::Time::getMillisecondCounter()
                            - parameterUntil) >= 0)
                {
                    temporaryParameterText.clear();
                }
                else active = true;
                repaint();
            }
            if (!active) stopTimer();
        }

    private:
        void rebuildCoveragePositions()
        {
            coveragePositions.clear();
            if(!database || getWidth()<100 || getHeight()<100)
                return;
            coveragePositions.resize(database->size(),
                                     {-1000.0f,-1000.0f});
            std::vector<OrganicIslandCloud11::Point> sources;
            std::vector<int> islandIds;
            std::vector<uint32_t> fileSeeds;
            std::vector<int> indices;
            sources.reserve(database->size());
            islandIds.reserve(database->size());
            fileSeeds.reserve(database->size());
            indices.reserve(database->size());
            for(int i=0;i<(int)database->size();++i)
            {
                const auto& rec=(*database)[(size_t)i];
                // Explicitly hidden records KEEP their allocated cell so
                // removing a sound cannot rearrange its neighbours.
                // Permanently excluded short/drum samples are not shown
                // and do not consume scarce space.
                if(!AtlasDurationPolicy::isEligible(rec.durationSec)
                   || PercussionGate::shouldExcludeFromAtlas(rec))
                    continue;
                const auto pos=effectiveMapCoordinates(rec);
                sources.push_back({pos.x,pos.y});
                islandIds.push_back(rec.neuralMapValid
                    ? rec.visualIslandGroup : -1);
                fileSeeds.push_back((uint32_t)
                    rec.file.getFullPathName().hashCode());
                indices.push_back(i);
            }
            // Visual-only: slightly bloom overly horizontal CLAP neighbourhoods.
            // Semantic groups and cached coordinates remain untouched.
            const auto organic=OrganicIslandCloud11::arrange(
                sources,islandIds,fileSeeds,
                (float)getWidth(),(float)getHeight()-26.0f);
            std::vector<AtlasPointCoverage10::Point> packedSources;
            packedSources.reserve(organic.size());
            for (const auto& p:organic)
                packedSources.push_back({p.x,p.y});
            const auto packed=AtlasPointCoverage10::pack(packedSources,
                (float)getWidth(),(float)getHeight()-26.0f);
            for(size_t n=0;n<indices.size();++n)
                coveragePositions[(size_t)indices[n]]={
                    packed[n].x,packed[n].y};
        }

        juce::Point<float> coverageMapPosition(int i) const
        {
            if(i>=0 && (size_t)i<coveragePositions.size())
            {
                const auto& p=coveragePositions[(size_t)i];
                if(p.x>-100.0f && p.y>-100.0f)
                    return p;
            }
            return effectiveMapCoordinates((*database)[(size_t)i]);
        }

        void rebuildIslandOverlays()
        {
            for (auto& island : islandOverlays)
                island = {};
            if (database == nullptr)
                return;
            for (int idx=0;idx<(int)database->size();++idx)
            {
                const auto& rec=(*database)[(size_t)idx];
                if (rec.visualIslandGroup < 0 || rec.visualIslandGroup >= SoundIslandLayout::maxIslands
                    || !rec.neuralMapValid || rec.isLoop != showLoopsOnly
                    || !matchesCurrentFilter(rec))
                    continue;
                auto& island = islandOverlays[(size_t)rec.visualIslandGroup];
                island.islandId = rec.visualIslandGroup;
                const auto position=coverageMapPosition(idx);
                island.points.push_back(position);
                ++island.count;
                island.centreX += position.x;
                island.centreY += position.y;
                if (island.colourGroup < 0)
                {
                    island.colourGroup = rec.colourGroup;
                    island.attackShadeLevel = rec.attackShadeLevel;
                }
            }
            constexpr int angles = 48;
            constexpr float tau = juce::MathConstants<float>::twoPi;
            for (auto& island : islandOverlays)
            {
                if (island.count == 0)
                    continue;
                island.centreX /= (float)island.count;
                island.centreY /= (float)island.count;

                // A smoothed, density-supported radial envelope: lobe sizes
                // follow the actual filtered sample positions, never an
                // ellipse fitted to their bounding box. Precomputed only
                // on database/filter changes, not during painting.
                std::array<float, angles> original {};
                for (int i = 0; i < angles; ++i)
                {
                    const float angle = tau * (float)i / (float)angles;
                    const float ux = std::cos(angle);
                    const float uy = std::sin(angle);
                    std::vector<float> supports;
                    supports.reserve(1200);
                    // The outline samples at most 1200 points per island.
                    // Density still counts every matching file; moving the
                    // filter remains responsive with very large libraries.
                    const size_t stride = std::max<size_t>(
                        1, (island.points.size() + 1199) / 1200);
                    for (size_t p = 0; p < island.points.size(); p += stride)
                    {
                        const auto& pt = island.points[p];
                        const float dx = pt.x - island.centreX;
                        const float dy = pt.y - island.centreY;
                        const float along = dx * ux + dy * uy;
                        if (along <= 0.0f)
                            continue;
                        const float perpendicular = std::abs(dx * uy - dy * ux);
                        const float distance = std::sqrt(dx * dx + dy * dy);
                        const float spread = std::max(0.009f, distance * 0.48f);
                        const float closeness =
                            std::exp(-0.5f * (perpendicular / spread)
                                           * (perpendicular / spread));
                        if (closeness > 0.45f)
                            supports.push_back(along * closeness);
                    }
                    // A few distant samples must not stretch a whole island
                    // into a giant backdrop across otherwise empty space.
                    float extent = 0.0f;
                    if (!supports.empty())
                    {
                        const size_t percentile =
                            (size_t)(0.82f * (float)(supports.size() - 1));
                        std::nth_element(supports.begin(),
                            supports.begin() + (std::ptrdiff_t)percentile,
                            supports.end());
                        extent = supports[percentile];
                    }
                    original[(size_t)i] = juce::jlimit(
                        0.010f, 0.090f, extent + 0.010f);
                }
                double radialSquareSum = 0.0;
                for (int i = 0; i < angles; ++i)
                {
                    const int prev = (i + angles - 1) % angles;
                    const int next = (i + 1) % angles;
                    const float r = 0.25f * original[(size_t)prev]
                        + 0.5f * original[(size_t)i]
                        + 0.25f * original[(size_t)next];
                    island.radial[(size_t)i] = r;
                    radialSquareSum += (double)r * r;
                }
                const float meanSq = (float)(radialSquareSum / angles);
                const float area = juce::MathConstants<float>::pi * meanSq;
                const float density = (float)island.count
                    / std::max(1.0f, area * 9500.0f);
                // The fill represents ALL sounds in the island (not only
                // LOD representatives). Preserve useful density contrast
                // over a broad range of library sizes.
                island.fillAlpha = juce::jlimit(0.045f, 0.22f,
                    0.045f + 0.175f *
                    (1.0f - std::exp(-0.55f * std::sqrt(density))));
                island.points.clear();
            }
        }

        void drawSoundIslands(juce::Graphics& g, float opacity = 1.0f)
        {
            if (!neuralMapActive || database == nullptr)
                return;
            constexpr int angles = 48;
            constexpr float tau = juce::MathConstants<float>::twoPi;
            for (const auto& island : islandOverlays)
            {
                if (island.count < 2)
                    continue;
                std::array<juce::Point<float>, angles> positions {};
                for (int i = 0; i < angles; ++i)
                {
                    const float angle = tau * (float)i / (float)angles;
                    const float radius = island.radial[(size_t)i];
                    positions[(size_t)i] = mapToScreen(
                        island.centreX + std::cos(angle) * radius,
                        island.centreY + std::sin(angle) * radius);
                }
                auto midpoint = [](juce::Point<float> p, juce::Point<float> q)
                {
                    return juce::Point<float>(
                        (p.x + q.x) * 0.5f, (p.y + q.y) * 0.5f);
                };
                juce::Path outline;
                const auto start = midpoint(
                    positions[(size_t)(angles - 1)], positions[0]);
                outline.startNewSubPath(start);
                for (int i = 0; i < angles; ++i)
                {
                    const auto control = positions[(size_t)i];
                    const auto end = midpoint(control,
                        positions[(size_t)((i + 1) % angles)]);
                    outline.quadraticTo(control, end);
                }
                outline.closeSubPath();

                SampleRecord display;
                display.colourGroup = island.colourGroup;
                display.attackShadeLevel = island.attackShadeLevel;
                display.islandGroup = island.islandId;
                const auto colour = MusicMath::getClapFamilyColour(display);
                // Colour intensity represents filtered sample concentration,
                // even when semantic zoom hides almost all marker dots.
                g.setColour(colour.withAlpha(island.fillAlpha * opacity));
                g.fillPath(outline);
                g.setColour(colour.withAlpha(0.27f * opacity));
                g.strokePath(outline, juce::PathStrokeType(1.05f));
            }
        }

        void drawTopOverlayAndZoomControls(juce::Graphics& g, float w)
        {
            const juce::Font monoSmall(
                juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::bold);

            g.setColour(juce::Colour(0xe60f1319));
            g.fillRect(1.0f, 1.0f, w - 2.0f, 26.0f);

            // Stable musical register, low to high from left to right.
            // Colours no longer encode attack or per-sound lightness.
            g.setFont(monoSmall);
            // The map header never changes mode: only the atlas moves.
            static constexpr const char* names[] = {
                "LOW", "LOW-MID", "HIGH-MID", "HIGH"
            };
            float lx = 12.0f;
            for (int c = 0; c < 4; ++c)
            {
                g.setColour(MusicMath::getSpectralRegionColour(c, -1));
                g.fillEllipse(lx, 10.0f, 7.0f, 7.0f);
                lx += 11.0f;
                g.setColour(juce::Colour(0xff9aa7b8));
                g.drawText(names[c], (int)lx, 4, 59, 18,
                           juce::Justification::centredLeft, false);
                lx += 72.0f;
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
            if (w > 950.0f)
            {
                // Same location and label in every mode. Count only sounds
                // actually shown in the current normal / discovery view.
                const int shown=discoveryActive
                    ? discoveryVisibleCount()
                    : (discoveryReturning
                        ? (int)returningNormalIndices.size()
                        : (int)visibleSampleIndices.size());
                const int total=discoveryActive
                    ? (int)discoveryIndices.size()
                    : (int)sortedCandidateIndices.size();
                const juce::String progress = "POINTS " +
                    juce::String(shown) + " / " + juce::String(total);
                g.setColour(juce::Colour(0xff8c9dab));
                g.drawText(progress,(int)w-355,4,240,20,
                           juce::Justification::centredRight,true);
            }
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

                    juce::Colour c = MusicMath::getClapFamilyColour(rec);
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

            if (temporaryParameterText.isNotEmpty())
            {
                g.setColour(juce::Colour(0xff96d1a4));
                g.setFont(juce::Font(juce::Font::getDefaultSansSerifFontName(),
                                    12.0f, juce::Font::bold));
                g.drawText(temporaryParameterText,
                           (int)(w * 0.45f), (int)mapBottomY,
                           (int)(w * 0.54f) - 8, 26,
                           juce::Justification::centredRight, true);
            }
            else if (database != nullptr &&
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
            if (rec.hiddenFromAtlas
                || !AtlasDurationPolicy::isEligible(rec.durationSec)
                || PercussionGate::shouldExcludeFromAtlas(rec))
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

                // 07: dynamically discovered names, not static bit flags.
                const auto& requested = activeDynamicSubgroups[(size_t)cIdx];
                if (requested.isNotEmpty()
                    && rec.dynamicSubgroup != requested)
                    return false;
                const uint32_t legacy = clusterSubMasks[(size_t)cIdx];
                if (requested.isEmpty() && legacy != 0
                    && (rec.subMask & legacy) == 0)
                    return false;
            }

            if (activeMoodMask != 0)
            {
                // Character is an independent measured DSP property.
                // Never reinterpret instrument subtype bits as Character
                // when acoustic analysis is unavailable.
                const uint32_t tags = rec.acoustic.valid
                    ? rec.characterMask : 0u;
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
            auto point=neuralMapActive
                ? juce::Point<float>(rec.neuralMapX, rec.neuralMapY)
                : juce::Point<float>(rec.mapX, rec.mapY);
            // Purely visual: narrower across X, taller along Y. Apply
            // identically to dots, island outlines, LOD and hit testing.
            // Cached CLAP positions, semantic register and pads are intact.
            point.x=juce::jlimit(0.0f,1.0f,
                0.5f + 0.86f*(point.x-0.5f));
            point.y=juce::jlimit(0.0f,1.0f,
                0.5f + 1.38f*(point.y-0.5f));
            return point;
        }

        juce::Point<float> mapToScreenForIndex(int sampleIndex) const
        {
            const auto& rec = (*database)[(size_t)sampleIndex];
            if (discoveryReturning)
            {
                const auto it=discoveryExitStartPixels.find(sampleIndex);
                if(it!=discoveryExitStartPixels.end())
                {
                    const float alpha=discoveryMorph*discoveryMorph*
                        (3.0f-2.0f*discoveryMorph);
                    const auto target=coverageMapPosition(sampleIndex);
                    const auto end=mapToScreen(target.x,target.y);
                    return {
                        juce::jmap(alpha,end.x,it->second.x),
                        juce::jmap(alpha,end.y,it->second.y)
                    };
                }
            }
            if (discoveryActive)
            {
                const auto it=discoveryPositions.find(sampleIndex);
                if(it!=discoveryPositions.end())
                {
                    const auto original=coverageMapPosition(sampleIndex);
                    const auto goal=it->second;
                    const float alpha=discoveryMorph*discoveryMorph*
                        (3.0f-2.0f*discoveryMorph);
                    return mapToScreen(
                        juce::jmap(alpha,original.x,goal.x),
                        juce::jmap(alpha,original.y,goal.y));
                }
            }
            const auto point=coverageMapPosition(sampleIndex);
            return mapToScreen(point.x,point.y);
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
        void appendReturningDiscoverySamples() const
        {
            if (!discoveryReturning)
                return;
            returningNormalIndices.clear();
            returningNormalIndices.insert(
                visibleSampleIndices.begin(), visibleSampleIndices.end());
            for (int n = 0; n < discoveryExitCount
                 && n < (int)discoveryIndices.size(); ++n)
            {
                const int idx = discoveryIndices[(size_t)n];
                if (returningNormalIndices.find(idx) ==
                    returningNormalIndices.end())
                    visibleSampleIndices.push_back(idx);
            }
        }

        void ensureVisibleSamples() const
        {
            if (!visibleSamplesDirty)
                return;
            visibleSamplesDirty = false;
            visibleSampleIndices.clear();
            returningNormalIndices.clear();
            if (database == nullptr || getWidth() <= 0 || getHeight() <= 26)
                return;
            if(discoveryActive)
            {
                const int count=discoveryVisibleCount();
                visibleSampleIndices.assign(
                    discoveryIndices.begin(),
                    discoveryIndices.begin()+juce::jlimit(0,
                        (int)discoveryIndices.size(),count));
                return;
            }

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

            // All eligible sounds MUST be reachable. The old LOD silently
            // capped density even at 18x, so hiding one marker could reveal
            // several more. At maximum zoom the collision-free packing
            // guarantees a separate dot for EVERY filtered sample.
            if (zoomFactor >= AtlasPointCoverage10::referenceZoom - 0.001f)
            {
                visibleSampleIndices = sortedCandidateIndices;
                appendReturningDiscoverySamples();
                return;
            }
            // More information already at overview zoom. Spacing steadily
            // decreases with zoom; no threshold that increases on approach.
            const float logZoom = std::log(
                juce::jlimit(1.0f, AtlasPointCoverage10::referenceZoom,
                            zoomFactor));
            const float normalizedZoom=logZoom/
                std::log(AtlasPointCoverage10::referenceZoom);
            const float minSpacingPx=juce::jmap(
                normalizedZoom,8.0f,5.7f);
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
                const auto coords = coverageMapPosition(candidateIndex);
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
            appendReturningDiscoverySamples();
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
                float dSq = mousePt.getDistanceSquaredFrom(mapToScreenForIndex(i));
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
        std::array<juce::String, 7> activeDynamicSubgroups {};
        uint32_t activeMoodMask = 0;

        int activeSampleIndex = -1;
        int hoveredSampleIndex = -1;
        int lastSweptSampleIndex = -1;
        uint32_t lastPreviewTriggerMs = 0;

        // Recomputed only when data, filters, viewport size or zoom changes.
        // Panning and auditioning do not alter the visible representatives.
        struct IslandOverlay
        {
            std::vector<juce::Point<float>> points;
            std::array<float, 48> radial {};
            float centreX = 0.0f, centreY = 0.0f;
            float fillAlpha = 0.065f;
            int count = 0, colourGroup = -1, islandId = -1;
            int attackShadeLevel = 1;
        };
        std::array<IslandOverlay, SoundIslandLayout::maxIslands> islandOverlays {};
        bool neuralMapActive = false;
        mutable bool candidateOrderDirty = true;
        mutable std::vector<int> sortedCandidateIndices;
        mutable bool visibleSamplesDirty = true;
        mutable std::vector<int> visibleSampleIndices;
        mutable std::unordered_set<int> returningNormalIndices;
        // Collision-free display positions at the existing 18x maximum.
        // Never persisted in CLAP or project state.
        std::vector<juce::Point<float>> coveragePositions;

        // UI-only temporary random discovery cloud.
        bool discoveryActive = false;
        bool discoveryReturning = false;
        int discoveryExitCount = 0;
        std::vector<int> discoveryIndices;
        std::unordered_map<int, juce::Point<float>> discoveryPositions;
        std::unordered_map<int, juce::Point<float>> discoveryExitStartPixels;
        float discoveryMorph = 1.0f;
        uint32_t discoveryReturnStartMs = 0;
        float discoveryReturnStartZoom = 1.0f;
        juce::Point<float> discoveryReturnStartPan {0.0f, 0.0f};
        float originalZoom = 1.0f;
        juce::Point<float> originalPan {0.0f, 0.0f};
        juce::String temporaryParameterText;
        uint32_t parameterUntil = 0;

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
