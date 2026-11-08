#pragma once
#include "AtlasTypes.h"
#include <array>
#include <algorithm>
#include <cmath>

namespace Atlas
{
    // Independent, conservative source-identity evidence. Acoustic timbre
    // alone cannot reliably distinguish a sampled guitar from a synthesizer.
    // These are heuristic evidence weights, not calibrated probabilities.
    class SoundIdentityAnalyzer
    {
    public:
        static void evaluate(const juce::File& file, SampleRecord& rec)
        {
            rec.identityState = IdentityState::NotEvaluated;
            rec.identityCluster = ClusterGroup::Other;
            rec.identityConfidence = 0.0f;

            if (!rec.acoustic.valid && !rec.tonal.valid)
                return;

            std::array<float, 7> scores {};
            const juce::String filename = file.getFileNameWithoutExtension();
            const juce::File parent = file.getParentDirectory();

            // Name hints carry more evidence than folder names, which can
            // refer to an entire mixed sample pack instead of this sample.
            collectTokens(filename, 3.0f, scores);
            collectTokens(parent.getFileName(), 1.15f, scores);
            collectTokens(parent.getParentDirectory().getFileName(), 0.40f, scores);

            // Broad acoustics can support a bass hypothesis but are too
            // ambiguous to certify a specific acoustic instrument by sound.
            // Explicitly *do not* map spectral flatness to FX or transient
            // sharpness to Keys; either could be a guitar or synth.
            if (rec.acoustic.valid && rec.tonal.valid
                && rec.tonal.periodicity >= 0.66f
                && rec.acoustic.spectralCentroidHz > 0.0f
                && rec.acoustic.spectralCentroidHz < 400.0f)
            {
                scores[(size_t)ClusterGroup::Bass] += 0.35f;
            }

            int best = -1;
            float bestScore = 0.0f, runnerUp = 0.0f;
            for (int i = 0; i < 7; ++i)
            {
                const float score = scores[(size_t)i];
                if (score > bestScore)
                {
                    runnerUp = bestScore;
                    bestScore = score;
                    best = i;
                }
                else if (score > runnerUp)
                {
                    runnerUp = score;
                }
            }

            if (best < 0 || bestScore < 0.75f)
            {
                rec.identityState = IdentityState::Unidentified;
                return;
            }

            rec.identityCluster = (ClusterGroup)best;
            const float separation = bestScore - runnerUp;
            // Independent file-level tokens with no meaningful competition
            // may be considered identified, but a folder hint alone never is.
            const auto exactDirectory = parent.getFileName().trim().toLowerCase();
            // A folder with an exact, unambiguous instrument-family name is
            // stronger evidence than a generic sample-pack parent directory.
            int namedFolder = -1;
            if (exactDirectory == "brass" || exactDirectory == "woodwinds"
                || exactDirectory == "winds" || exactDirectory == "strings"
                || exactDirectory == "orchestra" || exactDirectory == "violins"
                || exactDirectory == "saxes")
                namedFolder = (int)ClusterGroup::Strings;
            else if (exactDirectory == "synth" || exactDirectory == "synths"
                     || exactDirectory == "synthesizers" || exactDirectory == "pads")
                namedFolder = (int)ClusterGroup::Synth;
            else if (exactDirectory == "keys" || exactDirectory == "keyboards"
                     || exactDirectory == "piano" || exactDirectory == "pianos"
                     || exactDirectory == "electric pianos")
                namedFolder = (int)ClusterGroup::Keys;
            else if (exactDirectory == "bass" || exactDirectory == "basses")
                namedFolder = (int)ClusterGroup::Bass;
            else if (exactDirectory == "guitar" || exactDirectory == "guitars")
                namedFolder = (int)ClusterGroup::Guitar;
            else if (exactDirectory == "vocal" || exactDirectory == "vocals"
                     || exactDirectory == "voices")
                namedFolder = (int)ClusterGroup::Vocal;
            else if (exactDirectory == "fx" || exactDirectory == "effects"
                     || exactDirectory == "sound effects")
                namedFolder = (int)ClusterGroup::FX;
            if (bestScore >= 2.8f && separation >= 2.0f)
            {
                rec.identityState = IdentityState::Identified;
                rec.identityConfidence = juce::jlimit(
                    0.0f, 0.92f, 0.78f + 0.025f * (separation - 2.0f));
            }
            else if (namedFolder == best && separation >= 0.8f)
            {
                rec.identityState = IdentityState::Identified;
                rec.identityConfidence = 0.74f;
            }
            else
            {
                rec.identityState = IdentityState::Ambiguous;
                rec.identityConfidence = juce::jlimit(
                    0.0f, 0.65f, 0.20f + 0.10f * separation);
            }
        }

    private:
        static int categoryForToken(const juce::String& token)
        {
            // Exact tokens, not substring matching: e.g. "ipad" should not
            // trigger a synth pad and "ambassador" must not become bass.
            if (token == "piano" || token == "pianos" || token == "rhodes" ||
                token == "wurli" || token == "wurlitzer" ||
                token == "organ" || token == "clav" ||
                token == "harpsichord" || token == "marimba" ||
                token == "kalimba" || token == "vibraphone" ||
                token == "xylophone" || token == "celesta" ||
                token == "glock" || token == "mallet" ||
                token == "keys" || token == "keyboards" || token == "epiano")
                return (int)ClusterGroup::Keys;

            if (token == "synth" || token == "synths" || token == "pads" ||
                token == "pad" || token == "lead" || token == "leads" ||
                token == "arp" || token == "arpeggio" ||
                token == "synthesizers" || token == "synthesizer" ||
                token == "synthesiser" || token == "juno" ||
                token == "serum" || token == "prophet" ||
                token == "modular" || token == "oscillator" ||
                token == "supersaw" || token == "analog" ||
                token == "analogue")
                return (int)ClusterGroup::Synth;

            if (token == "bass" || token == "basses" || token == "subbass" ||
                token == "reese" || token == "808" ||
                token == "contrabass")
                return (int)ClusterGroup::Bass;

            if (token == "vox" || token == "vocal" ||
                token == "vocals" || token == "voice" || token == "voices" ||
                token == "choir" || token == "chant" ||
                token == "acapella" || token == "acappella" ||
                token == "speech" || token == "singer")
                return (int)ClusterGroup::Vocal;

            if (token == "strings" || token == "violins" || token == "string" ||
                token == "violin" || token == "viola" ||
                token == "vln" || token == "fiddle" || token == "fiddles" ||
                token == "cello" || token == "orchestral" ||
                token == "orchestra" || token == "brass" ||
                token == "trumpet" || token == "trombone" ||
                token == "tuba" || token == "flugel" || token == "frenchhorn" ||
                token == "sax" || token == "saxophone" ||
                token == "flute" || token == "oboe" ||
                token == "clarinet" || token == "horn" ||
                token == "woodwind" || token == "woodwinds" ||
                token == "bassoon" || token == "piccolo" ||
                token == "recorder" || token == "duduk" ||
                token == "accordion" || token == "harmonica" ||
                token == "concertina" || token == "melodica" ||
                token == "bagpipe" ||
                token == "winds")
                return (int)ClusterGroup::Strings;

            if (token == "guitar" || token == "guitars" || token == "gtr" ||
                token == "ukulele" || token == "banjo" ||
                token == "mandolin" || token == "harp" ||
                token == "sitar" || token == "koto")
                return (int)ClusterGroup::Guitar;

            if (token == "fx" || token == "effects" || token == "sfx" ||
                token == "riser" || token == "uplifter" ||
                token == "downlifter" || token == "whoosh" ||
                token == "sweep" || token == "foley" ||
                token == "ambience" || token == "atmosphere")
                return (int)ClusterGroup::FX;

            return -1;
        }

        static void collectTokens(const juce::String& text, float weight,
                                  std::array<float, 7>& scores)
        {
            const juce::String lower = text.toLowerCase();
            juce::String token;
            for (int i = 0; i <= lower.length(); ++i)
            {
                const juce::juce_wchar c = i < lower.length()
                    ? lower[i] : (juce::juce_wchar)' ';
                if (juce::CharacterFunctions::isLetterOrDigit(c))
                {
                    token += juce::String::charToString(c);
                }
                else if (token.isNotEmpty())
                {
                    const int category = categoryForToken(token);
                    if (category >= 0)
                        scores[(size_t)category] += weight;
                    token.clear();
                }
            }
        }
    };
}
