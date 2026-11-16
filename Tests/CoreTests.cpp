// Core tests: SysEx encode/decode, parameter tables, .syx files, and the
// official MIDIbox FM preset bank.

#include "fm/Params.h"
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

int main()
{
    testMessages();
    testParams();
    testPresets();
    std::printf (failures == 0 ? "All tests passed\n" : "%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
