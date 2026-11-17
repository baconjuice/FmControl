#include "EnsembleEditor.h"
#include "LookAndFeel.h"

namespace ui
{
namespace
{
const fm::ParamTable& T() { return fm::ensembleParams(); }

// Name of the patch a voice plays, kept up to date (the library or the
// voice may learn it after the ensemble changed).
class PatchNameLabel : public juce::Label,
                       private juce::Timer
{
public:
    PatchNameLabel (Session& s, int v) : session (s), voice (v)
    {
        setFont (ui::font (12.5f, true));
        setColour (juce::Label::textColourId, colours::accent);
        setJustificationType (juce::Justification::centredLeft);
        refresh();
        startTimerHz (4);
    }

private:
    void timerCallback() override { refresh(); }

    void refresh()
    {
        const auto& e = session.getEnsemble();
        auto text = session.storedPatchLabel (fm::ensembleBank (e, voice), fm::ensemblePatch (e, voice));
        if (text.length() <= 4 && session.isVoiceKnown (voice))
            text << "  (" << juce::String (session.voicePatch (voice).name()).trimEnd() << ")";
        if (text != getText())
        {
            setText (text, juce::dontSendNotification);
            setTooltip ("The stored patch voice " + juce::String (voice + 1) + " plays. Changing Bank or Patch loads it "
                        "into the voice right away (unsaved edits of that voice are lost).");
        }
    }

    Session& session;
    int voice;
};

class EnsemblePage : public Page
{
public:
    explicit EnsemblePage (Session& s) : Page (s.getEnsembleTarget(), T())
    {
        hint = &add (makeLabel ("Each voice plays a stored patch on its MIDI channel and only answers notes inside its key range. "
                                "Sending or storing an ensemble makes all four voices reload their stored patches."));
        hint->setMinimumHorizontalScale (0.7f);

        for (int v = 0; v < fm::kVoices; ++v)
        {
            auto id = [v] (const char* p) { return fm::ensembleId (v, p); };
            auto& g = group ("Voice " + juce::String (v + 1));
            g.combo (id ("bank"), "Bank", 56);
            g.knob (id ("patch"), "Patch", [] (int n) { return juce::String (n + 1).paddedLeft ('0', 3); });
            g.knob (id ("channel"), "Channel", [] (int ch) { return juce::String (ch + 1); });
            g.lineBreak();
            g.custom (std::make_unique<PatchNameLabel> (s, v), { 200, 20 });
            g.lineBreak();
            g.knob (id ("keylo"), "Key Low", noteText);
            g.knob (id ("keyhi"), "Key High", noteText);
            g.combo (id ("velocity"), "Velocity", 66);
            g.lineBreak();
            g.knob (id ("volume"), "Volume");
            g.knob (id ("transpose"), "Transpose", signedText);
            g.knob (id ("unisono"), "Unisono", [] (int u) { return u == 0 ? juce::String ("Off") : juce::String (u); });
            g.lineBreak();
            for (auto [pair, label] : { std::pair { "op12", "Op 1+2" }, { "op34", "Op 3+4" } })
            {
                auto& l = g.custom (makeLabel (juce::String (label) + " out"), { 70, 24 });
                l.setJustificationType (juce::Justification::centredLeft);
                for (int o = 1; o <= 4; ++o)
                    g.toggle (id (pair) + ".out" + std::to_string (o), juce::String (o));
                g.lineBreak();
            }
            voices.push_back (&g);
        }

        minContentWidth = 960;
        layout = [this] (int w)
        {
            auto area = juce::Rectangle<int> (0, 0, w, 10000).reduced (8);
            hint->setBounds (area.removeFromTop (20));
            area.removeFromTop (6);
            return Page::columns (voices, fm::kVoices, area, 8) + 8;
        };
    }

private:
    juce::Label* hint = nullptr;
    std::vector<ParamGroup*> voices;
};
} // namespace

EnsembleEditor::EnsembleEditor (Session& s)
{
    addPage ("Ensemble", std::make_unique<EnsemblePage> (s));
}
} // namespace ui
