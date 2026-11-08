#pragma once
#include "AtlasTypes.h"
#include <initializer_list>

namespace Atlas
{
    // Explicit instrument names are supporting evidence, not CLAP output.
    // No speculative labels are assigned when audio/metadata is ambiguous.
    struct InstrumentSubgroupTags
    {
        static void evaluate(const juce::File& file, SampleRecord& rec)
        {
            juce::StringArray words;
            const auto append = [&words](const juce::String& text)
            {
                juce::StringArray tokens;
                tokens.addTokens(text.toLowerCase(),
                                 " _-.()[]{}0123456789", "");
                words.addArray(tokens);
            };
            append(file.getFileNameWithoutExtension());
            append(file.getParentDirectory().getFileName());
            // Avoid using arbitrary sample-pack grandparents to invent labels.
            const auto contains = [&words](
                std::initializer_list<const char*> tokens) -> bool
            {
                for (const char* token : tokens)
                    if (words.contains(juce::String(token)))
                        return true;
                return false;
            };

            switch (rec.cluster)
            {
                case ClusterGroup::Keys:
                    if (contains({"piano", "grand", "upright", "pno"}))
                        rec.subMask |= Sub_Piano;
                    if (contains({"rhodes", "wurli", "wurlitzer", "epiano",
                                  "electricpiano"}))
                        rec.subMask |= Sub_ElectricPiano;
                    if (contains({"organ", "hammond", "b3", "farfisa"}))
                        rec.subMask |= Sub_Organ;
                    if (contains({"mallet", "marimba", "kalimba", "vibes",
                                  "vibraphone", "xylophone", "glock",
                                  "celesta", "bell"}))
                        rec.subMask |= Sub_Mallet;
                    break;
                case ClusterGroup::Synth:
                    if (contains({"pad", "pads", "drone"}))
                        rec.subMask |= Sub_Pad;
                    if (contains({"lead", "leads"}))
                        rec.subMask |= Sub_Lead;
                    if (contains({"pluck", "plucks", "plucky"}))
                        rec.subMask |= Sub_Pluck;
                    if (contains({"arp", "arpeggio", "arpeggiator", "arps",
                                  "sequence", "sequencer"}))
                        rec.subMask |= Sub_Arp;
                    break;
                case ClusterGroup::Bass:
                    if (contains({"sub", "subbass", "808"}))
                        rec.subMask |= Sub_SubBass;
                    if (contains({"synth", "reese", "moog"}))
                        rec.subMask |= Sub_SynthBass;
                    if (contains({"electric", "ebass", "fender"}))
                        rec.subMask |= Sub_ElectricBass;
                    if (contains({"acoustic", "upright", "contrabass"}))
                        rec.subMask |= Sub_AcousticBass;
                    break;
                case ClusterGroup::Vocal:
                    if (contains({"sing", "singing", "voice", "vocal",
                                  "vocals", "singer", "vox", "speech"}))
                        rec.subMask |= Sub_VocalSinging;
                    if (contains({"chop", "chops", "adlib", "phrase"}))
                        rec.subMask |= Sub_VocalChops;
                    if (contains({"choir", "choral", "ensemble"}))
                        rec.subMask |= Sub_Choir;
                    break;
                case ClusterGroup::Strings:
                    if (contains({"string", "strings", "violin", "viola",
                                  "cello", "fiddle", "harp", "pizzicato",
                                  "pizz", "quartet"}))
                        rec.subMask |= Sub_StringsSection;
                    if (contains({"brass", "trumpet", "trombone", "tuba",
                                  "flugelhorn", "flugel", "euphonium",
                                  "frenchhorn", "horns"}))
                        rec.subMask |= Sub_Brass;
                    if (contains({"woodwind", "woodwinds", "wind", "winds",
                                  "sax", "saxophone", "clarinet", "oboe",
                                  "bassoon", "flute", "piccolo",
                                  "duduk", "recorder"}))
                        rec.subMask |= Sub_Woodwind;
                    break;
                case ClusterGroup::Guitar:
                    if (contains({"acoustic", "nylon", "steelstring"}))
                        rec.subMask |= Sub_AcousticGuitar;
                    if (contains({"electric", "strat", "telecaster",
                                  "lespaul"}))
                        rec.subMask |= Sub_ElectricGuitar;
                    if (contains({"processed", "fx", "granular",
                                  "reversed", "reverse", "resampled"}))
                        rec.subMask |= Sub_ProcessedGuitar;
                    break;
                case ClusterGroup::FX:
                    if (contains({"atmos", "atmosphere", "ambience", "ambient",
                                  "texture", "textural", "drone", "foley"}))
                        rec.subMask |= Sub_Atmos;
                    break;
                default: break;
            }
        }
    };
}
