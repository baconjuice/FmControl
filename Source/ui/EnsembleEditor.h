#pragma once

#include "EditorCommon.h"
#include "model/Session.h"

namespace ui
{
// Editor for the MIDIbox FM ensemble: for each of the four voices the stored
// patch it plays, its MIDI channel, key range, velocity mode, volume,
// transpose, unisono and the audio outputs of its operator pairs.
class EnsembleEditor : public EditorTabs
{
public:
    explicit EnsembleEditor (Session& s);
};
} // namespace ui
