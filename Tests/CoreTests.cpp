// Core tests: SysEx encode/decode, parameter tables, .syx files, and the
// official MIDIbox FM preset bank.

#include "fm/Drums.h"
#include "fm/Params.h"
#include "fm/Randomizer.h"
#include "fm/SyxFile.h"
#include "fm/SysEx.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>

using namespace fm;

static int failures = 0;
#define CHECK(cond)                                                                                 \
    do {                                                                                            \
        if (! (cond)) { ++failures; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

static std::vector<uint8_t> load (const char* name)
{
    std::ifstream f (std::string (FIXTURES_DIR) + "/" + name, std::ios::binary);
    return { std::istreambuf_iterator<char> (f), {} };
}

static void testMessages()
{
    CHECK ((sysex::ping (0) == sysex::Bytes { 0xf0, 0, 0, 0x7e, 0x49, 0, 0x0f, 0xf7 }));
    CHECK ((sysex::requestVoice (0, 2) == sysex::Bytes { 0xf0, 0, 0, 0x7e, 0x49, 0, 0x01, 0x0a, 0, 0, 0xf7 }));
    CHECK ((sysex::requestStored (1, 3, 5) == sysex::Bytes { 0xf0, 0, 0, 0x7e, 0x49, 1, 0x01, 0x00, 3, 5, 0xf7 }));
    CHECK ((sysex::parameterWrite (0, 1, 0x42, 0x55) == sysex::Bytes { 0xf0, 0, 0, 0x7e, 0x49, 0, 0x06, 0x09, 0, 0x42, 0x55, 0xf7 }));
    CHECK ((sysex::parameterWrite (0, 0, 0x85, 0x7f) == sysex::Bytes { 0xf0, 0, 0, 0x7e, 0x49, 0, 0x06, 0x08, 1, 0x05, 0x7f, 0xf7 }));
    CHECK ((sysex::storeVoice (0, 3) == sysex::Bytes { 0xf0, 0, 0, 0x7e, 0x49, 0, 0x0a, 0x03, 0xf7 }));

    auto p = Patch::makeInit();
    p.setName ("Hello");
    const auto w = sysex::writeVoice (0, 1, p);
    CHECK (w.size() == 268);
    const auto m = sysex::parse (w.data(), w.size());
    CHECK (m && m->kind == sysex::Message::Kind::PatchDump && m->checksumOk && m->patch == p);
    CHECK (m && m->isVoiceRam() && m->voice() == 1);
    CHECK (m && m->patch.name() == "Hello");

    const sysex::Bytes ack { 0xf0, 0, 0, 0x7e, 0x49, 0, 0x0f, 0xf7 };
    CHECK (sysex::parse (ack.data(), ack.size())->kind == sysex::Message::Kind::Ack);
    const sysex::Bytes err { 0xf0, 0, 0, 0x7e, 0x49, 0, 0x0e, 0x03, 0xf7 };
    auto e = sysex::parse (err.data(), err.size());
    CHECK (e && e->kind == sysex::Message::Kind::Error && e->errorCode == 3);
    const sysex::Bytes sid { 0xf0, 0, 0, 0x7e, 0x4b, 0, 0x0f, 0xf7 };
    CHECK (! sysex::parse (sid.data(), sid.size()));
}

static void testParams()
{
    const auto& t = voiceParams();
    auto p = Patch::makeInit();
    for (auto& d : t.all())
    {
        CHECK (d.addr >= 0x10 && d.addr < kPatchSize);
        for (int v : { 0, d.maxRaw() / 2, d.maxRaw() })
        {
            auto q = p;
            q.setRaw (d, v);
            CHECK (q.getRaw (d) == v);
            for (int a = 0; a < kPatchSize; ++a)
                if (a != d.addr) CHECK (q.byte (a) == p.byte (a));
        }
    }
    CHECK (t.get ("op3.attack").addr == 0x22);
    CHECK (t.get ("lfo2.rate").addr == 0x52);
    CHECK (t.get ("lfo1.wave").shift == 4);
    CHECK (t.get ("wt.31.p3").addr == 0xff);
    CHECK (std::string (ccName (0x3c)) == "Algorithm");
}

static void testPresets()
{
    const auto bytes = load ("midibox_fm_voices.syx");
    CHECK (bytes.size() == 34304);
    const auto r = syx::read (bytes.data(), bytes.size());
    CHECK (r.patches.size() == 128);
    CHECK (r.otherMessages == 0);

    std::map<std::string, int> outOfRange;
    std::map<int, int> finetunes;
    for (auto& e : r.patches)
    {
        CHECK (e.checksumOk);
        // Every byte the parameter tables cover is fully described by them.
        for (int a = 0x10; a < kPatchSize; ++a)
        {
            int covered = 0;
            for (auto& d : voiceParams().all())
                if (d.addr == a) covered |= ((1 << d.bits) - 1) << d.shift;
            if (covered != 0 && (e.patch.byte (a) & ~covered) != 0)
                ++outOfRange[std::to_string (a)];
        }
        ++finetunes[e.patch.getRaw (voiceParams().get ("finetune"))];
    }
    for (auto& [addr, n] : outOfRange)
        std::printf ("  note: byte 0x%02x has bits outside its parameters in %d presets\n", std::stoi (addr), n);
    std::printf ("  finetune values:");
    for (auto& [v, n] : finetunes) std::printf (" %d(x%d)", v, n);
    std::printf ("\n  first: %s, last: %s\n", r.patches.front().patch.name().c_str(), r.patches.back().patch.name().c_str());

    // Writing the bank back gives the same bytes.
    std::vector<syx::Entry> entries (r.patches.begin(), r.patches.end());
    CHECK (syx::write (entries) == bytes);
}

static void testRandomizer()
{
    std::mt19937 rng (1234);
    const auto& t = voiceParams();
    auto v = [&] (const Patch& p, const char* id) { return p.getRaw (t.get (id)); };

    for (int i = 0; i < 300; ++i)
    {
        Patch p = Patch::makeInit();
        randomize (p, "all", 1.0f, rng);
        CHECK (p.name().rfind ("Random Patch", 0) == 0);
        CHECK (v (p, "op1.mult") <= 7 && v (p, "op3.mult") <= 7);
        CHECK (v (p, "op2.mult") == 0 && v (p, "op4.mult") == 0);
        CHECK (v (p, "op1.volume") >= 48 && v (p, "op3.volume") >= 48);
        CHECK (v (p, "op2.volume") == 63 && v (p, "op4.volume") == 63);
        for (auto* s : { "op1.sustain", "op2.sustain", "op3.sustain", "op4.sustain" })
            CHECK (v (p, s) >= 8);
        CHECK (v (p, "lfo1.wave") <= 3 && v (p, "lfo2.wave") <= 3);
        for (auto* d : { "lfo1.op2", "lfo1.op4", "lfo2.op2", "lfo2.op4", "eg5.op2", "eg5.op4" })
            CHECK (v (p, d) == 64);
        const int rate = v (p, "wt.rate");
        CHECK (rate == 0 || rate >= 64);
        for (auto b : p.data)
            CHECK (b < 0x80);
    }

    // A section only touches its own parameters.
    Patch base = Patch::makeInit();
    Patch p = base;
    randomize (p, "lfo", 1.0f, rng);
    for (auto& d : t.all())
        if (d.id.rfind ("lfo", 0) != 0)
            CHECK (p.getRaw (d) == base.getRaw (d));

    // A small amount stays close to the original.
    p = base;
    randomize (p, "eg5", 0.1f, rng);
    for (auto* id : { "eg5.attack", "eg5.decay1", "eg5.release" })
        CHECK (std::abs (v (p, id) - v (base, id)) <= 13);
}

static void testDrums()
{
    const auto& t = drumParams();
    auto v = [&] (const Patch& p, const char* id) { return p.getRaw (t.get (id)); };

    // Messages.
    CHECK ((sysex::requestDrums (0, 2, 5) == sysex::Bytes { 0xf0, 0, 0, 0x7e, 0x49, 0, 0x01, 0x10, 2, 5, 0xf7 }));
    CHECK ((sysex::requestDrumsRam (1) == sysex::Bytes { 0xf0, 0, 0, 0x7e, 0x49, 1, 0x01, 0x18, 0, 0, 0xf7 }));
    CHECK ((sysex::drumParameterWrite (0, 0x50, 0x40) == sysex::Bytes { 0xf0, 0, 0, 0x7e, 0x49, 0, 0x06, 0x18, 0, 0x50, 0x40, 0xf7 }));
    {
        const auto m = sysex::writeDrumsRam (0, makeInitDrums());
        const auto parsed = sysex::parse (m.data(), m.size());
        CHECK (parsed && parsed->kind == sysex::Message::Kind::PatchDump && parsed->checksumOk);
        CHECK (parsed && parsed->isDrums() && parsed->isDrumsRam() && ! parsed->isVoiceRam());
        CHECK (parsed && parsed->patch == makeInitDrums());
    }

    // The firmware's 16 preset drum sets.
    const auto bytes = load ("midibox_fm_drumsets.syx");
    const auto r = syx::read (bytes.data(), bytes.size());
    CHECK (r.patches.size() == 16 && r.otherMessages == 0);
    for (size_t i = 0; i < r.patches.size(); ++i)
    {
        const auto& e = r.patches[i];
        CHECK (e.drums && ! e.drumsRam && e.checksumOk && e.bank == 0 && e.patchNumber == i);
        for (size_t a = kDrumDataSize; a < kPatchSize; ++a)
            CHECK (e.patch.data[a] == 0);
    }
    if (! r.patches.empty())
    {
        const auto& kit = r.patches.front().patch;
        CHECK (kit == makeInitDrums());
        CHECK (v (kit, "bd.freq") == 0x60 && v (kit, "bd.decr") == 0x48);
        CHECK (v (kit, "hh.freq") == 0x6d && v (kit, "tom.freq") == 0x70);
        CHECK (v (kit, "bdm.volume") == 0x28 && v (kit, "bdc.volume") == 0x3f);
        CHECK (v (kit, "sd.mult") == 4 && v (kit, "cym.mult") == 5);
        CHECK (v (kit, "sd.wave") == 2 && v (kit, "cym.wave") == 6);
        CHECK (v (kit, "bd.out1") == 1 && v (kit, "bd.out2") == 0 && v (kit, "hhsd.out2") == 1 && v (kit, "tomcym.out2") == 1);
        CHECK (drumNote (kit, "bd") == 36 && drumNote (kit, "sd") == 38 && drumNote (kit, "hhc") == 44
               && drumNote (kit, "hho") == 46 && drumNote (kit, "tom") == 40 && drumNote (kit, "cym") == 48);

        // Writing them back gives the same file.
        std::vector<syx::Entry> entries (r.patches.begin(), r.patches.end());
        CHECK (syx::write (entries) == bytes);
    }

    // Every parameter lies in the drum area and fields don't overlap.
    std::map<int, int> used;
    for (auto& d : t.all())
    {
        CHECK (d.addr < kDrumDataSize);
        const int mask = d.maxRaw() << d.shift;
        CHECK ((used[d.addr] & mask) == 0);
        used[d.addr] |= mask;
        CHECK (mask <= 0x7f);
    }

    // Random kits.
    std::mt19937 rng (99);
    const Patch base = makeInitDrums();
    for (int i = 0; i < 300; ++i)
    {
        Patch p = base;
        randomizeDrums (p, "all", 1.0f, rng);
        for (auto& d : t.all())
        {
            const bool keyOrOut = d.id.find (".key") != std::string::npos || d.id.find (".out") != std::string::npos;
            if (keyOrOut)
                CHECK (p.getRaw (d) == base.getRaw (d));
        }
        for (auto& inst : drumInstruments())
        {
            const std::string id = inst.id;
            CHECK (p.getRaw (t.get (id + ".attack")) <= 3);
            const int rel = p.getRaw (t.get (id + ".release"));
            CHECK (rel >= 3 && rel <= 12);
            if (id != "bdm")
                CHECK (p.getRaw (t.get (id + ".volume")) >= 40);
        }
        CHECK (v (p, "bdm.mult") <= 4 && v (p, "bdc.mult") <= 4);
        for (size_t a = kDrumDataSize; a < kPatchSize; ++a)
            CHECK (p.data[a] == 0);
    }
    // A section leaves the other instruments alone.
    Patch p = base;
    randomizeDrums (p, "bd", 1.0f, rng);
    for (auto& d : t.all())
        if (d.id.rfind ("bd", 0) != 0)
            CHECK (p.getRaw (d) == base.getRaw (d));
}

int main()
{
    testMessages();
    testParams();
    testPresets();
    testRandomizer();
    testDrums();
    std::printf (failures == 0 ? "All tests passed\n" : "%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
