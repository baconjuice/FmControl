#include "Session.h"
#include "MidiLearn.h"
#include "fm/Randomizer.h"

#include <algorithm>
#include <cstring>
#include <utility>

using namespace fm;

namespace
{
constexpr int kDumpTimeoutMs = 2500;
constexpr int kWriteTimeoutMs = 4000; // BankStick (EEPROM) writes are slow
constexpr int kPlayNote = 60;
} // namespace

juce::String Session::slotName (int bank, int slot, Part p)
{
    return juce::String::charToString ((juce::juce_wchar) ('A' + bank)) + juce::String (slot + 1).paddedLeft ('0', p == Part::Voice ? 3 : 2);
}

juce::String Session::partNoun (Part p, bool plural)
{
    switch (p)
    {
        case Part::Drums:    return plural ? "drum sets" : "drum set";
        case Part::Ensemble: return plural ? "ensembles" : "ensemble";
        case Part::Voice:    break;
    }
    return plural ? "patches" : "patch";
}

namespace
{
Session::Part partOf (const sysex::Message& m)
{
    return m.isEnsemble() ? Session::Part::Ensemble : m.isDrums() ? Session::Part::Drums : Session::Part::Voice;
}

// The four patches an ensemble plays: "A005 A037 A084 A099".
juce::String ensembleSummary (const Patch& e)
{
    juce::StringArray s;
    for (int v = 0; v < kVoices; ++v)
        s.add (Session::slotName (ensembleBank (e, v), ensemblePatch (e, v)));
    return s.joinIntoString (" ");
}
} // namespace

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
MidiLearn* DrumTarget::getMidiLearn() { return session.getMidiLearn(); }
int DrumTarget::learnScope() const { return MidiLearn::kDrumScope; }
AutomationLink* DrumTarget::getAutomation() { return session.getAutomation(); }

uint8_t EnsembleTarget::getByte (int addr) const { return session.getEnsemble().byte (addr); }
int EnsembleTarget::getParam (const ParamDef& p) const { return session.getEnsemble().getRaw (p); }
void EnsembleTarget::setParam (const ParamDef& p, int raw, uint8_t) { session.setEnsembleParam (p, raw); }
int EnsembleTarget::defaultRaw (const ParamDef& p) const { return makeInitEnsemble().getRaw (p); }

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

void Session::fireEnsembleReplaced()
{
    ensembleTarget.replaced();
    listeners.call ([] (Listener& l) { l.editReplaced(); });
    updateKeyboardChannel();
}

juce::String Session::partName() const
{
    return part == Part::Voice ? "voice " + juce::String (editVoice + 1) : partNoun (part);
}

const Patch& Session::current() const
{
    return part == Part::Drums ? drumKit : part == Part::Ensemble ? ensemble : edit();
}

Patch& Session::currentRef()
{
    return part == Part::Drums ? drumKit : part == Part::Ensemble ? ensemble : editRef();
}

std::vector<Patch>& Session::historyRef()
{
    return part == Part::Drums ? drumHistory : part == Part::Ensemble ? noHistory : randomHistory[(size_t) editVoice];
}

const std::vector<Patch>& Session::historyRef() const
{
    return part == Part::Drums ? drumHistory : part == Part::Ensemble ? noHistory : randomHistory[(size_t) editVoice];
}

std::vector<std::optional<Patch>>& Session::slotsFor (Part p)
{
    return p == Part::Drums ? drumSlots : p == Part::Ensemble ? ensembleSlots : slots;
}

const std::vector<std::optional<Patch>>& Session::slotsFor (Part p) const
{
    return p == Part::Drums ? drumSlots : p == Part::Ensemble ? ensembleSlots : slots;
}

int& Session::bankRef (Part p)
{
    return p == Part::Drums ? drumBankNumber : p == Part::Ensemble ? ensembleBankNumber : bankNumber;
}

int Session::bankFor (Part p) const
{
    return p == Part::Drums ? drumBankNumber : p == Part::Ensemble ? ensembleBankNumber : bankNumber;
}

int& Session::sourceSlotRef()
{
    return part == Part::Drums ? drumSourceSlot : part == Part::Ensemble ? ensembleSourceSlot : sourceSlot[(size_t) editVoice];
}

int Session::editorSourceSlot() const
{
    return part == Part::Drums ? drumSourceSlot : part == Part::Ensemble ? ensembleSourceSlot : sourceSlot[(size_t) editVoice];
}

juce::String Session::storedPatchLabel (int bank, int patch) const
{
    auto s = slotName (bank, patch);
    if (bank == bankNumber && patch >= 0 && patch < kSlots && slots[(size_t) patch])
        s << " " << juce::String (slots[(size_t) patch]->name()).trimEnd();
    return s;
}

void Session::setPart (Part p)
{
    switch (p)
    {
        case Part::Voice:    setEditVoice (editVoice); break;
        case Part::Drums:    setDrumMode (true); break;
        case Part::Ensemble: setEnsembleMode (true); break;
    }
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
    if (v == editVoice && part == Part::Voice)
        return;
    if (part != Part::Voice)
    {
        part = Part::Voice;
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
    if (editDrums == (part == Part::Drums))
        return;
    if (! editDrums)
    {
        setEditVoice (editVoice);
        return;
    }
    part = Part::Drums;
    updateKeyboardChannel();
    fireDrumsReplaced();
    listeners.call ([] (Listener& l) { l.bankChanged(); });
    if (! drumKnown && device.isOutputOpen() && device.isInputOpen())
        fetchDrums();
    else
        setStatus ("Editing the drum set (MIDI channel " + juce::String (kDrumChannel) + ")");
}

void Session::setEnsembleMode (bool editEnsemble)
{
    if (editEnsemble == (part == Part::Ensemble))
        return;
    if (! editEnsemble)
    {
        setEditVoice (editVoice);
        return;
    }
    part = Part::Ensemble;
    fireEnsembleReplaced();
    listeners.call ([] (Listener& l) { l.bankChanged(); });
    if (! ensembleKnown && device.isOutputOpen() && device.isInputOpen())
        fetchEnsemble();
    else
        setStatus ("Editing the ensemble");
}

void Session::setEnsembleParam (const ParamDef& p, int raw)
{
    auto changed = ensemble.setRaw (p, raw);
    if (changed.empty())
        return;
    if (live)
    {
        // A new bank or patch only takes effect when the voice loads it:
        // reading the stored patch into that voice does both (and tells us
        // what it sounds like). Everything else is a plain register write.
        const bool patchChange = p.id.size() > 3 && (p.id.compare (3, std::string::npos, "bank") == 0
                                                     || p.id.compare (3, std::string::npos, "patch") == 0);
        if (patchChange)
            loadVoicePatchSoon (p.addr / kEnsembleVoiceStride);
        else
            for (auto addr : changed)
                device.queueParameter (kEnsemblePart, addr, ensemble.byte (addr));
    }
    ensembleTarget.changed (changed);
    updateKeyboardChannel();
}

void Session::loadVoicePatchSoon (int voice)
{
    voice &= 3;
    const int gen = ++patchLoadGeneration[(size_t) voice];
    juce::WeakReference<Session> self (this);
    juce::Timer::callAfterDelay (200, [self, gen, voice]
    {
        auto* s = self.get();
        if (s == nullptr || s->patchLoadGeneration[(size_t) voice] != gen || ! s->device.isOutputOpen())
            return;
        const int bank = ensembleBank (s->ensemble, voice), patch = ensemblePatch (s->ensemble, voice);
        s->device.enqueue (sysex::requestStored (s->device.getDeviceNumber(), (uint8_t) bank, (uint8_t) patch, voice),
                           FmDevice::Expect::Dump, kDumpTimeoutMs,
                           [self, voice, bank, patch] (const FmDevice::Result& r)
                           {
                               auto* s2 = self.get();
                               if (s2 == nullptr) return;
                               const auto label = slotName (bank, patch);
                               if (r.ok && r.reply != nullptr && r.reply->isVoice())
                               {
                                   s2->voices[(size_t) voice] = r.reply->patch;
                                   s2->voiceKnown[(size_t) voice] = true;
                                   s2->sourceSlot[(size_t) voice] = bank == s2->bankNumber ? patch : -1;
                                   if (s2->part == Part::Voice && voice == s2->editVoice)
                                       s2->fireReplaced();
                                   s2->setStatus ("Voice " + juce::String (voice + 1) + " plays " + label + ": "
                                                  + juce::String (r.reply->patch.name()));
                               }
                               else
                               {
                                   s2->setStatus ("Voice " + juce::String (voice + 1) + " could not load " + label + ": "
                                                  + (r.error.isNotEmpty() ? r.error : juce::String ("no patch in the reply")));
                               }
                           });
    });
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
        device.setKeyboardChannel (part == Part::Drums ? kDrumChannel : voiceChannel (editVoice));
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
    if (part == Part::Ensemble)
    {
        ensemble = p;
        ensembleSourceSlot = fromSlot;
        fireEnsembleReplaced();
        if (live)
            sendEditBuffer();
        return;
    }
    if (part == Part::Drums)
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
    if (part == Part::Ensemble)
        return; // no random ensembles
    auto& history = historyRef();
    history.push_back (current());
    if (history.size() > 100)
        history.erase (history.begin());
    if (part == Part::Drums)
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
    if (part == Part::Drums)
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
    if (part == Part::Drums)
    {
        sendDrums();
        return;
    }
    if (part == Part::Ensemble)
    {
        sendEnsemble();
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

void Session::sendEnsemble()
{
    device.clearPendingParameters();
    juce::WeakReference<Session> self (this);
    device.enqueue (sysex::writeEnsembleRam (device.getDeviceNumber(), ensemble), FmDevice::Expect::Ack, kDumpTimeoutMs,
                    [self] (const FmDevice::Result& r)
                    {
                        auto* s = self.get();
                        if (s == nullptr) return;
                        if (! r.ok)
                        {
                            s->setStatus ("Send failed: " + r.error);
                            return;
                        }
                        s->ensembleKnown = true;
                        s->setStatus ("Sent ensemble: the voices now play " + ensembleSummary (s->ensemble));
                        s->rereadVoicesAndDrums();
                    });
}

void Session::fetchEnsemble (bool quiet)
{
    juce::WeakReference<Session> self (this);
    if (! quiet)
        setStatus ("Requesting the ensemble...");
    device.enqueue (sysex::requestEnsembleRam (device.getDeviceNumber()), FmDevice::Expect::Dump, kDumpTimeoutMs,
                    [self, quiet] (const FmDevice::Result& r)
                    {
                        auto* s = self.get();
                        if (s == nullptr) return;
                        if (r.ok && r.reply != nullptr && r.reply->isEnsemble())
                        {
                            s->ensemble = r.reply->patch;
                            s->ensembleKnown = true;
                            s->ensembleSourceSlot = -1;
                            s->fireEnsembleReplaced();
                            if (! quiet)
                                s->setStatus ("Received the ensemble the MIDIbox FM is playing: " + ensembleSummary (s->ensemble));
                        }
                        else if (! quiet)
                        {
                            s->setStatus ("Fetch failed: " + (r.error.isNotEmpty() ? r.error : juce::String ("no ensemble in the reply")));
                        }
                    });
}

void Session::rereadVoicesAndDrums()
{
    juce::WeakReference<Session> self (this);
    for (int v = 0; v < kVoices; ++v)
        device.enqueue (sysex::requestVoice (device.getDeviceNumber(), v), FmDevice::Expect::Dump, kDumpTimeoutMs,
                        [self, v] (const FmDevice::Result& r)
                        {
                            auto* s = self.get();
                            if (s == nullptr || ! r.ok || r.reply == nullptr || ! r.reply->isVoice()) return;
                            s->voices[(size_t) v] = r.reply->patch;
                            s->voiceKnown[(size_t) v] = true;
                            const int bank = ensembleBank (s->ensemble, v);
                            s->sourceSlot[(size_t) v] = bank == s->bankNumber ? ensemblePatch (s->ensemble, v) : -1;
                            if (s->part == Part::Voice && v == s->editVoice)
                                s->fireReplaced();
                        });
    device.enqueue (sysex::requestDrumsRam (device.getDeviceNumber()), FmDevice::Expect::Dump, kDumpTimeoutMs,
                    [self] (const FmDevice::Result& r)
                    {
                        auto* s = self.get();
                        if (s == nullptr || ! r.ok || r.reply == nullptr || ! r.reply->isDrums()) return;
                        s->drumKit = r.reply->patch;
                        s->drumKnown = true;
                        s->drumSourceSlot = -1;
                        if (s->part == Part::Drums)
                            s->fireDrumsReplaced();
                        else
                            s->drumTarget.replaced();
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
                            if (s->part == Part::Drums)
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
    if (part == Part::Drums)
    {
        fetchDrums();
        return;
    }
    if (part == Part::Ensemble)
    {
        fetchEnsemble();
        return;
    }
    // The ensemble says which MIDI channel each voice listens on.
    if (! ensembleKnown)
        fetchEnsemble (true);
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
                            if (v == s->editVoice && s->part == Part::Voice)
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
    if (part == Part::Drums)
    {
        if (on)
            playDrumBeat();
        return;
    }
    if (on)
    {
        // The ensemble plays all its voices (each on its own channel), a voice just itself.
        playingChannels.clear();
        for (int v = 0; v < kVoices; ++v)
            if (part == Part::Ensemble || v == editVoice)
                if (const int ch = voiceChannel (v); std::find (playingChannels.begin(), playingChannels.end(), ch) == playingChannels.end())
                    playingChannels.push_back (ch);
        for (int ch : playingChannels)
            device.sendMidi (juce::MidiMessage::noteOn (ch, kPlayNote, (juce::uint8) 100));
    }
    else
    {
        for (int ch : playingChannels)
            device.sendMidi (juce::MidiMessage::noteOff (ch, kPlayNote));
        playingChannels.clear();
    }
}

void Session::allNotesOff()
{
    for (int ch = 1; ch <= 16; ++ch)
    {
        device.sendMidi (juce::MidiMessage::allNotesOff (ch));
        device.sendMidi (juce::MidiMessage::controllerEvent (ch, 64, 0)); // sustain pedal up
    }
    playingChannels.clear();
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
    auto& list = slotsFor (part);
    if (i < 0 || i >= (int) list.size()) return;
    list[(size_t) i] = std::move (p);
    listeners.call ([] (Listener& l) { l.bankChanged(); });
}

void Session::setBankNumber (int b)
{
    bankRef (part) = juce::jlimit (0, kBanks - 1, b);
    listeners.call ([] (Listener& l) { l.bankChanged(); });
}

sysex::Bytes Session::requestSlot (Part p, int bank, int i) const
{
    const auto dev = device.getDeviceNumber();
    switch (p)
    {
        case Part::Drums:    return sysex::requestDrums (dev, (uint8_t) bank, (uint8_t) i);
        case Part::Ensemble: return sysex::requestEnsemble (dev, (uint8_t) bank, (uint8_t) i);
        case Part::Voice:    break;
    }
    return sysex::requestStored (dev, (uint8_t) bank, (uint8_t) i);
}

sysex::Bytes Session::writeSlot (Part p, int bank, int i, const Patch& patch) const
{
    const auto dev = device.getDeviceNumber();
    switch (p)
    {
        case Part::Drums:    return sysex::writeDrums (dev, (uint8_t) bank, (uint8_t) i, patch);
        case Part::Ensemble: return sysex::writeEnsemble (dev, (uint8_t) bank, (uint8_t) i, patch);
        case Part::Voice:    break;
    }
    return sysex::writeStored (dev, (uint8_t) bank, (uint8_t) i, patch);
}

namespace
{
juce::String describe (const Patch& p, Session::Part part)
{
    switch (part)
    {
        case Session::Part::Drums:    return "drum set";
        case Session::Part::Ensemble: return ensembleSummary (p);
        case Session::Part::Voice:    break;
    }
    return p.name();
}
} // namespace

void Session::saveVoice1Then (std::function<void()> next)
{
    voice1Before.reset();
    voice1SlotBefore.reset();
    juce::WeakReference<Session> self (this);
    device.enqueue (sysex::requestEnsembleRam (device.getDeviceNumber()), FmDevice::Expect::Dump, kDumpTimeoutMs,
                    [self] (const FmDevice::Result& r)
                    {
                        auto* s = self.get();
                        if (s == nullptr) return;
                        if (r.ok && r.reply != nullptr && r.reply->isEnsemble())
                        {
                            s->ensemble = r.reply->patch;
                            s->ensembleKnown = true;
                            s->ensembleTarget.replaced();
                        }
                        if (s->ensembleKnown)
                            s->voice1SlotBefore = std::pair { ensembleBank (s->ensemble, 0), ensemblePatch (s->ensemble, 0) };
                    });
    device.enqueue (sysex::requestVoice (device.getDeviceNumber(), 0), FmDevice::Expect::Dump, kDumpTimeoutMs,
                    [self, then = std::move (next)] (const FmDevice::Result& r)
                    {
                        auto* s = self.get();
                        if (s == nullptr) return;
                        if (r.ok && r.reply != nullptr && r.reply->isVoice())
                            s->voice1Before = r.reply->patch;
                        else if (s->voiceKnown[0])
                            s->voice1Before = s->voices[0];
                        then();
                    });
}

void Session::restoreVoice1 (bool patchToo)
{
    const auto slotBefore = std::exchange (voice1SlotBefore, std::nullopt);
    const auto saved = std::exchange (voice1Before, std::nullopt);
    const auto dev = device.getDeviceNumber();
    if (slotBefore)
    {
        // Plain register writes: they don't reload the voice.
        device.enqueue (sysex::ensembleParameterWrite (dev, 0x00, (uint8_t) slotBefore->first), FmDevice::Expect::Ack, kDumpTimeoutMs, {});
        device.enqueue (sysex::ensembleParameterWrite (dev, 0x01, (uint8_t) slotBefore->second), FmDevice::Expect::Ack, kDumpTimeoutMs, {});
    }
    if (! patchToo || ! saved)
        return;
    juce::WeakReference<Session> self (this);
    device.enqueue (sysex::writeVoice (dev, 0, *saved), FmDevice::Expect::Ack, kDumpTimeoutMs,
                    [self, p = *saved] (const FmDevice::Result& r)
                    {
                        auto* s = self.get();
                        if (s == nullptr || ! r.ok) return;
                        s->voices[0] = p;
                        s->voiceKnown[0] = true;
                        if (s->part == Part::Voice && s->editVoice == 0)
                            s->fireReplaced();
                    });
}

void Session::fetchSlot (int i, bool thenEdit)
{
    if (part == Part::Voice && device.isInputOpen())
    {
        juce::WeakReference<Session> self (this);
        saveVoice1Then ([self, i, thenEdit] { if (auto* s = self.get()) s->fetchSlotNow (i, thenEdit); });
        return;
    }
    fetchSlotNow (i, thenEdit);
}

void Session::fetchSlotNow (int i, bool thenEdit)
{
    juce::WeakReference<Session> self (this);
    const Part d = part;
    const int bank = getBankNumber();
    setStatus ("Requesting " + slotName (bank, i, d) + "...");
    device.enqueue (requestSlot (d, bank, i), FmDevice::Expect::Dump, kDumpTimeoutMs,
                    [self, i, bank, d, thenEdit] (const FmDevice::Result& r)
                    {
                        auto* s = self.get();
                        if (s == nullptr) return;
                        // Voice 1 got this patch too: put back what it played (unless
                        // it is the voice the patch is about to be opened in).
                        if (d == Part::Voice)
                            s->restoreVoice1 (! (thenEdit && s->part == Part::Voice && s->editVoice == 0 && r.ok));
                        if (! r.ok || r.reply == nullptr || partOf (*r.reply) != d)
                        {
                            s->setStatus ("Fetch " + slotName (bank, i, d) + " failed: " + r.error);
                            return;
                        }
                        s->slotsFor (d)[(size_t) i] = r.reply->patch;
                        s->listeners.call ([] (Listener& l) { l.bankChanged(); });
                        s->setStatus ("Received " + slotName (bank, i, d) + ": " + describe (r.reply->patch, d));
                        if (d == Part::Ensemble)
                        {
                            // Reading it switched the box to this ensemble, so
                            // the editor now shows what plays.
                            s->ensemble = r.reply->patch;
                            s->ensembleKnown = true;
                            s->ensembleSourceSlot = i;
                            s->fireEnsembleReplaced();
                            s->rereadVoicesAndDrums();
                            return;
                        }
                        if (thenEdit && s->part == d)
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
    if (part == Part::Drums)
    {
        fetchNext (part, 0, slotCount(), 0, 0);
        return;
    }
    if (part == Part::Voice)
    {
        setStatus ("Reading voice 1, to put it back afterwards...");
        juce::WeakReference<Session> self (this);
        saveVoice1Then ([self]
        {
            auto* s = self.get();
            if (s == nullptr) return;
            if (s->cancelRequested)
            {
                s->busy = false;
                s->restoreVoice1 (true);
                return;
            }
            s->fetchNext (Part::Voice, 0, kSlots, 0, 0);
        });
        return;
    }

    // Reading each stored ensemble switches the box to it: remember the one
    // it plays now, to put back at the end.
    setStatus ("Reading the ensemble the MIDIbox FM is playing...");
    juce::WeakReference<Session> self (this);
    device.enqueue (sysex::requestEnsembleRam (device.getDeviceNumber()), FmDevice::Expect::Dump, kDumpTimeoutMs,
                    [self] (const FmDevice::Result& r)
                    {
                        auto* s = self.get();
                        if (s == nullptr) return;
                        if (s->cancelRequested)
                        {
                            s->busy = false;
                            return;
                        }
                        if (r.ok && r.reply != nullptr && r.reply->isEnsemble())
                            s->ensembleBeforeTransfer = r.reply->patch;
                        else
                            s->ensembleBeforeTransfer = s->ensembleKnown ? std::optional<Patch> (s->ensemble) : std::nullopt;
                        s->fetchNext (Part::Ensemble, 0, kEnsemblesPerBank, 0, 0);
                    });
}

void Session::finishEnsembleTransfer (const juce::String& message)
{
    juce::WeakReference<Session> self (this);
    auto saved = std::exchange (ensembleBeforeTransfer, std::nullopt);
    if (! saved)
    {
        setStatus (message);
        fetchEnsemble (true);
        rereadVoicesAndDrums();
        return;
    }
    setStatus (message + "; restoring the ensemble...");
    device.enqueue (sysex::writeEnsembleRam (device.getDeviceNumber(), *saved), FmDevice::Expect::Ack, kDumpTimeoutMs,
                    [self, saved, message] (const FmDevice::Result& r)
                    {
                        auto* s = self.get();
                        if (s == nullptr) return;
                        if (! r.ok)
                        {
                            s->setStatus (message + "; could not restore the ensemble: " + r.error);
                            return;
                        }
                        s->ensemble = *saved;
                        s->ensembleKnown = true;
                        s->fireEnsembleReplaced();
                        s->rereadVoicesAndDrums();
                        s->setStatus (message + "; the box plays " + ensembleSummary (*saved) + " again");
                    });
}

void Session::fetchNext (Part d, int i, int endExclusive, int got, int failed)
{
    const int bank = bankFor (d);
    const juce::String what = partNoun (d, true);
    auto finish = [this, d] (const juce::String& message)
    {
        busy = false;
        if (d == Part::Ensemble)
            finishEnsembleTransfer (message);
        else
            setStatus (message);
        if (d == Part::Voice)
            restoreVoice1 (true);
    };
    if (cancelRequested || i >= endExclusive)
    {
        finish (juce::String (cancelRequested ? "Cancelled: " : "Bank fetched: ") + juce::String (got) + " " + what
                + (failed > 0 ? ", " + juce::String (failed) + " not available" : juce::String()));
        return;
    }

    setStatus ("Fetching " + slotName (bank, i, d) + "...", (float) i / (float) endExclusive);
    juce::WeakReference<Session> self (this);
    device.enqueue (requestSlot (d, bank, i), FmDevice::Expect::Dump, kDumpTimeoutMs,
                    [self, d, i, endExclusive, got, failed, finish] (const FmDevice::Result& r)
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
                                const int b = s->bankFor (d);
                                finish ("Bank " + juce::String::charToString ((juce::juce_wchar) ('A' + b))
                                        + " is not available on the box (no BankStick for it)");
                                return;
                            }
                            list[(size_t) i].reset();
                            s->listeners.call ([] (Listener& l) { l.bankChanged(); });
                            s->fetchNext (d, i + 1, endExclusive, got, failed + 1);
                        }
                        else
                        {
                            finish ("Bank fetch stopped at " + slotName (s->bankFor (d), i, d) + ": " + r.error);
                        }
                    });
}

void Session::storeEditToSlot (int i)
{
    if (i < 0 || i >= slotCount()) return;
    juce::WeakReference<Session> self (this);
    const Part d = part;
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
                            (d == Part::Drums ? s->drumSourceSlot : d == Part::Ensemble ? s->ensembleSourceSlot : s->sourceSlot[(size_t) v]) = i;
                            s->slotsFor (d)[(size_t) i] = p;
                            s->listeners.call ([] (Listener& l) { l.bankChanged(); });
                            s->setStatus ("Stored " + slotName (bank, i, d) + ": " + describe (p, d));
                            if (d == Part::Ensemble)
                            {
                                // Storing also loads it: the voices play their stored patches again.
                                s->ensembleKnown = true;
                                s->rereadVoicesAndDrums();
                            }
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
    const Part d = part;
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
    // Each stored ensemble also loads on the box; afterwards it gets the editor's one back.
    if (d == Part::Ensemble)
        ensembleBeforeTransfer = ensembleKnown ? std::optional<Patch> (ensemble) : std::nullopt;
    const int total = (int) todo.size();
    writeNext (d, std::move (todo), 0, 0, total);
}

void Session::writeNext (Part d, std::vector<int> remaining, int done, int failed, int total)
{
    const int bank = bankFor (d);
    auto finish = [this, d] (const juce::String& message)
    {
        busy = false;
        if (d == Part::Ensemble)
            finishEnsembleTransfer (message);
        else
            setStatus (message);
    };
    if (cancelRequested || remaining.empty())
    {
        finish (juce::String (cancelRequested ? "Cancelled: " : "Bank written: ") + juce::String (done) + " " + partNoun (d, true)
                + (failed > 0 ? ", " + juce::String (failed) + " failed" : juce::String()));
        return;
    }
    const int i = remaining.back();
    remaining.pop_back();
    setStatus ("Writing " + slotName (bank, i, d) + "...", (float) done / (float) total);

    juce::WeakReference<Session> self (this);
    device.enqueue (writeSlot (d, bank, i, *slotsFor (d)[(size_t) i]), FmDevice::Expect::Ack, kWriteTimeoutMs,
                    [self, d, remaining, done, failed, total, finish] (const FmDevice::Result& r) mutable
                    {
                        auto* s = self.get();
                        if (s == nullptr) return;
                        if (! r.ok && r.reply == nullptr)
                        {
                            finish ("Bank write stopped: " + r.error);
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
        message = f.getFileName() + " contains no MIDIbox FM voice patches, drum sets or ensembles";
        return false;
    }

    auto partOfEntry = [] (const syx::Entry& e) { return e.ensemble ? Part::Ensemble : e.drums ? Part::Drums : Part::Voice; };
    int bad = 0;
    std::array<int, 3> count {}; // per Part
    for (auto& e : r.patches)
    {
        if (! e.checksumOk) ++bad;
        ++count[(size_t) partOfEntry (e)];
    }

    // A file of one kind switches the editor to that part (a mixed file
    // fills the lists and stays where it is). A single dump is marked as
    // known first, so switching doesn't fetch from the box and overwrite it.
    if ((count[0] > 0) + (count[1] > 0) + (count[2] > 0) == 1)
    {
        const Part p = partOfEntry (r.patches.front());
        if (r.patches.size() == 1)
        {
            if (p == Part::Drums) drumKnown = true;
            if (p == Part::Ensemble) ensembleKnown = true;
            if (p == Part::Voice) voiceKnown[(size_t) editVoice] = true;
        }
        setPart (p);
    }

    if (r.patches.size() == 1)
    {
        const auto& e = r.patches.front();
        loadIntoEditor (e.patch);
        message = e.ensemble ? "Loaded ensemble " + ensembleSummary (e.patch)
                  : e.drums  ? juce::String ("Loaded drum set")
                             : "Loaded patch " + juce::String (e.patch.name());
    }
    else
    {
        for (auto& e : r.patches)
        {
            auto& list = slotsFor (partOfEntry (e));
            const size_t i = (size_t) (e.patchNumber & 0x7f);
            if (i < list.size())
                list[i] = e.patch;
        }
        listeners.call ([] (Listener& l) { l.bankChanged(); });
        juce::StringArray parts;
        for (auto p : { Part::Voice, Part::Drums, Part::Ensemble })
            if (const int n = count[(size_t) p]; n > 0)
                parts.add (juce::String (n) + " " + partNoun (p, true));
        message = "Loaded " + parts.joinIntoString (", ") + " from " + f.getFileName();
    }
    if (bad > 0)
        message << " (" << bad << " with bad checksum)";
    return true;
}

bool Session::saveBankSyx (const juce::File& f, juce::String& message) const
{
    const Part d = part;
    const auto& list = slotsFor (d);
    std::vector<syx::Entry> entries;
    for (int i = 0; i < (int) list.size(); ++i)
    {
        if (! list[(size_t) i]) continue;
        syx::Entry e;
        e.bank = (uint8_t) getBankNumber();
        e.patchNumber = (uint8_t) i;
        e.drums = d == Part::Drums;
        e.ensemble = d == Part::Ensemble;
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
    message = "Saved " + juce::String ((int) entries.size()) + " " + partNoun (d, true) + " to " + f.getFileName();
    return true;
}

bool Session::savePatchSyx (const juce::File& f, juce::String& message) const
{
    auto bytes = part == Part::Drums      ? syx::writeDrumSet (drumKit)
                 : part == Part::Ensemble ? syx::writeEnsembleSetup (ensemble)
                                          : syx::writeVoicePatch (edit());
    if (! f.replaceWithData (bytes.data(), bytes.size()))
    {
        message = "Could not write " + f.getFileName();
        return false;
    }
    message = "Saved " + (part == Part::Voice ? juce::String (edit().name()) : "the " + partNoun (part)) + " to " + f.getFileName();
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
    v.setProperty ("part", (int) part, nullptr);
    v.setProperty ("drumMode", part == Part::Drums, nullptr); // read by older versions
    v.setProperty ("drumBank", drumBankNumber, nullptr);
    v.setProperty ("drums", juce::Base64::toBase64 (drumKit.data.data(), kPatchSize), nullptr);
    v.setProperty ("drumSlot", drumSourceSlot, nullptr);
    v.setProperty ("ensembleBank", ensembleBankNumber, nullptr);
    v.setProperty ("ensemble", juce::Base64::toBase64 (ensemble.data.data(), kPatchSize), nullptr);
    v.setProperty ("ensembleSlot", ensembleSourceSlot, nullptr);
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

    auto writeBank = [&v] (const char* type, const std::vector<std::optional<Patch>>& list)
    {
        juce::ValueTree tree (type);
        for (int i = 0; i < (int) list.size(); ++i)
        {
            if (! list[(size_t) i]) continue;
            juce::ValueTree s ("Slot");
            s.setProperty ("index", i, nullptr);
            s.setProperty ("data", juce::Base64::toBase64 (list[(size_t) i]->data.data(), kPatchSize), nullptr);
            tree.appendChild (s, nullptr);
        }
        v.appendChild (tree, nullptr);
    };
    writeBank ("DrumBank", drumSlots);
    writeBank ("EnsembleBank", ensembleSlots);
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
    if (v.hasProperty ("part"))
        part = (Part) juce::jlimit (0, 2, (int) v.getProperty ("part"));
    else
        part = (bool) v.getProperty ("drumMode", false) ? Part::Drums : Part::Voice;
    ensembleBankNumber = juce::jlimit (0, kBanks - 1, (int) v.getProperty ("ensembleBank", 0));
    ensembleSourceSlot = v.getProperty ("ensembleSlot", -1);
    ensembleKnown = false;
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
    if (decode (v.getProperty ("ensemble").toString(), p))
        ensemble = p;

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
    readBank (v.getChildWithName ("EnsembleBank"), ensembleSlots);

    midiLearn->fromValueTree (v.getChildWithName ("MidiLearn"));

    if (auto in = v.getProperty ("midiIn").toString(); in.isNotEmpty())
        device.openInput (in);
    if (auto out = v.getProperty ("midiOut").toString(); out.isNotEmpty())
        device.openOutput (out);
    updateKeyboardChannel();

    fireReplaced();
    drumTarget.replaced();
    ensembleTarget.replaced();
    listeners.call ([] (Listener& l) { l.bankChanged(); });
}

// ---------------------------------------------------------------------------
// Device notifications
// ---------------------------------------------------------------------------
void Session::fmUnsolicitedDump (const sysex::Message& m)
{
    if (m.isEnsemble())
    {
        if (! m.isEnsembleRam() && m.bank == ensembleBankNumber && m.patchNumber < kEnsemblesPerBank)
        {
            ensembleSlots[(size_t) m.patchNumber] = m.patch;
            listeners.call ([] (Listener& l) { l.bankChanged(); });
            setStatus ("Received ensemble " + slotName (m.bank, m.patchNumber, Part::Ensemble));
        }
        else
        {
            ensemble = m.patch;
            ensembleKnown = true;
            ensembleSourceSlot = -1;
            fireEnsembleReplaced();
            setStatus ("Received the ensemble from MIDIbox FM: " + ensembleSummary (m.patch));
        }
        return;
    }
    if (m.isDrums())
    {
        if (! m.isDrumsRam() && m.bank == drumBankNumber && m.patchNumber < kDrumSetsPerBank)
        {
            drumSlots[(size_t) m.patchNumber] = m.patch;
            listeners.call ([] (Listener& l) { l.bankChanged(); });
            setStatus ("Received drum set " + slotName (m.bank, m.patchNumber, Part::Drums));
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
        if (v == editVoice && part == Part::Voice)
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
