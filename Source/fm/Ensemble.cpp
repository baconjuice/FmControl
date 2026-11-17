#include "Ensemble.h"

namespace fm
{
namespace
{
ParamDef make (std::string id, std::string name, int addr, int bits, int shift = 0)
{
    ParamDef d;
    d.id = std::move (id);
    d.name = std::move (name);
    d.addr = (uint16_t) addr;
    d.bits = (uint8_t) bits;
    d.shift = (uint8_t) shift;
    return d;
}

ParamTable build()
{
    ParamTable t;
    for (int v = 0; v < kVoices; ++v)
    {
        const int b = v * kEnsembleVoiceStride;
        auto id = [v] (const char* p) { return ensembleId (v, p); };

        auto bank = make (id ("bank"), "Bank", b + 0x00, 3);
        bank.kind = ParamDef::Kind::Choice;
        bank.choices = { "A", "B", "C", "D", "E", "F", "G", "H" };
        t.add (std::move (bank));
        t.add (make (id ("patch"), "Patch", b + 0x01, 7));
        t.add (make (id ("channel"), "MIDI Channel", b + 0x02, 4));
        t.add (make (id ("keylo"), "Lowest Key", b + 0x03, 7));
        t.add (make (id ("keyhi"), "Highest Key", b + 0x04, 7));
        auto vel = make (id ("velocity"), "Velocity", b + 0x05, 2);
        vel.kind = ParamDef::Kind::Choice;
        vel.choices = { "Flat", "Hard", "Soft" };
        t.add (std::move (vel));
        t.add (make (id ("volume"), "Volume", b + 0x06, 7));
        auto tr = make (id ("transpose"), "Transpose", b + 0x07, 7);
        tr.offset = 64;
        t.add (std::move (tr));
        t.add (make (id ("unisono"), "Unisono", b + 0x08, 7));
        // Audio outputs (bits 0-3 = channel 1-4) of operators 1+2 and 3+4.
        for (auto [p, addr] : { std::pair { "op12", 0x09 }, { "op34", 0x0a } })
            for (int o = 0; o < 4; ++o)
            {
                auto d = make (id (p) + ".out" + std::to_string (o + 1), "Out " + std::to_string (o + 1), b + addr, 1, o);
                d.kind = ParamDef::Kind::Bool;
                t.add (std::move (d));
            }
    }
    return t;
}
} // namespace

std::string ensembleId (int voice, const char* param)
{
    return "v" + std::to_string ((voice & 3) + 1) + "." + param;
}

const ParamTable& ensembleParams()
{
    static const ParamTable t = build();
    return t;
}

Patch makeInitEnsemble()
{
    Patch e;
    constexpr uint8_t patches[kVoices] = { 4, 36, 83, 98 };
    for (int v = 0; v < kVoices; ++v)
    {
        const int b = v * kEnsembleVoiceStride;
        const uint8_t row[] = { 0, patches[v], (uint8_t) v, 0, 127, 0, 127, 64, 0, 15, 15 };
        for (int i = 0; i < (int) sizeof (row); ++i)
            e.data[(size_t) (b + i)] = row[i];
    }
    return e;
}

int ensembleBank (const Patch& e, int voice) { return e.getRaw (ensembleParams().get (ensembleId (voice, "bank"))); }
int ensemblePatch (const Patch& e, int voice) { return e.getRaw (ensembleParams().get (ensembleId (voice, "patch"))); }
int ensembleChannel (const Patch& e, int voice) { return e.getRaw (ensembleParams().get (ensembleId (voice, "channel"))) + 1; }
} // namespace fm
