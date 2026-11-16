#pragma once

#include "EditorCommon.h"
#include "model/Session.h"

namespace ui
{
// Editor for a MIDIbox FM voice patch: operators and algorithm, LFOs and
// EG5, play settings and controller assignments, wavetable sequencer.
class VoiceEditor : public EditorTabs
{
public:
    explicit VoiceEditor (Session& s);
};
} // namespace ui
