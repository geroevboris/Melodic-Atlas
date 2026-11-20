#pragma once

namespace Atlas
{
    // Pad Trigger: six chromatic keys per row, separated by one octave.
    // C3..F3 (MIDI 60..65) -> top row, C4..F4 (72..77) -> bottom.
    // The two-octave pattern repeats to accommodate keyboard octave switches.
    // Unassigned notes never fall through to an unrelated pad.
    struct PadMidiRouting12
    {
        static int padForNote(int midiNote)
        {
            if (midiNote < 0 || midiNote > 127)
                return -1;
            const int pitch = ((midiNote - 60) % 24 + 24) % 24;
            if (pitch < 6)
                return pitch;
            if (pitch >= 12 && pitch < 18)
                return pitch - 6;
            return -1;
        }
    };
}
