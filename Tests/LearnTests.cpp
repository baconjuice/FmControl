// MIDI learn end to end: controllers go through FmDevice::forwardMidi (the
// play-through path), get mapped and change the edited voice. No ports needed.

#include "device/FmDevice.h"
#include "model/MidiLearn.h"
#include "model/Session.h"

#include <cstdio>

static int failures = 0;
#define CHECK(c) do { if (! (c)) { std::printf ("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static void sendCC (FmDevice& d, int ch, int cc, int value)
{
    juce::MidiBuffer b;
    b.addEvent (juce::MidiMessage::controllerEvent (ch, cc, value), 0);
    d.forwardMidi (b);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    FmDevice device;
    Session session (device);
    session.setLive (false);
    auto& ml = *session.getMidiLearn();
    const auto& T = fm::voiceParams();
    const auto& vol = T.get ("op1.volume");
    const int scope = MidiLearn::kVoiceScope;

    // Not learning, nothing mapped: CC is ignored by the editor.
    const int before = session.getParam (vol);
    sendCC (device, 1, 21, 0);
    CHECK (session.getParam (vol) == before);

    // Learn: first CC that arrives gets mapped and applied.
    ml.startLearning (scope, "op1.volume", 0);
    CHECK (device.isLearning());
    sendCC (device, 3, 74, 127);
    CHECK (! device.isLearning());
    const auto* m = ml.find (scope, "op1.volume");
    CHECK (m != nullptr && m->channel == 3 && m->cc == 74);
    CHECK (session.getParam (vol) == 63);
    sendCC (device, 3, 74, 0);
    CHECK (session.getParam (vol) == 0);
    sendCC (device, 1, 74, 100); // other channel: not mapped
    CHECK (session.getParam (vol) == 0);

    // Choices spread over the controller range.
    const auto& algo = T.get ("algorithm");
    CHECK (MidiLearn::scaledRaw (algo, 0) == 0 && MidiLearn::scaledRaw (algo, 127) == 3);

    // The mapping follows the edited voice.
    session.setEditVoice (2);
    sendCC (device, 3, 74, 127);
    CHECK (session.getVoiceParam (2, vol) == 63);
    CHECK (session.getVoiceParam (0, vol) == 0);
    session.setEditVoice (0);

    // Re-learning the same CC for another parameter moves it.
    ml.startLearning (scope, "feedback", 0);
    sendCC (device, 3, 74, 127);
    CHECK (ml.find (scope, "op1.volume") == nullptr);
    CHECK (ml.find (scope, "feedback") != nullptr);
    CHECK (session.getParam (T.get ("feedback")) == 7);

    // State round trip.
    auto state = session.toValueTree();
    ml.clearAll();
    CHECK (ml.all().empty());
    session.fromValueTree (state);
    CHECK (ml.all().size() == 1);

    // Cancel learning.
    ml.startLearning (scope, "op1.volume", 0);
    ml.stopLearning();
    CHECK (! device.isLearning());

    std::printf (failures == 0 ? "learn tests passed\n" : "%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
