#pragma once

// MIDIbox FM voice patch: the 256-byte image the firmware sends and receives
// in SysEx dumps (midibox_fm_sysex_implementation.txt):
//   0x00-0x0f  name (16 ASCII characters)
//   0x10-0x7f  voice parameters, at the address of their CC number
//              (midibox_fm_cc_voice.txt), all 7-bit values
//   0x80-0xff  wavetable: 32 steps of 4 bytes (mode, parameter 1-3)
// Plain C++ with no JUCE dependency, so it can be unit tested.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fm
{
constexpr int kPatchSize = 256;
constexpr int kNameLength = 16;
constexpr int kVoices = 4;     // the four instruments of the box
constexpr int kBanks = 8;
constexpr int kPatchesPerBank = 128;
constexpr int kWavetableAddr = 0x80;
constexpr int kWavetableSteps = 32;

struct ParamDef;

int getRawValue (const uint8_t* data, size_t size, const ParamDef& p);
std::vector<uint16_t> setRawValue (uint8_t* data, size_t size, const ParamDef& p, int raw);

class Patch
{
public:
    Patch() = default;
    static Patch makeInit();

    std::string name() const; // right-trimmed
    void setName (std::string_view newName);

    uint8_t byte (int addr) const { return data[(size_t) (addr & 0xff)]; }
    void setByte (int addr, uint8_t v) { data[(size_t) (addr & 0xff)] = (uint8_t) (v & 0x7f); }

    int getRaw (const ParamDef& p) const { return getRawValue (data.data(), data.size(), p); }
    std::vector<uint16_t> setRaw (const ParamDef& p, int raw) { return setRawValue (data.data(), data.size(), p, raw); }

    bool operator== (const Patch& o) const { return data == o.data; }
    bool operator!= (const Patch& o) const { return data != o.data; }

    std::array<uint8_t, kPatchSize> data {};
};
} // namespace fm
