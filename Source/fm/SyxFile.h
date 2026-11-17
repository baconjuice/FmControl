#pragma once

// Reading and writing .syx files with MIDIbox FM voice patch, drum set and ensemble dumps.

#include "SysEx.h"

#include <vector>

namespace fm::syx
{
struct Entry
{
    uint8_t bank = 0;
    uint8_t patchNumber = 0;
    bool voiceRam = false;   // dump was addressed to a voice, not a slot
    bool drums = false;      // a drum set (bank/patchNumber = bank/set)
    bool drumsRam = false;   // ... addressed to the set playing now
    bool ensemble = false;   // an ensemble (bank/patchNumber = bank/ensemble)
    bool ensembleRam = false;
    bool checksumOk = false;
    Patch patch;
};

struct ReadResult
{
    std::vector<Entry> patches;
    int otherMessages = 0;   // other devices / commands
};

ReadResult read (const uint8_t* data, size_t size);

// One stored-patch write per entry (bank/number as given).
sysex::Bytes write (const std::vector<Entry>& entries, uint8_t device = 0);
// A single patch addressed to voice 1's RAM, so sending it never overwrites a slot.
sysex::Bytes writeVoicePatch (const Patch& p, uint8_t device = 0);
// A single drum set addressed to the set playing now.
sysex::Bytes writeDrumSet (const Patch& p, uint8_t device = 0);
// A single ensemble addressed to the one playing now.
sysex::Bytes writeEnsembleSetup (const Patch& p, uint8_t device = 0);
} // namespace fm::syx
