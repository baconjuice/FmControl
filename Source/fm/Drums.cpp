#include "Drums.h"

namespace fm
{
const std::array<DrumInstrument, 7>& drumInstruments()
{
    static const std::array<DrumInstrument, 7> v { {
        { "bdm", "BD Mod", "Bass Drum Modulator" },
        { "bdc", "BD Car", "Bass Drum Carrier" },
        { "hho", "HH Open", "Open Hi-Hat" },
        { "hhc", "HH Closed", "Closed Hi-Hat" },
        { "sd", "Snare", "Snare Drum" },
        { "tom", "Tom", "Tom" },
        { "cym", "Cymbal", "Cymbal" },
    } };
    return v;
}

const std::array<DrumKey, 6>& drumKeys()
{
    static const std::array<DrumKey, 6> v { {
        { "bd", "Bass Drum" }, { "hho", "Open Hi-Hat" }, { "hhc", "Closed Hi-Hat" },
        { "sd", "Snare" },     { "tom", "Tom" },         { "cym", "Cymbal" },
    } };
    return v;
}

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

ParamTable buildDrums()
{
    ParamTable t;
    const auto& inst = drumInstruments();

    // Operator rows: 0x00 + n, 0x08 + n, ... for the seven instruments.
    for (int n = 0; n < 7; ++n)
    {
        const std::string p = inst[(size_t) n].id;
        t.add (make (p + ".mult", "Multiplier", 0x00 + n, 4));
        // The modulator's "volume" is how much it modulates the carrier.
        t.add (make (p + ".volume", n == 0 ? "Mod Level" : "Volume", 0x08 + n, 6));
        t.add (make (p + ".attack", "Attack", 0x10 + n, 4));
        t.add (make (p + ".decay", "Decay", 0x18 + n, 4));
        t.add (make (p + ".sustain", "Sustain", 0x20 + n, 4));
        t.add (make (p + ".release", "Release", 0x28 + n, 4));
        auto w = make (p + ".wave", "Waveform", 0x30 + n, 3);
        w.kind = ParamDef::Kind::Choice;
        w.choices = opWaveforms();
        t.add (std::move (w));
    }

    // OPL3 channels 7-9 in rhythm mode: BD, HH+SD, Tom+Cymbal.
    t.add (make ("bd.feedback", "Feedback", 0x38, 3));
    auto am = make ("bd.am", "Mode", 0x39, 1);
    am.kind = ParamDef::Kind::Choice;
    am.choices = { "FM", "AM" };
    t.add (std::move (am));
    t.add (make ("hhsd.feedback", "Feedback", 0x3a, 3));
    t.add (make ("tomcym.feedback", "Feedback", 0x3c, 3));

    // Key ranges: a note between lower and upper plays the drum.
    const auto& keys = drumKeys();
    for (int k = 0; k < 6; ++k)
    {
        const std::string p = keys[(size_t) k].id;
        t.add (make (p + ".keylo", "Lowest Key", 0x41 + k, 7));
        t.add (make (p + ".keyhi", "Highest Key", 0x49 + k, 7));
    }

    t.add (make ("bd.freq", "Pitch", 0x50, 7));
    t.add (make ("bd.decr", "Pitch Drop", 0x51, 7));
    t.add (make ("hh.freq", "Pitch", 0x52, 7));
    t.add (make ("hh.decr", "Pitch Drop", 0x53, 7));
    t.add (make ("tom.freq", "Pitch", 0x54, 7));
    t.add (make ("tom.decr", "Pitch Drop", 0x55, 7));

    // Audio outputs (bits 0-3 = channel 1-4) per OPL3 channel.
    for (auto [p, addr] : { std::pair { "bd", 0x58 }, { "hhsd", 0x5a }, { "tomcym", 0x5c } })
        for (int o = 0; o < 4; ++o)
        {
            auto d = make (std::string (p) + ".out" + std::to_string (o + 1), "Out " + std::to_string (o + 1), addr, 1, o);
            d.kind = ParamDef::Kind::Bool;
            t.add (std::move (d));
        }
    return t;
}

// Drum set 1 of the MIDIbox FM preset bank (midibox_fm_drumsets.syx).
constexpr uint8_t kInitDrums[kDrumDataSize] = {
    0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x05, 0x00, 0x28, 0x3f, 0x37, 0x3f, 0x3f, 0x3f, 0x3f, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0a, 0x0f, 0x0f, 0x0f, 0x0b, 0x0f, 0x00,
    0x04, 0x08, 0x0f, 0x0f, 0x0f, 0x08, 0x07, 0x00, 0x07, 0x08, 0x09, 0x07, 0x07, 0x0a, 0x07, 0x00,
    0x00, 0x00, 0x04, 0x04, 0x02, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x24, 0x2e, 0x2c, 0x26, 0x28, 0x30, 0x00, 0x00, 0x24, 0x2e, 0x2c, 0x26, 0x28, 0x30, 0x00,
    0x60, 0x48, 0x6d, 0x00, 0x70, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00,
};
} // namespace

const ParamTable& drumParams()
{
    static const ParamTable t = buildDrums();
    return t;
}

Patch makeInitDrums()
{
    Patch p;
    for (int i = 0; i < kDrumDataSize; ++i)
        p.data[(size_t) i] = kInitDrums[i];
    return p;
}

int drumNote (const Patch& drums, const std::string& keyId)
{
    const auto& t = drumParams();
    const auto* lo = t.find (keyId + ".keylo");
    const auto* hi = t.find (keyId + ".keyhi");
    if (lo == nullptr || hi == nullptr)
        return -1;
    const int l = drums.getRaw (*lo), h = drums.getRaw (*hi);
    return l <= h ? l : -1;
}
} // namespace fm
