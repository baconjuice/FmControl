#include "Session.h"
#include "MidiLearn.h"
#include "fm/Randomizer.h"

#include <cstring>

using namespace fm;

namespace
{
constexpr int kDumpTimeoutMs = 2500;
constexpr int kWriteTimeoutMs = 4000; // BankStick (EEPROM) writes are slow
constexpr int kPlayNote = 60;
} // namespace

juce::String Session::slotName (int bank, int slot, bool drums)
{
    return juce::String::charToString ((juce::juce_wchar) ('A' + bank)) + juce::String (slot + 1).paddedLeft ('0', drums ? 2 : 3);
}

// Plays one bar of 16ths on the drum channel: kick, snare on 2 and 4,
// closed hi-hat 8ths with an open one at the end, a crash on the one and a
// tom fill. Each note uses the lowest key of that drum's range.
struct Session::BeatPlayer : private juce::Timer
{
    explicit BeatPlayer (Session& s) : session (s) {}
    ~BeatPlayer() override { stop(); }

    void start()
    {
        stop();
        step = 0;
        timerCallback();
        startTimer (125); // 16ths at 120 bpm
    }

    void stop()
    {
        stopTimer();
        releaseNotes();
    }

private:
    void releaseNotes()
    {
        for (int n : held)
            session.device.sendMidi (juce::MidiMessage::noteOff (fm::kDrumChannel, n));
        held.clear();
    }

    void timerCallback() override
    {
        releaseNotes();
        if (step >= 16)
        {
            stopTimer();
            return;
        }
        auto hit = [this] (const char* key, int velocity)
        {
            const int n = fm::drumNote (session.drumKit, key);
            if (n < 0) return;
            session.device.sendMidi (juce::MidiMessage::noteOn (fm::kDrumChannel, n, (juce::uint8) velocity));
            held.push_back (n);
        };
        const int s = step++;
        if (s == 0 || s == 7 || s == 8 || s == 10) hit ("bd", 110);
        if (s == 4 || s == 12) hit ("sd", 105);
        if (s % 2 == 0 && s != 14) hit ("hhc", s % 4 == 0 ? 90 : 70);
        if (s == 14) hit ("hho", 90);
        if (s == 0) hit ("cym", 100);
        if (s == 13 || s == 15) hit ("tom", 95);
    }

    Session& session;
    int step = 0;
    std::vector<int> held;
};

uint8_t DrumTarget::getByte (int addr) const { return session.drums().byte (addr); }
int DrumTarget::getParam (const ParamDef& p) const { return session.drums().getRaw (p); }
void DrumTarget::setParam (const ParamDef& p, int raw, uint8_t) { session.setDrumParam (p, raw); }
int DrumTarget::defaultRaw (const ParamDef& p) const { return makeInitDrums().getRaw (p); }

Session::Session (FmDevice& d) : device (d)
{
    for (auto& v : voices)
        v = Patch::makeInit();
    device.addListener (this);
    midiLearn = std::make_unique<MidiLearn> (*this);
    updateKeyboardChannel();
}

Session::~Session()
{
    beat.reset();
    device.removeListener (this);
}

void Session::fireReplaced()
{
    listeners.call ([] (Listener& l) { l.editReplaced(); });
    notifyReplaced();
}

void Session::fireDrumsReplaced()
{
    listeners.call ([] (Listener& l) { l.editReplaced(); });
    drumTarget.replaced();
}

juce::String Session::partName() const
{
    return drumMode ? juce::String ("drum set") : "voice " + juce::String (editVoice + 1);
}

void Session::fireChanged (const std::vector<uint16_t>& addresses)
{
    listeners.call ([&] (Listener& l) { l.editChanged (addresses); });
    notifyChanged (addresses);
}

// ---------------------------------------------------------------------------
// Voices / edit buffer
// ---------------------------------------------------------------------------
void Session::setEditVoice (int v)
{
    v = juce::jlimit (0, kVoices - 1, v);
    if (v == editVoice && ! drumMode)
        return;
    if (drumMode)
    {
        drumMode = false;
        listeners.call ([] (Listener& l) { l.bankChanged(); });
    }
    editVoice = v;
    updateKeyboardChannel();
    fireReplaced();
    if (! voiceKnown[(size_t) v] && device.isOutputOpen() && device.isInputOpen())
        fetchEditBuffer();
    else
        setStatus ("Editing voice " + juce::String (v + 1) + ": " + juce::String (edit().name()));
}

void Session::setDrumMode (bool editDrums)
{
    if (editDrums == drumMode)
        return;
    if (! editDrums)
    {
        setEditVoice (editVoice);
        return;
    }
    drumMode = true;
    updateKeyboardChannel();
    fireDrumsReplaced();
    listeners.call ([] (Listener& l) { l.bankChanged(); });
    if (! drumKnown && device.isOutputOpen() && device.isInputOpen())
        fetchDrums();
    else
        setStatus ("Editing the drum set (MIDI channel " + juce::String (kDrumChannel) + ")");
}

void Session::setDrumParam (const ParamDef& p, int raw)
{
    auto changed = drumKit.setRaw (p, raw);
    if (changed.empty())
        return;
    if (live)
        for (auto addr : changed)
            device.queueParameter (kDrumPart, addr, drumKit.byte (addr));
    drumTarget.changed (changed);
}

void Session::playDrumBeat()
{
    if (beat == nullptr)
        beat = std::make_unique<BeatPlayer> (*this);
    beat->start();
}

void Session::setKeysFollowVoice (bool f)
{
    followVoice = f;
    updateKeyboardChannel();
}

void Session::updateKeyboardChannel()
{
    if (followVoice)
        device.setKeyboardChannel (drumMode ? kDrumChannel : voiceChannel (editVoice));
}

void Session::setParam (const ParamDef& p, int raw, uint8_t)
{
    auto changed = editRef().setRaw (p, raw);
    if (changed.empty())
        return;
    if (live)
        for (auto addr : changed)
            device.queueParameter (editVoice, addr, edit().byte (addr));
    fireChanged (changed);
}

void Session::setVoiceParam (int voice, const ParamDef& p, int raw)
{
    voice &= 3;
    if (voice == editVoice)
    {
        setParam (p, raw);
        return;
    }
    auto& patch = voices[(size_t) voice];
    const auto changed = patch.setRaw (p, raw);
    if (live)
        for (auto addr : changed)
            device.queueParameter (voice, addr, patch.byte (addr));
}

void Session::setName (const juce::String& name)
{
    const auto before = edit();
    editRef().setName (name.toStdString());
    std::vector<uint16_t> changed;
    for (int i = 0; i < kNameLength; ++i)
        if (before.byte (i) != edit().byte (i))
        {
            changed.push_back ((uint16_t) i);
            if (live)
                device.queueParameter (editVoice, (uint16_t) i, edit().byte (i));
        }
    if (! changed.empty())
        fireChanged (changed);
}

void Session::loadIntoEditor (const Patch& p, int fromSlot)
{
    if (drumMode)
    {
        drumKit = p;
        drumSourceSlot = fromSlot;
        fireDrumsReplaced();
        if (live)
            sendEditBuffer();
        return;
    }
    editRef() = p;
    sourceSlot[(size_t) editVoice] = fromSlot;
    fireReplaced();
    if (live)
        sendEditBuffer();
}

void Session::randomize (const std::string& section, float amount)
{
    auto& history = historyRef();
    history.push_back (current());
    if (history.size() > 100)
        history.erase (history.begin());
    if (drumMode)
    {
        fm::randomizeDrums (drumKit, section, amount, rng);
        drumSourceSlot = -1;
        fireDrumsReplaced();
        sendEditBufferSoon();
        return;
    }
    fm::randomize (editRef(), section, amount, rng);
    sourceSlot[(size_t) editVoice] = -1;
    fireReplaced();
    sendEditBufferSoon();
}

void Session::undoRandom()
{
    auto& history = historyRef();
    if (history.empty())
        return;
    currentRef() = history.back();
    history.pop_back();
    if (drumMode)
        fireDrumsReplaced();
    else
        fireReplaced();
    sendEditBufferSoon();
    setStatus ("Undid random change");
}

void Session::sendEditBufferSoon()
{
    if (! live)
        return;
    const int gen = ++sendGeneration;
    juce::WeakReference<Session> self (this);
    juce::Timer::callAfterDelay (150, [self, gen]
                                 {
                                     if (auto* s = self.get(); s != nullptr && s->sendGeneration == gen)
                                         s->sendEditBuffer();
                                 });
}

void Session::sendEditBuffer()
{
    if (! device.isOutputOpen())
    {
        setStatus ("Select a MIDI output first");
        return;
    }
    if (drumMode)
    {
        sendDrums();
        return;
    }
    device.clearPendingParameters();
    juce::WeakReference<Session> self (this);
    const int v = editVoice;
    device.enqueue (sysex::writeVoice (device.getDeviceNumber(), v, edit()), FmDevice::Expect::Ack, kDumpTimeoutMs,
                    [self, v] (const FmDevice::Result& r)
                    {
                        auto* s = self.get();
                        if (s == nullptr) return;
                        if (r.ok) s->voiceKnown[(size_t) v] = true;
                        s->setStatus (r.ok ? "Sent patch to voice " + juce::String (v + 1) : "Send failed: " + r.error);
                    });
}

void Session::sendDrums()
{
    device.clearPendingParameters();
    juce::WeakReference<Session> self (this);
    device.enqueue (sysex::writeDrumsRam (device.getDeviceNumber(), drumKit), FmDevice::Expect::Ack, kDumpTimeoutMs,
                    [self] (const FmDevice::Result& r)
                    {
                        auto* s = self.get();
                        if (s == nullptr) return;
                        if (r.ok) s->drumKnown = true;
                        s->setStatus (r.ok ? "Sent drum set" : "Send failed: " + r.error);
                    });
}

void Session::fetchDrums()
{
    juce::WeakReference<Session> self (this);
    setStatus ("Requesting the drum set...");
    device.enqueue (sysex::requestDrumsRam (device.getDeviceNumber()), FmDevice::Expect::Dump, kDumpTimeoutMs,
                    [self] (const FmDevice::Result& r)
                    {
                        auto* s = self.get();
                        if (s == nullptr) return;
                        if (r.ok && r.reply != nullptr && r.reply->isDrums())
                        {
                            s->drumKit = r.reply->patch;
                            s->drumKnown = true;
                            s->drumSourceSlot = -1;
                            if (s->drumMode)
                                s->fireDrumsReplaced();
                            s->setStatus ("Received the drum set the MIDIbox FM is playing");
                        }
                        else
                        {
                            s->setStatus ("Fetch failed: " + (r.error.isNotEmpty() ? r.error : juce::String ("no drum set in the reply")));
                        }
                    });
}

void Session::fetchEditBuffer()
{
    if (drumMode)
    {
        fetchDrums();
        return;
    }
    juce::WeakReference<Session> self (this);
    const int v = editVoice;
    setStatus ("Requesting voice " + juce::String (v + 1) + "...");
    device.enqueue (sysex::requestVoice (device.getDeviceNumber(), v), FmDevice::Expect::Dump, kDumpTimeoutMs,
                    [self, v] (const FmDevice::Result& r)
                    {
                        auto* s = self.get();
                        if (s == nullptr) return;
                        if (r.ok && r.reply != nullptr)
                        {
                            s->voices[(size_t) v] = r.reply->patch;
                            s->voiceKnown[(size_t) v] = true;
                            s->sourceSlot[(size_t) v] = -1;
                            if (v == s->editVoice)
                                s->fireReplaced();
                            s->setStatus ("Voice " + juce::String (v + 1) + ": " + juce::String (r.reply->patch.name()));
                        }
                        else
                        {
                            s->setStatus ("Fetch failed: " + r.error);
                        }
                    });
}

void Session::storeVoiceOnBox()
{
    juce::WeakReference<Session> self (this);
    const int v = editVoice;
    device.enqueue (sysex::storeVoice (device.getDeviceNumber(), v), FmDevice::Expect::Ack, kWriteTimeoutMs,
                    [self, v] (const FmDevice::Result& r)
                    {
                        if (auto* s = self.get())
                            s->setStatus (r.ok ? "Voice " + juce::String (v + 1) + " stored on the MIDIbox FM"
                                               : "Store failed: " + r.error);
                    });
}

void Session::playNote (bool on)
{
    if (drumMode)
    {
        if (on)
            playDrumBeat();
        return;
    }
    if (on)
    {
        playingNoteChannel = voiceChannel (editVoice);
        device.sendMidi (juce::MidiMessage::noteOn (playingNoteChannel, kPlayNote, (juce::uint8) 100));
    }
    else if (playingNoteChannel > 0)
    {
        device.sendMidi (juce::MidiMessage::noteOff (playingNoteChannel, kPlayNote));
        playingNoteChannel = 0;
    }
}

void Session::allNotesOff()
{
    for (int ch = 1; ch <= 16; ++ch)
    {
        device.sendMidi (juce::MidiMessage::allNotesOff (ch));
        device.sendMidi (juce::MidiMessage::controllerEvent (ch, 64, 0)); // sustain pedal up
    }
    playingNoteChannel = 0;
    if (beat != nullptr)
        beat->stop();
}

void Session::ping()
{
    juce::WeakReference<Session> self (this);
    device.enqueue (sysex::ping (device.getDeviceNumber()), FmDevice::Expect::Ack, 1000,
                    [self] (const FmDevice::Result& r)
                    {
                        auto* s = self.get();
                        if (s == nullptr) return;
                        if (r.ok)
                            s->setStatus ("MIDIbox FM answered");
                        else if (! s->device.isOutputOpen() || ! s->device.isInputOpen())
                            s->setStatus ("Select both MIDI In and MIDI Out first");
                        else
                            s->setStatus ("No answer from MIDIbox FM. Check: MIDI Out goes to the FM's MIDI IN "
                                          "and the FM's MIDI OUT comes back to MIDI In, the ports and device number match, "
                                          "and the FM is running.");
                    });
}

// ---------------------------------------------------------------------------
// Bank
// ---------------------------------------------------------------------------
void Session::setSlot (int i, std::optional<Patch> p)
{
    auto& list = slotsFor (drumMode);
    if (i < 0 || i >= (int) list.size()) return;
    list[(size_t) i] = std::move (p);
    listeners.call ([] (Listener& l) { l.bankChanged(); });
}

void Session::setBankNumber (int b)
{
    (drumMode ? drumBankNumber : bankNumber) = juce::jlimit (0, kBanks - 1, b);
    listeners.call ([] (Listener& l) { l.bankChanged(); });
}

sysex::Bytes Session::requestSlot (bool d, int bank, int i) const
{
    return d ? sysex::requestDrums (device.getDeviceNumber(), (uint8_t) bank, (uint8_t) i)
             : sysex::requestStored (device.getDeviceNumber(), (uint8_t) bank, (uint8_t) i);
}

sysex::Bytes Session::writeSlot (bool d, int bank, int i, const Patch& p) const
{
    return d ? sysex::writeDrums (device.getDeviceNumber(), (uint8_t) bank, (uint8_t) i, p)
             : sysex::writeStored (device.getDeviceNumber(), (uint8_t) bank, (uint8_t) i, p);
}

namespace
{
juce::String describe (const Patch& p, bool drums) { return drums ? juce::String ("drum set") : juce::String (p.name()); }
}

void Session::fetchSlot (int i, bool thenEdit)
{
    juce::WeakReference<Session> self (this);
    const bool d = drumMode;
    const int bank = getBankNumber();
    setStatus ("Requesting " + slotName (bank, i, d) + "...");
    device.enqueue (requestSlot (d, bank, i), FmDevice::Expect::Dump, kDumpTimeoutMs,
                    [self, i, bank, d, thenEdit] (const FmDevice::Result& r)
                    {
                        auto* s = self.get();
                        if (s == nullptr) return;
                        if (! r.ok || r.reply == nullptr || r.reply->isDrums() != d)
                        {
                            s->setStatus ("Fetch " + slotName (bank, i, d) + " failed: " + r.error);
                            return;
                        }
                        s->slotsFor (d)[(size_t) i] = r.reply->patch;
                        s->listeners.call ([] (Listener& l) { l.bankChanged(); });
                        s->setStatus ("Received " + slotName (bank, i, d) + ": " + describe (r.reply->patch, d));
                        if (thenEdit && s->drumMode == d)
                            s->loadIntoEditor (r.reply->patch, i);
                    });
}

void Session::fetchBank()
{
    if (busy) return;
    if (! device.isOutputOpen() || ! device.isInputOpen())
    {
        setStatus ("Select MIDI input and output first");
        return;
    }
    busy = true;
    cancelRequested = false;
    fetchNext (drumMode, 0, slotCount(), 0, 0);
}

void Session::fetchNext (bool d, int i, int endExclusive, int got, int failed)
{
    const int bank = d ? drumBankNumber : bankNumber;
    const juce::String what = d ? "drum sets" : "patches";
    if (cancelRequested || i >= endExclusive)
    {
        busy = false;
        setStatus (juce::String (cancelRequested ? "Cancelled: " : "Bank fetched: ") + juce::String (got) + " " + what
                   + (failed > 0 ? ", " + juce::String (failed) + " not available" : juce::String()));
        return;
    }

    setStatus ("Fetching " + slotName (bank, i, d) + "...", (float) i / (float) endExclusive);
    juce::WeakReference<Session> self (this);
    device.enqueue (requestSlot (d, bank, i), FmDevice::Expect::Dump, kDumpTimeoutMs,
                    [self, d, i, endExclusive, got, failed] (const FmDevice::Result& r)
                    {
                        auto* s = self.get();
                        if (s == nullptr) return;
                        auto& list = s->slotsFor (d);
                        if (r.ok && r.reply != nullptr)
                        {
                            list[(size_t) i] = r.reply->patch;
                            s->listeners.call ([] (Listener& l) { l.bankChanged(); });
                            s->fetchNext (d, i + 1, endExclusive, got + 1, failed);
                        }
                        else if (r.reply != nullptr)
                        {
                            // The box said "not available": don't leave the previous
                            // fetch's patch in this slot.
                            if (i == 0 && r.reply->kind == sysex::Message::Kind::Error && r.reply->errorCode == 0x0a)
                            {
                                // No BankStick for this bank at all.
                                for (auto& slot : list)
                                    slot.reset();
                                s->listeners.call ([] (Listener& l) { l.bankChanged(); });
                                s->busy = false;
                                const int b = d ? s->drumBankNumber : s->bankNumber;
                                s->setStatus ("Bank " + juce::String::charToString ((juce::juce_wchar) ('A' + b))
                                              + " is not available on the box (no BankStick for it)");
                                return;
                            }
                            list[(size_t) i].reset();
                            s->listeners.call ([] (Listener& l) { l.bankChanged(); });
                            s->fetchNext (d, i + 1, endExclusive, got, failed + 1);
                        }
                        else
                        {
                            s->busy = false;
                            s->setStatus ("Bank fetch stopped at " + slotName (d ? s->drumBankNumber : s->bankNumber, i, d)
                                          + ": " + r.error);
                        }
                    });
}

void Session::storeEditToSlot (int i)
{
    if (i < 0 || i >= slotCount()) return;
    juce::WeakReference<Session> self (this);
    const bool d = drumMode;
    const int bank = getBankNumber();
    const int v = editVoice;
    const Patch p = current();
    setStatus ("Storing to " + slotName (bank, i, d) + "...");
    device.enqueue (writeSlot (d, bank, i, p), FmDevice::Expect::Ack, kWriteTimeoutMs,
                    [self, i, bank, p, v, d] (const FmDevice::Result& r)
                    {
                        auto* s = self.get();
                        if (s == nullptr) return;
                        if (r.ok)
                        {
                            (d ? s->drumSourceSlot : s->sourceSlot[(size_t) v]) = i;
                            s->slotsFor (d)[(size_t) i] = p;
                            s->listeners.call ([] (Listener& l) { l.bankChanged(); });
                            s->setStatus ("Stored " + slotName (bank, i, d) + ": " + describe (p, d));
                        }
                        else
                        {
                            s->setStatus ("Store failed: " + r.error);
                        }
                    });
}

void Session::writeBankToDevice()
{
    if (busy) return;
    const bool d = drumMode;
    const auto& list = slotsFor (d);
    std::vector<int> todo;
    for (int i = (int) list.size() - 1; i >= 0; --i)
        if (list[(size_t) i]) todo.push_back (i);
    if (todo.empty())
    {
        setStatus ("Bank is empty");
        return;
    }
    busy = true;
    cancelRequested = false;
    const int total = (int) todo.size();
    writeNext (d, std::move (todo), 0, 0, total);
}

void Session::writeNext (bool d, std::vector<int> remaining, int done, int failed, int total)
{
    const int bank = d ? drumBankNumber : bankNumber;
    if (cancelRequested || remaining.empty())
    {
        busy = false;
        setStatus (juce::String (cancelRequested ? "Cancelled: " : "Bank written: ") + juce::String (done)
                   + (d ? " drum sets" : " patches") + (failed > 0 ? ", " + juce::String (failed) + " failed" : juce::String()));
        return;
    }
    const int i = remaining.back();
    remaining.pop_back();
    setStatus ("Writing " + slotName (bank, i, d) + "...", (float) done / (float) total);

    juce::WeakReference<Session> self (this);
    device.enqueue (writeSlot (d, bank, i, *slotsFor (d)[(size_t) i]), FmDevice::Expect::Ack, kWriteTimeoutMs,
                    [self, d, remaining, done, failed, total] (const FmDevice::Result& r) mutable
                    {
                        auto* s = self.get();
                        if (s == nullptr) return;
                        if (! r.ok && r.reply == nullptr)
                        {
                            s->busy = false;
                            s->setStatus ("Bank write stopped: " + r.error);
                            return;
                        }
                        s->writeNext (d, std::move (remaining), done + (r.ok ? 1 : 0), failed + (r.ok ? 0 : 1), total);
                    });
}

void Session::cancelTransfers()
{
    cancelRequested = true;
    device.cancelAll();
    busy = false;
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------
bool Session::loadSyx (const juce::File& f, juce::String& message)
{
    juce::MemoryBlock mb;
    if (! f.loadFileAsData (mb))
    {
        message = "Could not read " + f.getFileName();
        return false;
    }
    auto r = syx::read (static_cast<const uint8_t*> (mb.getData()), mb.getSize());
    if (r.patches.empty())
    {
        message = f.getFileName() + " contains no MIDIbox FM voice patches or drum sets";
        return false;
    }

    int bad = 0, voiceCount = 0, drumCount = 0;
    for (auto& e : r.patches)
    {
        if (! e.checksumOk) ++bad;
        (e.drums ? drumCount : voiceCount)++;
    }

    // A file of drum sets switches to drum mode, one of voice patches back
    // to the voices (a mixed file fills both lists and stays where it is).
    // (Marked as known first, so switching doesn't fetch from the box and
    // overwrite what we load.)
    if (drumCount > 0 && voiceCount == 0)
    {
        drumKnown = drumKnown || r.patches.size() == 1;
        setDrumMode (true);
    }
    else if (voiceCount > 0 && drumCount == 0 && drumMode)
    {
        if (r.patches.size() == 1)
            voiceKnown[(size_t) editVoice] = true;
        setEditVoice (editVoice);
    }

    if (r.patches.size() == 1)
    {
        const auto& e = r.patches.front();
        loadIntoEditor (e.patch);
        message = e.drums ? juce::String ("Loaded drum set") : "Loaded patch " + juce::String (e.patch.name());
    }
    else
    {
        for (auto& e : r.patches)
        {
            auto& list = slotsFor (e.drums);
            const size_t i = (size_t) (e.patchNumber & 0x7f);
            if (i < list.size())
                list[i] = e.patch;
        }
        listeners.call ([] (Listener& l) { l.bankChanged(); });
        message = "Loaded";
        if (voiceCount > 0) message << " " << voiceCount << " patches";
        if (voiceCount > 0 && drumCount > 0) message << " and";
        if (drumCount > 0) message << " " << drumCount << " drum sets";
        message << " from " << f.getFileName();
    }
    if (bad > 0)
        message << " (" << bad << " with bad checksum)";
    return true;
}

bool Session::saveBankSyx (const juce::File& f, juce::String& message) const
{
    const bool d = drumMode;
    const auto& list = slotsFor (d);
    std::vector<syx::Entry> entries;
    for (int i = 0; i < (int) list.size(); ++i)
    {
        if (! list[(size_t) i]) continue;
        syx::Entry e;
        e.bank = (uint8_t) getBankNumber();
        e.patchNumber = (uint8_t) i;
        e.drums = d;
        e.patch = *list[(size_t) i];
        entries.push_back (e);
    }
    if (entries.empty())
    {
        message = "Bank is empty";
        return false;
    }
    auto bytes = syx::write (entries);
    if (! f.replaceWithData (bytes.data(), bytes.size()))
    {
        message = "Could not write " + f.getFileName();
        return false;
    }
    message = "Saved " + juce::String ((int) entries.size()) + (d ? " drum sets to " : " patches to ") + f.getFileName();
    return true;
}

bool Session::savePatchSyx (const juce::File& f, juce::String& message) const
{
    auto bytes = drumMode ? syx::writeDrumSet (drumKit) : syx::writeVoicePatch (edit());
    if (! f.replaceWithData (bytes.data(), bytes.size()))
    {
        message = "Could not write " + f.getFileName();
        return false;
    }
    message = "Saved " + (drumMode ? juce::String ("the drum set") : juce::String (edit().name())) + " to " + f.getFileName();
    return true;
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
juce::ValueTree Session::toValueTree() const
{
    juce::ValueTree v ("FmControl");
    v.setProperty ("version", 1, nullptr);
    v.setProperty ("midiIn", device.getInputId(), nullptr);
    v.setProperty ("midiOut", device.getOutputId(), nullptr);
    v.setProperty ("deviceNumber", (int) device.getDeviceNumber(), nullptr);
    v.setProperty ("live", live, nullptr);
    v.setProperty ("keysFollowVoice", followVoice, nullptr);
    v.setProperty ("keyboardChannel", device.getKeyboardChannel(), nullptr);
    v.setProperty ("bank", bankNumber, nullptr);
    v.setProperty ("editVoice", editVoice, nullptr);
    v.setProperty ("drumMode", drumMode, nullptr);
    v.setProperty ("drumBank", drumBankNumber, nullptr);
    v.setProperty ("drums", juce::Base64::toBase64 (drumKit.data.data(), kPatchSize), nullptr);
    v.setProperty ("drumSlot", drumSourceSlot, nullptr);
    for (int i = 0; i < kVoices; ++i)
    {
        v.setProperty ("voice" + juce::String (i), juce::Base64::toBase64 (voices[(size_t) i].data.data(), kPatchSize), nullptr);
        v.setProperty ("voiceSlot" + juce::String (i), sourceSlot[(size_t) i], nullptr);
    }

    juce::ValueTree b ("Bank");
    for (int i = 0; i < kSlots; ++i)
    {
        if (! slots[(size_t) i]) continue;
        juce::ValueTree s ("Slot");
        s.setProperty ("index", i, nullptr);
        s.setProperty ("data", juce::Base64::toBase64 (slots[(size_t) i]->data.data(), kPatchSize), nullptr);
        b.appendChild (s, nullptr);
    }
    v.appendChild (b, nullptr);

    juce::ValueTree db ("DrumBank");
    for (int i = 0; i < kDrumSetsPerBank; ++i)
    {
        if (! drumSlots[(size_t) i]) continue;
        juce::ValueTree s ("Slot");
        s.setProperty ("index", i, nullptr);
        s.setProperty ("data", juce::Base64::toBase64 (drumSlots[(size_t) i]->data.data(), kPatchSize), nullptr);
        db.appendChild (s, nullptr);
    }
    v.appendChild (db, nullptr);
    v.appendChild (midiLearn->toValueTree(), nullptr);
    return v;
}

void Session::fromValueTree (const juce::ValueTree& v)
{
    if (! v.hasType ("FmControl"))
        return;

    auto decode = [] (const juce::String& s, Patch& out)
    {
        juce::MemoryOutputStream mo;
        if (! juce::Base64::convertFromBase64 (mo, s) || mo.getDataSize() != (size_t) kPatchSize)
            return false;
        std::memcpy (out.data.data(), mo.getData(), (size_t) kPatchSize);
        return true;
    };

    device.setDeviceNumber (v.getProperty ("deviceNumber", 0));
    live = v.getProperty ("live", true);
    followVoice = v.getProperty ("keysFollowVoice", true);
    device.setKeyboardChannel (v.getProperty ("keyboardChannel", 0));
    bankNumber = juce::jlimit (0, kBanks - 1, (int) v.getProperty ("bank", 0));
    editVoice = juce::jlimit (0, kVoices - 1, (int) v.getProperty ("editVoice", 0));
    drumMode = v.getProperty ("drumMode", false);
    drumBankNumber = juce::jlimit (0, kBanks - 1, (int) v.getProperty ("drumBank", 0));
    drumSourceSlot = v.getProperty ("drumSlot", -1);
    drumKnown = false;

    Patch p;
    for (int i = 0; i < kVoices; ++i)
    {
        if (decode (v.getProperty ("voice" + juce::String (i)).toString(), p))
            voices[(size_t) i] = p;
        sourceSlot[(size_t) i] = v.getProperty ("voiceSlot" + juce::String (i), -1);
    }
    voiceKnown = {}; // the box may have changed since: fetch again when shown

    if (decode (v.getProperty ("drums").toString(), p))
        drumKit = p;

    auto readBank = [&] (const juce::ValueTree& tree, std::vector<std::optional<Patch>>& list)
    {
        for (auto& slot : list)
            slot.reset();
        for (auto s : tree)
        {
            const int i = s.getProperty ("index", -1);
            if (i >= 0 && i < (int) list.size() && decode (s.getProperty ("data").toString(), p))
                list[(size_t) i] = p;
        }
    };
    readBank (v.getChildWithName ("Bank"), slots);
    readBank (v.getChildWithName ("DrumBank"), drumSlots);

    midiLearn->fromValueTree (v.getChildWithName ("MidiLearn"));

    if (auto in = v.getProperty ("midiIn").toString(); in.isNotEmpty())
        device.openInput (in);
    if (auto out = v.getProperty ("midiOut").toString(); out.isNotEmpty())
        device.openOutput (out);
    updateKeyboardChannel();

    fireReplaced();
    drumTarget.replaced();
    listeners.call ([] (Listener& l) { l.bankChanged(); });
}

// ---------------------------------------------------------------------------
// Device notifications
// ---------------------------------------------------------------------------
void Session::fmUnsolicitedDump (const sysex::Message& m)
{
    if (m.isDrums())
    {
        if (! m.isDrumsRam() && m.bank == drumBankNumber && m.patchNumber < kDrumSetsPerBank)
        {
            drumSlots[(size_t) m.patchNumber] = m.patch;
            listeners.call ([] (Listener& l) { l.bankChanged(); });
            setStatus ("Received drum set " + slotName (m.bank, m.patchNumber, true));
        }
        else
        {
            drumKit = m.patch;
            drumKnown = true;
            drumSourceSlot = -1;
            fireDrumsReplaced();
            setStatus ("Received the drum set from MIDIbox FM");
        }
        return;
    }
    if (m.isVoiceRam())
    {
        const int v = m.voice();
        voices[(size_t) v] = m.patch;
        voiceKnown[(size_t) v] = true;
        if (v == editVoice)
            fireReplaced();
        setStatus ("Received voice " + juce::String (v + 1) + " from MIDIbox FM: " + juce::String (m.patch.name()));
    }
    else if (m.bank == bankNumber)
    {
        slots[(size_t) (m.patchNumber & 0x7f)] = m.patch;
        listeners.call ([] (Listener& l) { l.bankChanged(); });
        setStatus ("Received " + slotName (m.bank, m.patchNumber) + ": " + juce::String (m.patch.name()));
    }
    else
    {
        editRef() = m.patch;
        fireReplaced();
        setStatus ("Received " + slotName (m.bank, m.patchNumber) + " from MIDIbox FM: " + juce::String (m.patch.name()));
    }
}

void Session::fmLog (const juce::String& text)
{
    logLines.add (juce::Time::getCurrentTime().toString (false, true, true, true) + "  " + text);
    while (logLines.size() > 200)
        logLines.remove (0);
    listeners.call ([] (Listener& l) { l.statusChanged(); });
}

void Session::setStatus (const juce::String& s, float newProgress)
{
    status = s;
    progress = newProgress;
    fmLog (s);
}
