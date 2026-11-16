// DAW automation slots: host -> patch, patch -> host, voices, state.

#include "model/Session.h"
#include "plugin/Automation.h"

#include <cstdio>

static int failures = 0;
#define CHECK(c) do { if (! (c)) { std::printf ("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

struct DummyProcessor : juce::AudioProcessor
{
    const juce::String getName() const override { return "test"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
};


static void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil (60); }

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    FmDevice device;
    Session session (device);
    session.setLive (false); // no MIDI port in tests
    DummyProcessor proc;
    Automation automation (proc, session);
    auto& params = proc.getParameters();
    CHECK (params.size() == Automation::kSlots);

    // Every default resolves to a real parameter.
    for (auto& a : Automation::defaults())
        CHECK (a.paramId.empty() || fm::voiceParams().find (a.paramId) != nullptr);

    const auto& T = fm::voiceParams();
    CHECK (params[0]->getName (100) == "1: V1 OP1 Volume");
    CHECK (params[4]->getName (100) == "5: V1 Feedback");
    CHECK (params[14]->getName (100) == "15: V1 WT Rate");

    // Host -> patch.
    params[0]->setValue (1.0f);
    pump();
    CHECK (session.getParam (T.get ("op1.volume")) == 63);
    params[0]->setValue (0.0f);
    pump();
    CHECK (session.getParam (T.get ("op1.volume")) == 0);

    // Patch -> host (editor edit).
    session.setParam (T.get ("feedback"), 7);
    CHECK (std::abs (params[4]->getValue() - 1.0f) < 1.0e-4f);

    // A slot on voice 3 drives voice 3 even while voice 1 is edited.
    automation.assign (5, 2, "algorithm", 0);
    CHECK (params[5]->getName (100) == "6: V3 Algorithm");
    CHECK (automation.slotFor (2, "algorithm") == 5);
    CHECK (automation.slotFor (0, "algorithm") == -1);
    CHECK (automation.slotFor (0, "op1.mult") == -1); // slot 6 was replaced
    params[5]->setValue (1.0f);
    pump();
    CHECK (session.getVoiceParam (2, T.get ("algorithm")) == 3);
    CHECK (session.getParam (T.get ("algorithm")) != 3 || session.getVoiceParam (0, T.get ("algorithm")) == 0);
    CHECK (session.getEditVoice() == 0);

    // State round trip.
    auto state = automation.toValueTree();
    automation.resetToDefaults (0);
    CHECK (automation.slotFor (2, "algorithm") == -1);
    automation.fromValueTree (state);
    CHECK (automation.slotFor (2, "algorithm") == 5);

    // Unassign.
    automation.unassign (2, "algorithm");
    CHECK (params[5]->getName (100) == "6: (unused)");

    std::printf (failures == 0 ? "All automation tests passed\n" : "%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
