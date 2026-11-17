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

    // Drum set parameters: own scope, act on the drum set whatever is edited.
    {
        const auto& D = fm::drumParams();
        const int drums = MidiLearn::kDrumScope;
        CHECK (session.getDrumTarget().getMidiLearn() == &ml && session.getDrumTarget().learnScope() == drums);
        ml.startLearning (drums, "bd.freq", 0);
        sendCC (device, 10, 20, 127);
        CHECK (ml.find (drums, "bd.freq") != nullptr && ml.find (scope, "bd.freq") == nullptr);
        CHECK (session.drums().getRaw (D.get ("bd.freq")) == 127);
        sendCC (device, 10, 20, 32);
        CHECK (session.drums().getRaw (D.get ("bd.freq")) == 32);

        // A 4-bit drum value and a toggle get the whole controller range.
        ml.startLearning (drums, "sd.decay", 0);
        sendCC (device, 10, 21, 127);
        CHECK (session.drums().getRaw (D.get ("sd.decay")) == 15);
        ml.startLearning (drums, "tomcym.out3", 0);
        sendCC (device, 10, 22, 100);
        CHECK (session.drums().getRaw (D.get ("tomcym.out3")) == 1);

        // A controller belongs to one parameter: learning it for a voice
        // parameter takes it away from the drum set, and the other way round.
        ml.startLearning (scope, "op2.volume", 0);
        sendCC (device, 10, 20, 0);
        CHECK (ml.find (drums, "bd.freq") == nullptr && ml.find (scope, "op2.volume") != nullptr);
        CHECK (session.drums().getRaw (D.get ("bd.freq")) == 32);

        // Drum mappings survive the state round trip with their scope.
        auto st = session.toValueTree();
        ml.clearAll();
        session.fromValueTree (st);
        CHECK (ml.find (drums, "sd.decay") != nullptr && ml.find (drums, "tomcym.out3") != nullptr);
        CHECK (ml.find (scope, "sd.decay") == nullptr);
    }

    // Ensemble parameters: own scope, act on the ensemble; no DAW slots.
    {
        const auto& E = fm::ensembleParams();
        const int ens = MidiLearn::kEnsembleScope;
        auto& et = session.getEnsembleTarget();
        CHECK (et.getMidiLearn() == &ml && et.learnScope() == ens && et.getAutomation() == nullptr);
        ml.startLearning (ens, "v2.volume", 0);
        sendCC (device, 1, 30, 40);
        CHECK (ml.find (ens, "v2.volume") != nullptr);
        CHECK (session.getEnsemble().getRaw (E.get ("v2.volume")) == 40);
        ml.startLearning (ens, "v4.transpose", 0);
        sendCC (device, 1, 31, 0);
        CHECK (session.getEnsemble().getRaw (E.get ("v4.transpose")) == 0); // -64
        ml.startLearning (ens, "v1.velocity", 0);
        sendCC (device, 1, 32, 127);
        CHECK (session.getEnsemble().getRaw (E.get ("v1.velocity")) == 2); // Soft, never the unused 3
        ml.startLearning (ens, "v3.channel", 0);
        sendCC (device, 1, 33, 127);
        CHECK (session.voiceChannel (2) == 16);

        auto st = session.toValueTree();
        ml.clearAll();
        session.fromValueTree (st);
        CHECK (ml.find (ens, "v2.volume") != nullptr && ml.find (ens, "v3.channel") != nullptr);
        CHECK (ml.find (MidiLearn::kDrumScope, "sd.decay") != nullptr);
    }

    std::printf (failures == 0 ? "learn tests passed\n" : "%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
