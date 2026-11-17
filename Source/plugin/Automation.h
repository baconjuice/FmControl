#pragma once

// DAW automation: 24 host parameters ("slots"). Slots 1-16 each drive one
// parameter of one of the FM's four voices; defaults cover voice 1 (operator
// volumes, feedback, multipliers, LFO and EG5 rates, wavetable rate, ...).
// Slots 17-24 drive drum set parameters (defaults: bass drum, hi-hat and tom
// pitch, decays). Right-click any control to put it on a slot. Host changes are
// applied on the message thread through the Session, so they reach the FM like
// any other live edit; edits made in the editor are reported back to the host
// so they can be recorded.

#include "model/AutomationLink.h"
#include "model/Session.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

class Automation : public AutomationLink,
                   private Session::Listener,
                   private ParamTarget::Listener,
                   private juce::Timer
{
public:
    static constexpr int kVoiceSlots = 16;
    static constexpr int kDrumSlots = 8;
    static constexpr int kSlots = kVoiceSlots + kDrumSlots;
    static constexpr int kDrums = fm::kDrumPart; // "voice" of the drum slots

    // Creates the host parameters and adds them to the processor.
    Automation (juce::AudioProcessor& processor, Session& session);
    ~Automation() override;

    // AutomationLink (scope = voice 0..3, or kDrums)
    int numSlots() const override { return kSlots; }
    bool slotAccepts (int slot, int voice) const override { return isDrumSlot (slot) == (voice == kDrums); }
    static bool isDrumSlot (int slot) { return slot >= kVoiceSlots; }
    int slotFor (int voice, const std::string& paramId) const override;
    juce::String slotLabel (int slot) const override;
    void assign (int slot, int voice, const std::string& paramId, uint8_t writeOption) override;
    void unassign (int voice, const std::string& paramId) override;
    void resetToDefaults (int voice) override;
    void beginGesture (int voice, const std::string& paramId) override;
    void endGesture (int voice, const std::string& paramId) override;

    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree& v);

    struct Assignment
    {
        int voice = 0;       // 0..3, kDrums for the drum slots
        std::string paramId; // empty = unused
    };
    static std::array<Assignment, kSlots> defaults();

private:
    class SlotParameter;
    friend class SlotParameter;

    void editReplaced() override;
    void editChanged (const std::vector<uint16_t>& addresses) override;
    void targetReplaced() override { editReplaced(); }                // drum set
    void targetChanged (const std::vector<uint16_t>& addresses) override; // drum set
    void changedFor (int voice, const std::vector<uint16_t>& addresses);
    int currentRaw (const Assignment& a, const fm::ParamDef& d) const;
    void timerCallback() override;

    Assignment assignment (int slot) const;
    const fm::ParamDef* defFor (int slot) const;
    void pushToHost (int slot);
    juce::String valueText (int slot, float normalisedValue) const;
    void assignmentsChanged();

    juce::AudioProcessor& processor;
    Session& session;
    std::array<SlotParameter*, kSlots> params {};
    std::array<Assignment, kSlots> slots;
    mutable juce::CriticalSection lock; // slots are read by the host's threads
    bool applyingFromHost = false;
};
