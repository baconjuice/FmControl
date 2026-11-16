#include "Params.h"

#include <stdexcept>

namespace fm
{
void ParamTable::add (ParamDef d)
{
    lookup[d.id] = defs.size();
    defs.push_back (std::move (d));
}

const ParamDef* ParamTable::find (const std::string& id) const
{
    auto it = lookup.find (id);
    return it == lookup.end() ? nullptr : &defs[it->second];
}

const ParamDef& ParamTable::get (const std::string& id) const
{
    if (auto* d = find (id))
        return *d;
    throw std::out_of_range ("unknown FM parameter " + id);
}

const std::vector<std::string>& opWaveforms()
{
    static const std::vector<std::string> v { "Sine", "Half Sine", "Abs Sine", "Quarter Sine",
                                              "Alt. Sine", "Camel Sine", "Square", "Log Saw" };
    return v;
}

const std::vector<std::string>& algorithms()
{
    static const std::vector<std::string> v { "1*2*3*4", "1 + 2*3*4", "1*2 + 3*4", "1 + 2*3 + 4" };
    return v;
}

const std::vector<std::string>& lfoWaveforms()
{
    static const std::vector<std::string> v { "Sine", "Triangle", "Saw", "Pulse", "Random", "(5)", "(6)", "(7)" };
    return v;
}

const std::vector<std::string>& playModes()
{
    static const std::vector<std::string> v { "Mono", "Legato", "Poly", "(3)" };
    return v;
}

const std::vector<std::string>& kslChoices()
{
    // OPL KSL register order.
    static const std::vector<std::string> v { "Off", "3 dB/oct", "1.5 dB/oct", "6 dB/oct" };
    return v;
}

const std::vector<std::string>& wtModes()
{
    static const std::vector<std::string> v { "Normal", "Step per Note", "Free Running", "(3)" };
    return v;
}

const std::vector<std::string>& wtCommands()
{
    static const std::vector<std::string> v { "-", "Play", "Goto", "End" };
    return v;
}

std::string noteName (int n)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return std::string (names[n % 12]) + std::to_string (n / 12 - 2);
}

namespace
{
struct Builder
{
    ParamTable& t;

    void num (std::string id, std::string name, int addr, int bits = 7, int offset = 0)
    {
        ParamDef d;
        d.id = std::move (id); d.name = std::move (name);
        d.addr = (uint16_t) addr; d.bits = (uint8_t) bits; d.offset = offset;
        t.add (std::move (d));
    }
    void depth (std::string id, std::string name, int addr) { num (std::move (id), std::move (name), addr, 7, 64); }
    void flag (std::string id, std::string name, int addr, int bit)
    {
        ParamDef d;
        d.id = std::move (id); d.name = std::move (name);
        d.addr = (uint16_t) addr; d.shift = (uint8_t) bit; d.bits = 1; d.kind = ParamDef::Kind::Bool;
        t.add (std::move (d));
    }
    void choice (std::string id, std::string name, int addr, int bits, int shift, std::vector<std::string> c)
    {
        ParamDef d;
        d.id = std::move (id); d.name = std::move (name);
        d.addr = (uint16_t) addr; d.bits = (uint8_t) bits; d.shift = (uint8_t) shift;
        d.kind = ParamDef::Kind::Choice;
        c.resize ((size_t) 1 << bits);
        for (size_t i = 0; i < c.size(); ++i)
            if (c[i].empty()) c[i] = "(" + std::to_string (i) + ")";
        d.choices = std::move (c);
        t.add (std::move (d));
    }
    // "Assign to controller": 0 = off, else a voice CC number.
    void assign (std::string id, std::string name, int addr)
    {
        std::vector<std::string> c { "Off" };
        for (int i = 1; i < 128; ++i)
        {
            const std::string n = ccName (i);
            c.push_back (std::to_string (i) + ": " + (n.empty() ? "--" : n));
        }
        choice (std::move (id), std::move (name), addr, 7, 0, std::move (c));
    }
};

ParamTable buildVoice()
{
    ParamTable t;
    Builder b { t };

    for (int i = 0; i < 4; ++i)
    {
        const std::string o = "op" + std::to_string (i + 1);
        b.flag (o + ".ksr", "KSR", 0x10 + i, 0);
        b.flag (o + ".egt", "Sustain", 0x10 + i, 1);
        b.flag (o + ".vib", "Vibrato", 0x10 + i, 2);
        b.flag (o + ".trem", "Tremolo", 0x10 + i, 3);
        b.num (o + ".mult", "Multiplier", 0x14 + i, 4);
        b.choice (o + ".ksl", "Key Scale", 0x18 + i, 2, 0, kslChoices());
        b.num (o + ".volume", "Volume", 0x1c + i, 6);
        b.num (o + ".attack", "Attack", 0x20 + i, 4);
        b.num (o + ".decay", "Decay", 0x24 + i, 4);
        b.num (o + ".sustain", "Sustain", 0x28 + i, 4);
        b.num (o + ".release", "Release", 0x2c + i, 4);
        b.choice (o + ".wave", "Waveform", 0x30 + i, 3, 0, opWaveforms());
    }
    b.num ("feedback", "Feedback", 0x38, 3);
    b.choice ("algorithm", "Algorithm", 0x3c, 2, 0, algorithms());

    for (int l = 0; l < 2; ++l)
    {
        const std::string p = "lfo" + std::to_string (l + 1);
        const int base = 0x40 + l * 0x10;
        b.flag (p + ".on", "On", base, 0);
        b.flag (p + ".sync", "Note Sync", base, 2);
        b.choice (p + ".wave", "Waveform", base, 3, 4, lfoWaveforms());
        b.num (p + ".phase", "Phase", base + 1);
        b.num (p + ".rate", "Rate", base + 2);
        b.depth (p + ".pitch", "Pitch", base + 3);
        for (int o = 0; o < 4; ++o)
            b.depth (p + ".op" + std::to_string (o + 1), "OP" + std::to_string (o + 1) + " Vol", base + 4 + o);
        b.depth (p + (l == 0 ? ".lfo2" : ".lfo1"), l == 0 ? "LFO2 Rate" : "LFO1 Rate", base + 8);
        b.depth (p + ".aout", "AOUT", base + 9);
    }

    b.assign ("vel.assign", "Velocity To", 0x4c);
    b.num ("vel.init", "Init", 0x4d);
    b.depth ("vel.depth", "Depth", 0x4e);
    b.assign ("at.assign", "Aftertouch To", 0x5c);
    b.num ("at.init", "Init", 0x5d);
    b.depth ("at.depth", "Depth", 0x5e);
    b.assign ("mw.assign", "Modwheel To", 0x7c);
    b.num ("mw.init", "Init", 0x7d);
    b.depth ("mw.depth", "Depth", 0x7e);

    b.flag ("eg5.curvea", "Curve Atk", 0x60, 0);
    b.flag ("eg5.curved1", "Curve Dec1", 0x60, 1);
    b.flag ("eg5.curved2", "Curve Dec2", 0x60, 2);
    b.flag ("eg5.curver", "Curve Rel", 0x60, 3);
    b.num ("eg5.attack", "Attack", 0x61);
    b.num ("eg5.attacklvl", "Atk Level", 0x62);
    b.num ("eg5.decay1", "Decay 1", 0x63);
    b.num ("eg5.decaylvl", "Dec Level", 0x64);
    b.num ("eg5.decay2", "Decay 2", 0x65);
    b.num ("eg5.sustain", "Sustain", 0x66);
    b.num ("eg5.release", "Release", 0x67);
    b.depth ("eg5.curve", "Curve", 0x68);
    b.depth ("eg5.pitch", "Pitch", 0x69);
    for (int o = 0; o < 4; ++o)
        b.depth ("eg5.op" + std::to_string (o + 1), "OP" + std::to_string (o + 1) + " Vol", 0x6a + o);
    b.depth ("eg5.lfo1", "LFO1 Rate", 0x6e);
    b.depth ("eg5.aout", "AOUT", 0x6f);

    b.choice ("play.mode", "Play Mode", 0x70, 2, 0, playModes());
    b.flag ("play.suskey", "SusKey", 0x70, 2);
    b.flag ("sync.wt", "Sync WT", 0x72, 0);
    b.flag ("sync.lfo", "Sync LFOs", 0x72, 1);
    b.flag ("sync.env", "Sync EG5", 0x72, 2);
    b.choice ("wt.mode", "WT Mode", 0x73, 2, 0, wtModes());
    b.depth ("finetune", "Finetune", 0x74);
    b.num ("pitchrange", "Bend Range", 0x75);
    b.num ("porta", "Portamento", 0x76);
    b.num ("wt.rate", "WT Rate", 0x77);
    b.assign ("wt.par1", "WT Par 1", 0x78);
    b.assign ("wt.par2", "WT Par 2", 0x79);
    b.assign ("wt.par3", "WT Par 3", 0x7a);

    // Wavetable steps.
    for (int s = 0; s < kWavetableSteps; ++s)
    {
        const std::string p = "wt." + std::to_string (s);
        const int base = kWavetableAddr + s * 4;
        b.choice (p + ".cmd", "Command", base, 2, 0, wtCommands());
        for (int k = 1; k <= 3; ++k)
        {
            b.flag (p + ".abs" + std::to_string (k), "Abs " + std::to_string (k), base, 3 + k);
            b.num (p + ".p" + std::to_string (k), "Par " + std::to_string (k), base + k);
        }
    }
    return t;
}
} // namespace

const ParamTable& voiceParams()
{
    static const ParamTable t = buildVoice();
    return t;
}
} // namespace fm
