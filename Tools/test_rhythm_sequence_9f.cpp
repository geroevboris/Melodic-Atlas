#include "RhythmSequenceEvidence9F.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

static constexpr double sr = 44100.0;
static constexpr double pi = 3.14159265358979323846;
static std::vector<float> makeSequence(bool pianoNotes)
{
    std::vector<float> out((size_t)(sr * 4.0), 0.0f);
    for (int event = 0; event < 8; ++event)
    {
        const int start = (int)((0.075 + event * 0.48) * sr);
        const int length = (int)(0.22 * sr);
        const double frequency = pianoNotes
            ? (110.0 * std::pow(2.0, (double)(event % 5) / 12.0))
            : (event % 2 == 0 ? 72.0 : 180.0);
        for (int k = 0; k < length && start + k < (int)out.size(); ++k)
        {
            const double time = k / sr;
            const double amp = std::exp(-26.0 * time);
            out[(size_t)(start + k)] += (float)(
                amp * std::sin(2.0 * pi * frequency * time));
        }
    }
    return out;
}
int main()
{
    auto drums = makeSequence(false);
    auto d = Atlas::RhythmSequenceEvidence9F::analyse(
        drums.data(), (int)drums.size(), sr);
    assert(d.isSequence && d.onsetCount >= 6 && d.onsetSpanSeconds > 2.0f);

    // Musical arpeggio is also a rhythmically structured sequence,
    // but must NOT be classified as percussion by structural evidence.
    auto notes = makeSequence(true);
    auto n = Atlas::RhythmSequenceEvidence9F::analyse(
        notes.data(), (int)notes.size(), sr);
    assert(n.isSequence);

    std::vector<float> sustained((size_t)(sr * 4.0));
    for (size_t i = 0; i < sustained.size(); ++i)
        sustained[i] = 0.5f * (float)std::sin(
            2.0 * pi * 110.0 * i / sr);
    auto s = Atlas::RhythmSequenceEvidence9F::analyse(
        sustained.data(), (int)sustained.size(), sr);
    assert(!s.isSequence);

    for (size_t i = 0; i < sustained.size(); ++i)
        sustained[i] *= (float)std::exp(-4.0 * i / sr);
    auto single = Atlas::RhythmSequenceEvidence9F::analyse(
        sustained.data(), (int)sustained.size(), sr);
    assert(!single.isSequence);
    assert(!Atlas::RhythmSequenceEvidence9F::analyse(
        nullptr, 0, sr).isSequence);
    std::cout << "9F sequence tests passed\n";
}
