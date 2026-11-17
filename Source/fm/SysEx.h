#pragma once

// MIDIbox FM SysEx protocol (midibox_fm_sysex_implementation.txt):
//   F0 00 00 7E 49 <device> <command> ... F7
// Builds outgoing messages and parses replies.

#include "Patch.h"

#include <optional>
#include <vector>

namespace fm::sysex
{
using Bytes = std::vector<uint8_t>;

namespace type
{
    // Voice patch types: 00-03 = stored patch (bank/patch given), 08-0B = the
    // patch voice 1-4 is playing right now (RAM).
    constexpr uint8_t Stored = 0x00, VoiceRam = 0x08, Drum = 0x10, DrumRam = 0x18, Ensemble = 0x70, EnsembleRam = 0x78;
}

uint8_t checksum (const uint8_t* data, size_t count); // 2s complement, 7 bit

Bytes requestStored (uint8_t device, uint8_t bank, uint8_t patch);
Bytes requestVoice (uint8_t device, int voice);  // voice 0..3
Bytes writeStored (uint8_t device, uint8_t bank, uint8_t patch, const Patch& p);
Bytes writeVoice (uint8_t device, int voice, const Patch& p);
Bytes parameterWrite (uint8_t device, int voice, uint8_t addr, uint8_t value);
Bytes storeVoice (uint8_t device, int voice);    // voice's RAM patch -> its current slot

// Drum sets (same dump format, see Drums.h). "Ram" = the set playing now.
Bytes requestDrums (uint8_t device, uint8_t bank, uint8_t set);
Bytes requestDrumsRam (uint8_t device);
Bytes writeDrums (uint8_t device, uint8_t bank, uint8_t set, const Patch& p);
Bytes writeDrumsRam (uint8_t device, const Patch& p);
Bytes drumParameterWrite (uint8_t device, uint8_t addr, uint8_t value);
Bytes ping (uint8_t device);

struct Message
{
    enum class Kind { PatchDump, Ack, Error, Other };

    Kind kind = Kind::Other;
    uint8_t device = 0;
    uint8_t command = 0;
    uint8_t type = 0;
    uint8_t bank = 0;
    uint8_t patchNumber = 0;
    uint8_t errorCode = 0;
    bool checksumOk = false;
    Patch patch;

    bool isVoiceRam() const { return (type & 0xfc) == type::VoiceRam; }
    bool isDrums() const { return (type & 0xf7) == type::Drum; }
    bool isDrumsRam() const { return type == type::DrumRam; }
    int voice() const { return type & 0x03; }
};

// Parses one complete SysEx message (F0..F7). Returns nullopt if it isn't
// a MIDIbox FM message.
std::optional<Message> parse (const uint8_t* data, size_t size);

const char* errorText (uint8_t code);
} // namespace fm::sysex
