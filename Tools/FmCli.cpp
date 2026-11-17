// fmcli: command-line diagnostics for a MIDIbox FM, using the same device
// code as the FmControl app.
//
//   fmcli list
//   fmcli ping   <in-port> <out-port> [device]
//   fmcli get    <in-port> <out-port> [voice 1-4] [device]   (read a voice's patch)
//   fmcli getslot <in-port> <out-port> <bank 0-7> <patch 1-128>
//   fmcli scan   [port-filter]                     (ping every in/out pairing)
//   fmcli monitor <in-port> [seconds]              (print everything received)
//   fmcli play   <out-port> [channel]              (note on/off; listen!)
//   fmcli resend <in-port> <out-port> [voice] [times] (read a voice, write it back unchanged)
//
// Port arguments match any part of the port name, case-insensitively.

#include "device/FmDevice.h"
#include "device/PacedSysEx.h"
#include "model/Session.h"
#include "fm/Params.h"

#include <juce_events/juce_events.h>

#include <iostream>

namespace
{
juce::MidiDeviceInfo findPort (const juce::Array<juce::MidiDeviceInfo>& ports, const juce::String& part)
{
    for (auto& p : ports)
        if (p.name.containsIgnoreCase (part))
            return p;
    return {};
}

void waitFor (const bool& done, int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (! done && juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
}

struct Printer : FmDevice::Listener
{
    void fmLog (const juce::String& s) override { std::cout << "  log: " << s << "\n"; }
};

void describe (const fm::Patch& p)
{
    const auto& t = fm::voiceParams();
    std::cout << "  patch \"" << p.name() << "\": algorithm " << fm::algorithms()[(size_t) p.getRaw (t.get ("algorithm"))]
              << ", feedback " << p.getRaw (t.get ("feedback"));
    for (int o = 1; o <= 4; ++o)
    {
        const auto op = "op" + std::to_string (o);
        std::cout << "\n    OP" << o << ": " << fm::opWaveforms()[(size_t) p.getRaw (t.get (op + ".wave"))]
                  << " x" << p.getRaw (t.get (op + ".mult")) << " vol " << p.getRaw (t.get (op + ".volume"))
                  << " ADSR " << p.getRaw (t.get (op + ".attack")) << "/" << p.getRaw (t.get (op + ".decay")) << "/"
                  << p.getRaw (t.get (op + ".sustain")) << "/" << p.getRaw (t.get (op + ".release"));
    }
    std::cout << "\n";
}

bool request (FmDevice& dev, const fm::sysex::Bytes& msg, FmDevice::Expect expect, bool quiet = false)
{
    bool done = false, ok = false;
    dev.enqueue (msg, expect, 1500, [&] (const FmDevice::Result& r)
    {
        done = true;
        ok = r.ok;
        if (quiet) return;
        if (! r.ok)
            std::cout << "  no: " << r.error << "\n";
        else if (r.reply != nullptr && r.reply->kind == fm::sysex::Message::Kind::Ack)
            std::cout << "  ack\n";
        else if (r.reply != nullptr && r.reply->kind == fm::sysex::Message::Kind::PatchDump)
            describe (r.reply->patch);
    });
    waitFor (done, 3000);
    return ok;
}

int usage()
{
    std::cout << "usage: fmcli list | ping <in> <out> [dev] | get <in> <out> [voice] [dev]\n"
                 "             | getslot <in> <out> <bank> <patch> | scan [filter] | monitor <in> [sec] | play <out> [ch]\n"
              << "             | resend <in> <out> [voice] [times] | parburst <in> <out> | rawwrite <in> <out> [chunk] [gap ms]\n";
    return 2;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (argv[i]);
    if (args.isEmpty()) return usage();

    const auto ins = juce::MidiInput::getAvailableDevices();
    const auto outs = juce::MidiOutput::getAvailableDevices();
    const auto cmd = args[0];

    if (cmd == "list")
    {
        std::cout << "MIDI inputs:\n";
        for (auto& p : ins) std::cout << "  " << p.name << "\n";
        std::cout << "MIDI outputs:\n";
        for (auto& p : outs) std::cout << "  " << p.name << "\n";
        return 0;
    }

    if (cmd == "monitor" && args.size() >= 2)
    {
        auto in = findPort (ins, args[1]);
        if (in.identifier.isEmpty()) { std::cout << "no input matching " << args[1] << "\n"; return 1; }
        struct Mon : juce::MidiInputCallback
        {
            void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override
            {
                std::cout << "  " << juce::String::toHexString (m.getRawData(), m.getRawDataSize()) << "\n" << std::flush;
            }
        } mon;
        auto port = juce::MidiInput::openDevice (in.identifier, &mon);
        port->start();
        const int secs = args.size() >= 3 ? args[2].getIntValue() : 15;
        std::cout << "Listening on " << in.name << " for " << secs << " s...\n" << std::flush;
        bool never = false;
        waitFor (never, secs * 1000);
        return 0;
    }

    if (cmd == "play" && args.size() >= 2)
    {
        auto out = findPort (outs, args[1]);
        if (out.identifier.isEmpty()) { std::cout << "no output matching " << args[1] << "\n"; return 1; }
        FmDevice dev;
        dev.openOutput (out.identifier);
        const int ch = args.size() >= 3 ? args[2].getIntValue() : 1;
        std::cout << "Playing C3 on channel " << ch << " of " << out.name << ". Listen...\n";
        dev.sendMidi (juce::MidiMessage::noteOn (ch, 60, (juce::uint8) 100));
        bool never = false;
        waitFor (never, 1200);
        dev.sendMidi (juce::MidiMessage::noteOff (ch, 60));
        waitFor (never, 200);
        return 0;
    }

    if (cmd == "scan")
    {
        const auto filter = args.size() >= 2 ? args[1] : juce::String();
        bool any = false;
        for (auto& o : outs)
        {
            if (filter.isNotEmpty() && ! o.name.containsIgnoreCase (filter)) continue;
            for (auto& i : ins)
            {
                if (filter.isNotEmpty() && ! i.name.containsIgnoreCase (filter)) continue;
                FmDevice dev;
                if (! dev.openOutput (o.identifier) || ! dev.openInput (i.identifier)) continue;
                for (int d = 0; d < 2; ++d)
                {
                    dev.setDeviceNumber (d);
                    if (request (dev, fm::sysex::ping ((uint8_t) d), FmDevice::Expect::Ack, true))
                    {
                        std::cout << "FOUND: out \"" << o.name << "\" -> in \"" << i.name << "\", device " << d << "\n";
                        any = true;
                    }
                }
            }
        }
        if (! any) std::cout << "No MIDIbox FM answered on any port pairing.\n";
        return any ? 0 : 1;
    }

    if (cmd == "rawwrite" && args.size() >= 3)
    {
        // Read voice 1 through FmDevice, then write it back with a raw port,
        // optionally in chunks, printing every byte that comes back.
        auto in = findPort (ins, args[1]);
        auto out = findPort (outs, args[2]);
        const int chunk = args.size() >= 4 ? args[3].getIntValue() : 0;
        const int gapMs = args.size() >= 5 ? args[4].getIntValue() : 0;
        fm::Patch patch;
        {
            FmDevice dev;
            dev.openInput (in.identifier);
            dev.openOutput (out.identifier);
            bool done = false, got = false;
            dev.enqueue (fm::sysex::requestVoice (0, 0), FmDevice::Expect::Dump, 1500, [&] (const FmDevice::Result& r)
            {
                done = true; got = r.ok && r.reply != nullptr;
                if (got) patch = r.reply->patch;
            });
            waitFor (done, 3000);
            if (! got) { std::cout << "read failed\n"; return 1; }
        }
        struct Raw : juce::MidiInputCallback
        {
            void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override
            {
                if (m.isActiveSense()) return;
                juce::String hex;
                for (int i = 0; i < juce::jmin (16, m.getRawDataSize()); ++i)
                    hex << juce::String::toHexString (m.getRawData()[i]).paddedLeft ('0', 2) << " ";
                std::cout << "  in (" << m.getRawDataSize() << " bytes): " << hex << "\n";
            }
        } raw;
        auto mi = juce::MidiInput::openDevice (in.identifier, &raw);
        auto mo = juce::MidiOutput::openDevice (out.identifier);
        mi->start();
        const auto msg = fm::sysex::writeVoice (0, 0, patch);
        std::cout << "  sending " << msg.size() << " bytes, chunk " << chunk << ", gap " << gapMs << " ms\n";
        if (chunk < 0)
        {
            // small message: rewrite the algorithm with its current value
            const auto& t = fm::voiceParams();
            const auto alg = (uint8_t) patch.getRaw (t.get ("algorithm"));
            const auto pw = fm::sysex::parameterWrite (0, 0, 0x3c, alg);
            std::cout << "  instead sending parameter write (" << pw.size() << " bytes)\n";
            mo->sendMessageNow (juce::MidiMessage (pw.data(), (int) pw.size()));
        }
        else if (chunk == 0)
            mo->sendMessageNow (juce::MidiMessage (msg.data(), (int) msg.size()));
        else
        {
            PacedSysEx paced;
            if (! paced.open (out.identifier.toStdString())) { std::cout << "  paced open failed\n"; return 1; }
            paced.send (msg, chunk, gapMs);
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (2000);
        mi->stop();
        return 0;
    }

    if (cmd == "fetchbank" && args.size() >= 4)
    {
        // Fetch a bank through the app's Session (first fetching bank A so the
        // list starts full), and report how many slots end up filled.
        auto in = findPort (ins, args[1]);
        auto out = findPort (outs, args[2]);
        FmDevice dev;
        dev.openInput (in.identifier);
        dev.openOutput (out.identifier);
        Session session (dev);
        auto runFetch = [&] (int bank)
        {
            session.setBankNumber (bank);
            session.fetchBank();
            const auto end = juce::Time::getMillisecondCounter() + 200000;
            while (session.isBusy() && juce::Time::getMillisecondCounter() < end)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            int filled = 0;
            for (int i = 0; i < 128; ++i)
                if (session.slot (i).has_value()) ++filled;
            std::cout << "  bank " << (char) ('A' + bank) << ": " << session.getStatus() << " -> " << filled << " slots filled\n";
        };
        runFetch (0);
        runFetch (args[3].getIntValue());
        return 0;
    }

    if (cmd == "voice1check" && args.size() >= 3)
    {
        // Voice Fetch Bank through the Session must leave voice 1 and its
        // ensemble bank/patch as they were.
        auto in = findPort (ins, args[1]);
        auto out = findPort (outs, args[2]);
        FmDevice dev;
        dev.openInput (in.identifier);
        dev.openOutput (out.identifier);
        auto readBoth = [&]
        {
            std::pair<fm::Patch, fm::Patch> r;
            bool done = false;
            dev.enqueue (fm::sysex::requestVoice (0, 0), FmDevice::Expect::Dump, 2500,
                         [&] (const FmDevice::Result& x) { if (x.reply) r.first = x.reply->patch; });
            dev.enqueue (fm::sysex::requestEnsembleRam (0), FmDevice::Expect::Dump, 2500,
                         [&] (const FmDevice::Result& x) { if (x.reply) r.second = x.reply->patch; done = true; });
            const auto end = juce::Time::getMillisecondCounter() + 8000;
            while (! done && juce::Time::getMillisecondCounter() < end)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            return r;
        };
        const auto before = readBoth();
        std::cout << "before: voice 1 = " << before.first.name() << ", ensemble v1 = "
                  << Session::slotName (fm::ensembleBank (before.second, 0), fm::ensemblePatch (before.second, 0)) << "\n";
        {
            Session session (dev);
            session.setLive (false);
            session.setBankNumber (args.size() >= 4 ? args[3].getIntValue() : 0);
            session.fetchBank();
            auto end = juce::Time::getMillisecondCounter() + 200000;
            while (session.isBusy() && juce::Time::getMillisecondCounter() < end)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            std::cout << "  " << session.getStatus() << "\n";
            end = juce::Time::getMillisecondCounter() + 5000;
            while (juce::Time::getMillisecondCounter() < end)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        }
        const auto after = readBoth();
        std::cout << "after:  voice 1 = " << after.first.name() << ", ensemble v1 = "
                  << Session::slotName (fm::ensembleBank (after.second, 0), fm::ensemblePatch (after.second, 0)) << "\n";
        // The wavetable (0x80-0xff) of a voice RAM read comes from a buffer the
        // voices share on the box, so only name and parameters are compared.
        bool same = before.second == after.second;
        for (int i = 0; i < fm::kWavetableAddr; ++i)
            same = same && before.first.data[(size_t) i] == after.first.data[(size_t) i];
        for (int i = 0; i < fm::kPatchSize; ++i)
        {
            if (i < fm::kWavetableAddr && before.first.data[(size_t) i] != after.first.data[(size_t) i])
                std::cout << "  voice byte " << i << ": " << (int) before.first.data[(size_t) i] << " -> " << (int) after.first.data[(size_t) i] << "\n";
            if (before.second.data[(size_t) i] != after.second.data[(size_t) i])
                std::cout << "  ensemble byte " << i << ": " << (int) before.second.data[(size_t) i] << " -> " << (int) after.second.data[(size_t) i] << "\n";
        }
        std::cout << (same ? "voice 1 and ensemble unchanged\n" : "CHANGED\n");
        return same ? 0 : 1;
    }

    if (cmd == "randomsend" && args.size() >= 3)
    {
        // Through the app's Session: read voice 4, roll "All", send it (waits
        // for the ack), then send the original back.
        auto in = findPort (ins, args[1]);
        auto out = findPort (outs, args[2]);
        FmDevice dev;
        dev.openInput (in.identifier);
        dev.openOutput (out.identifier);
        Session session (dev);
        auto waitStatus = [&] (const juce::String& prefix)
        {
            const auto end = juce::Time::getMillisecondCounter() + 8000;
            while (! session.getStatus().startsWith (prefix) && ! session.getStatus().containsIgnoreCase ("fail")
                   && juce::Time::getMillisecondCounter() < end)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            std::cout << "  " << session.getStatus() << "\n";
            return session.getStatus().startsWith (prefix);
        };
        session.setEditVoice (3);
        if (! waitStatus ("Voice 4")) { std::cout << "  could not read voice 4, not touching it\n"; return 1; }
        const auto original = session.edit();
        std::cout << "  voice 4 was \"" << original.name() << "\"\n";
        session.randomize ("all", 1.0f);
        const bool ok = waitStatus ("Sent patch");
        session.loadIntoEditor (original);
        const bool back = waitStatus ("Sent patch");
        return ok && back ? 0 : 1;
    }

    if (cmd == "parburst" && args.size() >= 3)
    {
        // Rewrite every parameter of voice 1 with its current value as live
        // edits (like a fast knob sweep), then check the FM still answers.
        auto in = findPort (ins, args[1]);
        auto out = findPort (outs, args[2]);
        FmDevice dev;
        dev.openInput (in.identifier);
        dev.openOutput (out.identifier);
        fm::Patch patch;
        bool done = false, got = false;
        dev.enqueue (fm::sysex::requestVoice (0, 0), FmDevice::Expect::Dump, 1500, [&] (const FmDevice::Result& r)
        { done = true; got = r.ok && r.reply != nullptr; if (got) patch = r.reply->patch; });
        waitFor (done, 3000);
        if (! got) { std::cout << "read failed\n"; return 1; }
        for (int a = 0x10; a < 0x80; ++a)
            dev.queueParameter (0, (uint16_t) a, patch.data[(size_t) a]);
        const auto t0 = juce::Time::getMillisecondCounter();
        bool ok = request (dev, fm::sysex::requestVoice (0, 0), FmDevice::Expect::Dump, true);
        std::cout << "  112 parameter writes then read-back: " << (ok ? "ok" : "FAILED") << " after "
                  << (juce::Time::getMillisecondCounter() - t0) << " ms\n";
        return ok ? 0 : 1;
    }

    if (cmd == "resend" && args.size() >= 3)
    {
        auto in = findPort (ins, args[1]);
        auto out = findPort (outs, args[2]);
        if (in.identifier.isEmpty() || out.identifier.isEmpty()) { std::cout << "port not found\n"; return 1; }
        FmDevice dev;
        Printer printer;
        dev.addListener (&printer);
        dev.openInput (in.identifier);
        dev.openOutput (out.identifier);
        const int voice = args.size() >= 4 ? args[3].getIntValue() - 1 : 0;
        const int times = args.size() >= 5 ? args[4].getIntValue() : 1;

        fm::Patch patch;
        bool done = false, got = false;
        dev.enqueue (fm::sysex::requestVoice (0, voice), FmDevice::Expect::Dump, 1500, [&] (const FmDevice::Result& r)
        {
            done = true;
            got = r.ok && r.reply != nullptr;
            if (got) patch = r.reply->patch;
            else std::cout << "  read failed: " << r.error << "\n";
        });
        waitFor (done, 3000);
        if (! got) return 1;
        std::cout << "  read \"" << patch.name() << "\"\n";

        int okCount = 0;
        for (int i = 0; i < times; ++i)
        {
            bool d = false;
            const auto t0 = juce::Time::getMillisecondCounter();
            dev.enqueue (fm::sysex::writeVoice (0, voice, patch), FmDevice::Expect::Ack, 1500, [&] (const FmDevice::Result& r)
            {
                d = true;
                std::cout << "  write " << (i + 1) << ": " << (r.ok ? "ack" : r.error.toStdString())
                          << " after " << (juce::Time::getMillisecondCounter() - t0) << " ms\n";
                if (r.ok) ++okCount;
            });
            waitFor (d, 3000);
        }
        dev.removeListener (&printer);
        std::cout << okCount << "/" << times << " writes acknowledged\n";
        return okCount == times ? 0 : 1;
    }

    if ((cmd == "ping" || cmd == "get" || cmd == "getslot") && args.size() >= 3)
    {
        auto in = findPort (ins, args[1]);
        auto out = findPort (outs, args[2]);
        if (in.identifier.isEmpty()) { std::cout << "no input matching " << args[1] << "\n"; return 1; }
        if (out.identifier.isEmpty()) { std::cout << "no output matching " << args[2] << "\n"; return 1; }

        FmDevice dev;
        Printer printer;
        dev.addListener (&printer);
        dev.openInput (in.identifier);
        dev.openOutput (out.identifier);

        bool ok = false;
        if (cmd == "ping")
        {
            dev.setDeviceNumber (args.size() >= 4 ? args[3].getIntValue() : 0);
            ok = request (dev, fm::sysex::ping (dev.getDeviceNumber()), FmDevice::Expect::Ack);
        }
        else if (cmd == "get")
        {
            dev.setDeviceNumber (args.size() >= 5 ? args[4].getIntValue() : 0);
            const int voice = args.size() >= 4 ? args[3].getIntValue() - 1 : 0;
            ok = request (dev, fm::sysex::requestVoice (dev.getDeviceNumber(), voice), FmDevice::Expect::Dump);
        }
        else if (args.size() >= 5)
        {
            ok = request (dev, fm::sysex::requestStored (0, (uint8_t) args[3].getIntValue(), (uint8_t) (args[4].getIntValue() - 1)),
                          FmDevice::Expect::Dump);
        }
        dev.removeListener (&printer);
        return ok ? 0 : 1;
    }

    return usage();
}
