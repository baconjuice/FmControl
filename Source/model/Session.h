#pragma once

// Editor state: the patches of the box's four voices (one is edited at a
// time), its drum set and its ensemble, the banks shown in the librarian
// (128 voice patches, 16 drum sets or 32 ensembles), and the operations that
// move them between the editor, files and the MIDIbox FM. The edited "part"
// (a voice, the drum set or the ensemble) decides what the library, the
// header buttons and the edit-buffer actions work on.

#include "ParamTarget.h"
#include "device/FmDevice.h"
#include "fm/Drums.h"
#include "fm/Ensemble.h"
#include "fm/Params.h"
#include "fm/SyxFile.h"

#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <optional>
#include <random>

class MidiLearn;
class Session;

// The drum set as a ParamTarget, so the drum editor's controls bind to it.
// No MIDI learn or DAW slots for drums (yet).
class DrumTarget : public ParamTarget
{
public:
    explicit DrumTarget (Session& s) : session (s) {}
    uint8_t getByte (int addr) const override;
    int getParam (const fm::ParamDef& p) const override;
    void setParam (const fm::ParamDef& p, int raw, uint8_t writeOption = 0) override;
    int defaultRaw (const fm::ParamDef& p) const override;
    void replaced() { notifyReplaced(); }
    void changed (const std::vector<uint16_t>& a) { notifyChanged (a); }

private:
    Session& session;
};

// The ensemble as a ParamTarget, for the ensemble editor.
class EnsembleTarget : public ParamTarget
{
public:
    explicit EnsembleTarget (Session& s) : session (s) {}
    uint8_t getByte (int addr) const override;
    int getParam (const fm::ParamDef& p) const override;
    void setParam (const fm::ParamDef& p, int raw, uint8_t writeOption = 0) override;
    int defaultRaw (const fm::ParamDef& p) const override;
    void replaced() { notifyReplaced(); }
    void changed (const std::vector<uint16_t>& a) { notifyChanged (a); }

private:
    Session& session;
};

class Session : public ParamTarget,
                private FmDevice::Listener
{
public:
    static constexpr int kSlots = fm::kPatchesPerBank;
    enum class Part { Voice, Drums, Ensemble };

    explicit Session (FmDevice& d);
    ~Session() override;

    FmDevice& device;

    // ---- voices / edit buffer --------------------------------------------
    const fm::Patch& edit() const { return voices[(size_t) editVoice]; }
    int getEditVoice() const { return editVoice; }
    void setEditVoice (int v);              // 0..3; fetches its patch from the box the first time
    int voiceChannel (int v) const { return fm::ensembleChannel (ensemble, v); } // 1..16, from the ensemble
    bool isVoiceKnown (int v) const { return voiceKnown[(size_t) (v & 3)]; }
    const fm::Patch& voicePatch (int v) const { return voices[(size_t) (v & 3)]; }

    void setParam (const fm::ParamDef& p, int raw, uint8_t writeOption = 0) override;
    int getParam (const fm::ParamDef& p) const override { return edit().getRaw (p); }
    uint8_t getByte (int addr) const override { return edit().byte (addr); }
    int defaultRaw (const fm::ParamDef& p) const override { return fm::Patch::makeInit().getRaw (p); }
    void setName (const juce::String& name);
    int automationScope() const override { return editVoice; }

    // Parameters of any voice (DAW automation drives voices that may not be
    // the one being edited).
    int getVoiceParam (int voice, const fm::ParamDef& p) const { return voices[(size_t) (voice & 3)].getRaw (p); }
    void setVoiceParam (int voice, const fm::ParamDef& p, int raw);

    // ---- drum set ---------------------------------------------------------
    Part getPart() const { return part; }
    bool isDrumMode() const { return part == Part::Drums; }
    bool isEnsembleMode() const { return part == Part::Ensemble; }
    void setDrumMode (bool editDrums);      // fetches the playing set from the box the first time
    const fm::Patch& drums() const { return drumKit; }
    DrumTarget& getDrumTarget() { return drumTarget; }
    void setDrumParam (const fm::ParamDef& p, int raw);
    void playDrumBeat();                    // one bar on the drum channel, using the kit's key ranges

    // ---- ensemble ----------------------------------------------------------
    void setEnsembleMode (bool editEnsemble); // fetches the playing ensemble the first time
    const fm::Patch& getEnsemble() const { return ensemble; }
    EnsembleTarget& getEnsembleTarget() { return ensembleTarget; }
    void setEnsembleParam (const fm::ParamDef& p, int raw);
    // "A005 Rhodes Piano" for a stored patch when the library knows its name, else "A005".
    juce::String storedPatchLabel (int bank, int patch) const;

    // What the header and library work on: the edited voice, the drum set or the ensemble.
    const fm::Patch& current() const;

    // Replace the edited voice's patch or the drum set (sends it to the box when live).
    void loadIntoEditor (const fm::Patch& p, int fromSlot = -1);
    int editorSourceSlot() const;

    bool isLive() const { return live; }
    void setLive (bool shouldSendEdits) { live = shouldSendEdits; }

    void sendEditBuffer();
    void fetchEditBuffer();

    // Random generator (like the box's RND menu) for the edited voice. Keeps
    // the previous patches per voice so a roll can be undone.
    void randomize (const std::string& section, float amount);
    bool canUndoRandom() const { return ! historyRef().empty(); }
    void undoRandom();
    void storeVoiceOnBox();             // the box's own "store" for the edited voice
    void playNote (bool on);            // Play button: C3 on the edited voice's channel (ensemble: every voice; drums: a beat)
    void allNotesOff();
    void ping();

    // Keyboard play-through follows the edited voice (instead of a fixed channel).
    bool keysFollowVoice() const { return followVoice; }
    void setKeysFollowVoice (bool f);

    // ---- bank / librarian (voice patches, or drum sets in drum mode) ------
    int slotCount() const { return (int) slotsFor (part).size(); }
    const std::optional<fm::Patch>& slot (int i) const { return slotsFor (part)[(size_t) i]; }
    void setSlot (int i, std::optional<fm::Patch> p);
    int getBankNumber() const { return bankFor (part); }
    void setBankNumber (int b);
    juce::String currentSlotName (int slot) const { return slotName (getBankNumber(), slot, part); }

    void fetchSlot (int i, bool thenEdit);
    void fetchBank();
    void storeEditToSlot (int i);
    void writeBankToDevice();
    void cancelTransfers();
    bool isBusy() const { return busy; }

    bool loadSyx (const juce::File& f, juce::String& message);
    bool saveBankSyx (const juce::File& f, juce::String& message) const;
    bool savePatchSyx (const juce::File& f, juce::String& message) const;

    // ---- state (plugin project / app settings) ---------------------------
    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree& v);

    // ---- notifications ----------------------------------------------------
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void editReplaced() {}
        virtual void editChanged (const std::vector<uint16_t>& addresses) { juce::ignoreUnused (addresses); }
        virtual void bankChanged() {}
        virtual void statusChanged() {}
    };
    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    MidiLearn* getMidiLearn() override { return midiLearn.get(); }
    int learnScope() const override { return 0; }
    AutomationLink* getAutomation() override { return automation; }
    void setAutomation (AutomationLink* a) { automation = a; }

    juce::String getStatus() const { return status; }
    void setStatus (const juce::String& s, float newProgress = -1.0f);
    float getProgress() const { return progress; }
    const juce::StringArray& getLog() const { return logLines; }

    static juce::String slotName (int bank, int slot, Part p = Part::Voice); // "B012", drum set / ensemble "B03"
    static juce::String partNoun (Part p, bool plural = false);            // "patch", "drum sets", ...

private:
    void fmUnsolicitedDump (const fm::sysex::Message&) override;
    void fmLog (const juce::String&) override;

    fm::Patch& editRef() { return voices[(size_t) editVoice]; }
    fm::Patch& currentRef();
    std::vector<fm::Patch>& historyRef();
    const std::vector<fm::Patch>& historyRef() const;
    std::vector<std::optional<fm::Patch>>& slotsFor (Part p);
    const std::vector<std::optional<fm::Patch>>& slotsFor (Part p) const;
    int& bankRef (Part p);
    int bankFor (Part p) const;
    int& sourceSlotRef();
    fm::sysex::Bytes requestSlot (Part p, int bank, int i) const;
    fm::sysex::Bytes writeSlot (Part p, int bank, int i, const fm::Patch& patch) const;
    juce::String partName() const; // "voice 2" / "drum set" / "ensemble"
    void setPart (Part p);
    void fireEnsembleReplaced();
    void fetchEnsemble (bool quiet = false);
    void sendEnsemble();
    // After the box loaded an ensemble it plays the stored patches and drum
    // set again: read them back.
    void rereadVoicesAndDrums();
    void loadVoicePatchSoon (int voice); // after a bank/patch change in the ensemble
    void finishEnsembleTransfer (const juce::String& message);
    // Reading a stored voice patch also loads it into voice 1 on the box and
    // points the ensemble at it: remember voice 1 first, put it back after.
    void saveVoice1Then (std::function<void()> next);
    void restoreVoice1 (bool patchToo);
    void fetchSlotNow (int i, bool thenEdit); // put the saved ensemble back after a bank fetch/write
    void fireDrumsReplaced();
    void fetchDrums();
    void sendDrums();
    void fireReplaced();
    void fireChanged (const std::vector<uint16_t>& addresses);
    void fetchNext (Part p, int i, int endExclusive, int got, int failed);
    void writeNext (Part p, std::vector<int> remaining, int done, int failed, int total);
    void updateKeyboardChannel();
    void sendEditBufferSoon(); // quick repeated rolls only send the last result

    std::array<fm::Patch, fm::kVoices> voices;
    std::array<bool, fm::kVoices> voiceKnown {};   // fetched from / sent to the box
    std::array<int, fm::kVoices> sourceSlot { -1, -1, -1, -1 };
    int editVoice = 0;
    bool live = true;
    bool followVoice = true;
    std::vector<int> playingChannels; // Play button notes held
    std::array<std::vector<fm::Patch>, fm::kVoices> randomHistory;
    std::mt19937 rng { std::random_device{}() };
    int sendGeneration = 0;

    Part part = Part::Voice;
    fm::Patch drumKit = fm::makeInitDrums();
    bool drumKnown = false;
    int drumSourceSlot = -1;
    std::vector<fm::Patch> drumHistory;
    DrumTarget drumTarget { *this };
    struct BeatPlayer;
    std::unique_ptr<BeatPlayer> beat;

    std::vector<std::optional<fm::Patch>> slots = std::vector<std::optional<fm::Patch>> (kSlots);
    fm::Patch ensemble = fm::makeInitEnsemble();
    bool ensembleKnown = false;
    int ensembleSourceSlot = -1;
    std::vector<fm::Patch> noHistory; // the ensemble has no random generator
    std::optional<fm::Patch> ensembleBeforeTransfer;
    std::optional<fm::Patch> voice1Before;
    std::optional<std::pair<int, int>> voice1SlotBefore; // ensemble bank / patch of voice 1
    std::array<int, fm::kVoices> patchLoadGeneration {};
    EnsembleTarget ensembleTarget { *this };

    std::vector<std::optional<fm::Patch>> drumSlots = std::vector<std::optional<fm::Patch>> (fm::kDrumSetsPerBank);
    std::vector<std::optional<fm::Patch>> ensembleSlots = std::vector<std::optional<fm::Patch>> (fm::kEnsemblesPerBank);
    int bankNumber = 0, drumBankNumber = 0, ensembleBankNumber = 0;

    bool busy = false;
    bool cancelRequested = false;
    juce::String status;
    float progress = -1.0f;
    juce::StringArray logLines;

    juce::ListenerList<Listener> listeners;
    std::unique_ptr<MidiLearn> midiLearn;
    AutomationLink* automation = nullptr;
    JUCE_DECLARE_WEAK_REFERENCEABLE (Session)
};
