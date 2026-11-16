#pragma once

// Editor state: the patches of the box's four voices (one is edited at a
// time), the bank of 128 patches shown in the librarian, and the operations
// that move patches between them, files and the MIDIbox FM.

#include "ParamTarget.h"
#include "device/FmDevice.h"
#include "fm/Params.h"
#include "fm/SyxFile.h"

#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <optional>
#include <random>

class MidiLearn;

class Session : public ParamTarget,
                private FmDevice::Listener
{
public:
    static constexpr int kSlots = fm::kPatchesPerBank;

    explicit Session (FmDevice& d);
    ~Session() override;

    FmDevice& device;

    // ---- voices / edit buffer --------------------------------------------
    const fm::Patch& edit() const { return voices[(size_t) editVoice]; }
    int getEditVoice() const { return editVoice; }
    void setEditVoice (int v);              // 0..3; fetches its patch from the box the first time
    int voiceChannel (int v) const { return v + 1; } // MIDI channel of a voice (default ensemble)

    void setParam (const fm::ParamDef& p, int raw, uint8_t writeOption = 0) override;
    int getParam (const fm::ParamDef& p) const override { return edit().getRaw (p); }
    uint8_t getByte (int addr) const override { return edit().byte (addr); }
    int defaultRaw (const fm::ParamDef& p) const override { return fm::Patch::makeInit().getRaw (p); }
    void setName (const juce::String& name);

    // Replace the edited voice's patch (sends it to the box when live).
    void loadIntoEditor (const fm::Patch& p, int fromSlot = -1);
    int editorSourceSlot() const { return sourceSlot[(size_t) editVoice]; }

    bool isLive() const { return live; }
    void setLive (bool shouldSendEdits) { live = shouldSendEdits; }

    void sendEditBuffer();
    void fetchEditBuffer();
    void storeVoiceOnBox();             // the box's own "store" for the edited voice
    void playNote (bool on);            // Play button: middle C on the edited voice's channel
    void allNotesOff();
    void ping();

    // Keyboard play-through follows the edited voice (instead of a fixed channel).
    bool keysFollowVoice() const { return followVoice; }
    void setKeysFollowVoice (bool f);

    // ---- bank / librarian -------------------------------------------------
    const std::optional<fm::Patch>& slot (int i) const { return slots[(size_t) i]; }
    void setSlot (int i, std::optional<fm::Patch> p);
    int getBankNumber() const { return bankNumber; }
    void setBankNumber (int b);

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

    static juce::String slotName (int bank, int slot); // "B012"

private:
    void fmUnsolicitedDump (const fm::sysex::Message&) override;
    void fmLog (const juce::String&) override;

    fm::Patch& editRef() { return voices[(size_t) editVoice]; }
    void fireReplaced();
    void fireChanged (const std::vector<uint16_t>& addresses);
    void fetchNext (int i, int endExclusive, int got, int failed);
    void writeNext (std::vector<int> remaining, int done, int failed, int total);
    void updateKeyboardChannel();

    std::array<fm::Patch, fm::kVoices> voices;
    std::array<bool, fm::kVoices> voiceKnown {};   // fetched from / sent to the box
    std::array<int, fm::kVoices> sourceSlot { -1, -1, -1, -1 };
    int editVoice = 0;
    bool live = true;
    bool followVoice = true;
    int playingNoteChannel = 0;

    std::array<std::optional<fm::Patch>, kSlots> slots;
    int bankNumber = 0;

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
