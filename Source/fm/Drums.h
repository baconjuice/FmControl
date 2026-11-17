#pragma once

// MIDIbox FM drum set: the OPL3 rhythm section (bass drum, open and closed
// hi-hat, snare, tom, cymbal). A drum set travels in the same 256-byte dump
// as a voice patch (type 10 = stored set, 18 = the set playing now), with
// the parameters at the address of their number in midibox_fm_cc_drums.txt
// (0x00-0x5f, all 7-bit values) and the rest always 0. Drum sets have no
// name. The box plays them on MIDI channel 16 (firmware default), and a
// Program Change on that channel picks one: program = bank * 16 + set.

#include "Params.h"

#include <array>
#include <string>

namespace fm
{
constexpr int kDrumSetsPerBank = 16;
constexpr int kDrumPart = kVoices;   // "voice" number used for the drum set in edit/live code
constexpr int kDrumChannel = 16;
constexpr int kDrumDataSize = 0x60;  // bytes the box keeps; 0x60-0xff are always 0

// The seven OPL3 rhythm instruments, in the order of the parameter table.
struct DrumInstrument
{
    const char* id;    // parameter id prefix: "bdm", "bdc", "hho", "hhc", "sd", "tom", "cym"
    const char* name;  // "BD Mod", ...
    const char* longName;
};
const std::array<DrumInstrument, 7>& drumInstruments();

// The six playable drums (the bass drum's two operators play together),
// with the parameter id prefix of their key range: "bd", "hho", ...
struct DrumKey
{
    const char* id;
    const char* name;
};
const std::array<DrumKey, 6>& drumKeys();

const ParamTable& drumParams();

// Init kit: the first preset drum set of the MIDIbox FM ((c) Thorsten Klose).
Patch makeInitDrums();

// Lowest note that plays the drum `keyId` ("bd", "sd", ...) in this set, or
// -1 if its key range is empty.
int drumNote (const Patch& drums, const std::string& keyId);
} // namespace fm
