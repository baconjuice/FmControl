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

juce::String Session::slotName (int bank, int slot)
{
    return juce::String::charToString ((juce::juce_wchar) ('A' + bank)) + juce::String (slot + 1).paddedLeft ('0', 3);
}

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
    device.removeListener (this);
}

void Session::fireReplaced()
{
    listeners.call ([] (Listener& l) { l.editReplaced(); });
    notifyReplaced();
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
    if (v == editVoice)
        return;
    editVoice = v;
    updateKeyboardChannel();
    fireReplaced();
    if (! voiceKnown[(size_t) v] && device.isOutputOpen() && device.isInputOpen())
        fetchEditBuffer();
    else
        setStatus ("Editing voice " + juce::String (v + 1) + ": " + juce::String (edit().name()));
}

void Session::setKeysFollowVoice (bool f)
{
    followVoice = f;
    updateKeyboardChannel();
}

void Session::updateKeyboardChannel()
{
    if (followVoice)
        device.setKeyboardChannel (voiceChannel (editVoice));
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
    editRef() = p;
    sourceSlot[(size_t) editVoice] = fromSlot;
    fireReplaced();
    if (live)
        sendEditBuffer();
}

void Session::randomize (const std::string& section, float amount)
{
    auto& history = randomHistory[(size_t) editVoice];
    history.push_back (edit());
    if (history.size() > 100)
        history.erase (history.begin());
    fm::randomize (editRef(), section, amount, rng);
    sourceSlot[(size_t) editVoice] = -1;
    fireReplaced();
    sendEditBufferSoon();
}

void Session::undoRandom()
{
    auto& history = randomHistory[(size_t) editVoice];
    if (history.empty())
        return;
    editRef() = history.back();
    history.pop_back();
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

void Session::fetchEditBuffer()
{
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
    if (i < 0 || i >= kSlots) return;
    slots[(size_t) i] = std::move (p);
    listeners.call ([] (Listener& l) { l.bankChanged(); });
}

void Session::setBankNumber (int b)
{
    bankNumber = juce::jlimit (0, kBanks - 1, b);
    listeners.call ([] (Listener& l) { l.bankChanged(); });
}

void Session::fetchSlot (int i, bool thenEdit)
{
    juce::WeakReference<Session> self (this);
    const int bank = bankNumber;
    setStatus ("Requesting " + slotName (bank, i) + "...");
    device.enqueue (sysex::requestStored (device.getDeviceNumber(), (uint8_t) bank, (uint8_t) i), FmDevice::Expect::Dump,
                    kDumpTimeoutMs,
                    [self, i, bank, thenEdit] (const FmDevice::Result& r)
                    {
                        auto* s = self.get();
                        if (s == nullptr) return;
                        if (! r.ok || r.reply == nullptr)
                        {
                            s->setStatus ("Fetch " + slotName (bank, i) + " failed: " + r.error);
                            return;
                        }
                        s->setSlot (i, r.reply->patch);
                        s->setStatus ("Received " + slotName (bank, i) + ": " + juce::String (r.reply->patch.name()));
                        if (thenEdit)
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
    fetchNext (0, kSlots, 0, 0);
}

void Session::fetchNext (int i, int endExclusive, int got, int failed)
{
    if (cancelRequested || i >= endExclusive)
    {
        busy = false;
        setStatus (juce::String (cancelRequested ? "Cancelled: " : "Bank fetched: ") + juce::String (got) + " patches"
                   + (failed > 0 ? ", " + juce::String (failed) + " not available" : juce::String()));
        return;
    }

    setStatus ("Fetching " + slotName (bankNumber, i) + "...", (float) i / (float) endExclusive);
    juce::WeakReference<Session> self (this);
    device.enqueue (sysex::requestStored (device.getDeviceNumber(), (uint8_t) bankNumber, (uint8_t) i), FmDevice::Expect::Dump,
                    kDumpTimeoutMs,
                    [self, i, endExclusive, got, failed] (const FmDevice::Result& r)
                    {
                        auto* s = self.get();
                        if (s == nullptr) return;
                        if (r.ok && r.reply != nullptr)
                        {
                            s->slots[(size_t) i] = r.reply->patch;
                            s->listeners.call ([] (Listener& l) { l.bankChanged(); });
                            s->fetchNext (i + 1, endExclusive, got + 1, failed);
                        }
                        else if (r.reply != nullptr)
                        {
                            // The box said "not available": don't leave the previous
                            // fetch's patch in this slot.
                            if (i == 0 && r.reply->kind == sysex::Message::Kind::Error && r.reply->errorCode == 0x0a)
                            {
                                // No BankStick for this bank at all.
                                for (auto& slot : s->slots)
                                    slot.reset();
                                s->listeners.call ([] (Listener& l) { l.bankChanged(); });
                                s->busy = false;
                                s->setStatus ("Bank " + juce::String::charToString ((juce::juce_wchar) ('A' + s->bankNumber))
                                              + " is not available on the box (no BankStick for it)");
                                return;
                            }
                            s->slots[(size_t) i].reset();
                            s->listeners.call ([] (Listener& l) { l.bankChanged(); });
                            s->fetchNext (i + 1, endExclusive, got, failed + 1);
                        }
                        else
                        {
                            s->busy = false;
                            s->setStatus ("Bank fetch stopped at " + slotName (s->bankNumber, i) + ": " + r.error);
                        }
                    });
}

void Session::storeEditToSlot (int i)
{
    if (i < 0 || i >= kSlots) return;
    juce::WeakReference<Session> self (this);
    const int bank = bankNumber;
    const int v = editVoice;
    const Patch p = edit();
    setStatus ("Storing to " + slotName (bank, i) + "...");
    device.enqueue (sysex::writeStored (device.getDeviceNumber(), (uint8_t) bank, (uint8_t) i, p), FmDevice::Expect::Ack,
                    kWriteTimeoutMs,
                    [self, i, bank, p, v] (const FmDevice::Result& r)
                    {
                        auto* s = self.get();
                        if (s == nullptr) return;
                        if (r.ok)
                        {
                            s->sourceSlot[(size_t) v] = i;
                            s->setSlot (i, p);
                            s->setStatus ("Stored " + slotName (bank, i) + ": " + juce::String (p.name()));
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
    std::vector<int> todo;
    for (int i = kSlots - 1; i >= 0; --i)
        if (slots[(size_t) i]) todo.push_back (i);
    if (todo.empty())
    {
        setStatus ("Bank is empty");
        return;
    }
    busy = true;
    cancelRequested = false;
    const int total = (int) todo.size();
    writeNext (std::move (todo), 0, 0, total);
}

void Session::writeNext (std::vector<int> remaining, int done, int failed, int total)
{
    if (cancelRequested || remaining.empty())
    {
        busy = false;
        setStatus (juce::String (cancelRequested ? "Cancelled: " : "Bank written: ") + juce::String (done) + " patches"
                   + (failed > 0 ? ", " + juce::String (failed) + " failed" : juce::String()));
        return;
    }
    const int i = remaining.back();
    remaining.pop_back();
    setStatus ("Writing " + slotName (bankNumber, i) + "...", (float) done / (float) total);

    juce::WeakReference<Session> self (this);
    device.enqueue (sysex::writeStored (device.getDeviceNumber(), (uint8_t) bankNumber, (uint8_t) i, *slots[(size_t) i]),
                    FmDevice::Expect::Ack, kWriteTimeoutMs,
                    [self, remaining, done, failed, total] (const FmDevice::Result& r) mutable
                    {
                        auto* s = self.get();
                        if (s == nullptr) return;
                        if (! r.ok && r.reply == nullptr)
                        {
                            s->busy = false;
                            s->setStatus ("Bank write stopped: " + r.error);
                            return;
                        }
                        s->writeNext (std::move (remaining), done + (r.ok ? 1 : 0), failed + (r.ok ? 0 : 1), total);
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
        message = f.getFileName() + " contains no MIDIbox FM voice patches";
        return false;
    }

    int bad = 0;
    for (auto& e : r.patches)
        if (! e.checksumOk) ++bad;

    if (r.patches.size() == 1)
    {
        loadIntoEditor (r.patches.front().patch);
        message = "Loaded patch " + juce::String (r.patches.front().patch.name());
    }
    else
    {
        for (auto& e : r.patches)
            slots[(size_t) (e.patchNumber & 0x7f)] = e.patch;
        listeners.call ([] (Listener& l) { l.bankChanged(); });
        message = "Loaded " + juce::String ((int) r.patches.size()) + " patches from " + f.getFileName();
    }
    if (bad > 0)
        message << " (" << bad << " with bad checksum)";
    return true;
}

bool Session::saveBankSyx (const juce::File& f, juce::String& message) const
{
    std::vector<syx::Entry> entries;
    for (int i = 0; i < kSlots; ++i)
    {
        if (! slots[(size_t) i]) continue;
        syx::Entry e;
        e.bank = (uint8_t) bankNumber;
        e.patchNumber = (uint8_t) i;
        e.patch = *slots[(size_t) i];
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
    message = "Saved " + juce::String ((int) entries.size()) + " patches to " + f.getFileName();
    return true;
}

bool Session::savePatchSyx (const juce::File& f, juce::String& message) const
{
    auto bytes = syx::writeVoicePatch (edit());
    if (! f.replaceWithData (bytes.data(), bytes.size()))
    {
        message = "Could not write " + f.getFileName();
        return false;
    }
    message = "Saved " + juce::String (edit().name()) + " to " + f.getFileName();
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

    Patch p;
    for (int i = 0; i < kVoices; ++i)
    {
        if (decode (v.getProperty ("voice" + juce::String (i)).toString(), p))
            voices[(size_t) i] = p;
        sourceSlot[(size_t) i] = v.getProperty ("voiceSlot" + juce::String (i), -1);
    }
    voiceKnown = {}; // the box may have changed since: fetch again when shown

    slots = {};
    for (auto s : v.getChildWithName ("Bank"))
    {
        const int i = s.getProperty ("index", -1);
        if (i >= 0 && i < kSlots && decode (s.getProperty ("data").toString(), p))
            slots[(size_t) i] = p;
    }

    midiLearn->fromValueTree (v.getChildWithName ("MidiLearn"));

    if (auto in = v.getProperty ("midiIn").toString(); in.isNotEmpty())
        device.openInput (in);
    if (auto out = v.getProperty ("midiOut").toString(); out.isNotEmpty())
        device.openOutput (out);
    updateKeyboardChannel();

    fireReplaced();
    listeners.call ([] (Listener& l) { l.bankChanged(); });
}

// ---------------------------------------------------------------------------
// Device notifications
// ---------------------------------------------------------------------------
void Session::fmUnsolicitedDump (const sysex::Message& m)
{
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
        setSlot (m.patchNumber, m.patch);
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
