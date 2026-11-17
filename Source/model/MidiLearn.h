#pragma once

// MIDI learn: controllers (CC) from the keyboard / DAW track mapped to voice
// patch or drum set parameters. A controller maps to one parameter; voice
// mappings act on the edited voice (it follows the voice selector), drum
// mappings on the drum set. Mapped controllers are not passed through to the FM.

#include "device/FmDevice.h"
#include "fm/Params.h"

#include <juce_data_structures/juce_data_structures.h>

#include <optional>
#include <vector>

class Session;
class ParamTarget;

class MidiLearn : private FmDevice::Listener
{
public:
    static constexpr int kVoiceScope = 0;
    static constexpr int kDrumScope = 1;

    struct Mapping
    {
        int channel = 1; // 1..16
        int cc = 0;
        int scope = 0;
        std::string paramId;
        uint8_t writeOption = 0;
    };

    explicit MidiLearn (Session& s);
    ~MidiLearn() override;

    // Wait for the next controller and map it to this parameter.
    void startLearning (int scope, const std::string& paramId, uint8_t writeOption);
    void stopLearning();
    bool isLearning (int scope, const std::string& paramId) const;
    bool isLearning() const { return learnTarget.has_value(); }

    const Mapping* find (int scope, const std::string& paramId) const;
    void forget (int scope, const std::string& paramId);
    void clearAll();
    const std::vector<Mapping>& all() const { return mappings; }

    static juce::String describe (const Mapping& m); // "CC 21, ch 1"

    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree& v);

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void midiLearnChanged() = 0; // mappings or learn state
    };
    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    // Raw value for a 7-bit controller value, spread over the parameter's range.
    static int scaledRaw (const fm::ParamDef& d, int value);

private:
    void fmController (int channel, int cc, int value) override;
    void apply (const Mapping& m, int value);
    void syncDevice();
    void changed();

    Session& session;
    std::vector<Mapping> mappings;
    std::optional<Mapping> learnTarget; // channel / cc filled in when learned
    juce::ListenerList<Listener> listeners;
};
