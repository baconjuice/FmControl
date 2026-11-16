#pragma once

// Parameter definitions of an FM voice patch: where each value lives and how
// to show it. Built from midibox_fm_cc_voice.txt and the firmware sources.

#include "Patch.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace fm
{
struct ParamDef
{
    enum class Kind : uint8_t { Int, Bool, Choice };

    std::string id;    // stable key, e.g. "op1.attack"
    std::string name;  // short label
    uint16_t addr = 0;
    uint8_t shift = 0;
    uint8_t bits = 7;
    Kind kind = Kind::Int;
    int offset = 0;    // displayed value = raw - offset (64 for signed depths)
    std::vector<std::string> choices;

    int maxRaw() const { return (1 << bits) - 1; }
    int minDisplay() const { return -offset; }
    int maxDisplay() const { return maxRaw() - offset; }
    std::vector<uint16_t> addresses() const { return { addr }; }
};

class ParamTable
{
public:
    void add (ParamDef d);
    const ParamDef* find (const std::string& id) const;
    const ParamDef& get (const std::string& id) const; // throws if missing
    const std::vector<ParamDef>& all() const { return defs; }

private:
    std::vector<ParamDef> defs;
    std::unordered_map<std::string, size_t> lookup;
};

const ParamTable& voiceParams();

// Name of a voice CC / parameter number (for "assign to controller" menus).
const char* ccName (int cc);

const std::vector<std::string>& opWaveforms();   // the 8 OPL3 waveforms
const std::vector<std::string>& algorithms();    // the 4 operator connections
const std::vector<std::string>& lfoWaveforms();
const std::vector<std::string>& playModes();
const std::vector<std::string>& kslChoices();
const std::vector<std::string>& wtModes();
const std::vector<std::string>& wtCommands();   // -, Play, Goto, End
std::string noteName (int midiNote);
} // namespace fm
