#include "PadMidiRouting12.h"
#include <cassert>
#include <iostream>
using Atlas::PadMidiRouting12;
int main()
{
    // Six pads per row. The next octave must use the NEXT row,
    // rather than aliasing back to the same pad (former note % 12).
    for (int i = 0; i < 6; ++i)
    {
        assert(PadMidiRouting12::padForNote(60 + i) == i);
        assert(PadMidiRouting12::padForNote(72 + i) == i + 6);
        assert(PadMidiRouting12::padForNote(36 + i) == i);
        assert(PadMidiRouting12::padForNote(48 + i) == i + 6);
    }

    for (int midi = 0; midi <= 127; ++midi)
    {
        const int pad = PadMidiRouting12::padForNote(midi);
        assert(pad >= -1 && pad < 12);
        if (pad >= 0 && midi + 12 <= 127)
        {
            const int nextOctave = PadMidiRouting12::padForNote(midi + 12);
            assert(nextOctave >= 0 && nextOctave != pad);
        }
    }
    // The remaining six semitones in each octave are not pad triggers.
    for (int i = 6; i < 12; ++i)
    {
        assert(PadMidiRouting12::padForNote(60 + i) == -1);
        assert(PadMidiRouting12::padForNote(72 + i) == -1);
    }
    assert(PadMidiRouting12::padForNote(-1) == -1);
    assert(PadMidiRouting12::padForNote(128) == -1);
    std::cout << "12 pad octave routing PASS\n";
}
