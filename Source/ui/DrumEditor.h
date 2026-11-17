#pragma once

#include "EditorCommon.h"
#include "model/Session.h"

namespace ui
{
// Editor for the MIDIbox FM drum set: the seven OPL3 rhythm instruments
// (operator settings and envelopes), the three rhythm channels (pitch,
// pitch drop, feedback, audio outputs) and the key range of each drum.
class DrumEditor : public EditorTabs
{
public:
    explicit DrumEditor (Session& s);
};
} // namespace ui
