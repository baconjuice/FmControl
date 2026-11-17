#include "DrumEditor.h"
#include "LookAndFeel.h"

#include <map>

namespace ui
{
namespace
{
const fm::ParamTable& T() { return fm::drumParams(); }

// Envelope of one rhythm instrument. Drums have no sustain phase: after the
// decay they fade out at the release rate.
class DrumEnvelopeView : public juce::Component,
                         private ParamTarget::Listener
{
public:
    DrumEnvelopeView (ParamTarget& t, std::string instrument) : target (t), p (std::move (instrument))
    {
        target.addTargetListener (this);
    }
    ~DrumEnvelopeView() override { target.removeTargetListener (this); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        drawScope (g, r);
        r = r.reduced (5.0f, 5.0f);
        auto len = [] (int rate) { return 0.04f + (float) rate / 15.0f; };
        const float a = len (v (".attack")), d = len (v (".decay")), rel = len (v (".release"));
        const float s = (float) v (".sustain") / 15.0f;
        const float total = a + d + rel;
        drawCurve (g, r, { { 0, 0 }, { a / total, 1 }, { (a + d) / total, s }, { 1.0f, 0.0f } }, colours::accent);
    }

private:
    void targetReplaced() override { repaint(); }
    void targetChanged (const std::vector<uint16_t>&) override { repaint(); }
    int v (const char* suffix) const { return target.getParam (T().get (p + suffix)); }

    ParamTarget& target;
    std::string p;
};

class DrumSetPage : public Page
{
public:
    explicit DrumSetPage (Session& s) : Page (s.getDrumTarget(), T())
    {
        auto& drums = s.getDrumTarget();

        hint = &add (makeLabel ("The drum set plays on MIDI channel " + juce::String (fm::kDrumChannel)
                                + ". Each drum sounds on the notes of its key range; "
                                  "Program Change on channel 16 picks a stored set (bank x 16 + set)."));

        for (auto& inst : fm::drumInstruments())
        {
            const std::string p = inst.id;
            const bool modulator = p == "bdm";
            auto& g = group (inst.name);
            g.knobWidth = 50;
            g.combo (p + ".wave", "Waveform", 112);
            g.lineBreak();
            g.knob (p + ".mult", "Multiplier", multText);
            g.knob (p + ".volume", modulator ? "Mod Level" : "Volume");
            g.lineBreak();
            g.knob (p + ".attack", "Attack");
            g.knob (p + ".decay", "Decay");
            g.lineBreak();
            g.knob (p + ".sustain", "Sustain");
            g.knob (p + ".release", "Release");
            instruments.push_back (&g);
            envs.push_back (&add (std::make_unique<DrumEnvelopeView> (drums, p)));
        }

        auto outputs = [] (ParamGroup& g, const std::string& ch)
        {
            for (int o = 1; o <= 4; ++o)
                g.toggle (ch + ".out" + std::to_string (o), "Out " + juce::String (o));
        };

        auto& bd = group ("Bass Drum");
        bd.knob ("bd.freq", "Pitch");
        bd.knob ("bd.decr", "Pitch Drop");
        bd.knob ("bd.feedback", "Feedback");
        bd.combo ("bd.am", "Mode", 70);
        bd.lineBreak();
        outputs (bd, "bd");
        channels.push_back (&bd);

        auto& hh = group ("Hi-Hats / Snare");
        hh.knob ("hh.freq", "HH Pitch");
        hh.knob ("hh.decr", "HH Drop");
        hh.knob ("hhsd.feedback", "Feedback");
        hh.lineBreak();
        outputs (hh, "hhsd");
        channels.push_back (&hh);

        auto& tc = group ("Tom / Cymbal");
        tc.knob ("tom.freq", "Tom Pitch");
        tc.knob ("tom.decr", "Tom Drop");
        tc.knob ("tomcym.feedback", "Feedback");
        tc.lineBreak();
        outputs (tc, "tomcym");
        channels.push_back (&tc);

        // Key ranges: the notes (on channel 16) that play each drum.
        keys = &group ("Key Ranges");
        for (auto& k : fm::drumKeys())
        {
            const std::string id = k.id;
            static const std::map<std::string, juce::String> shortName {
                { "bd", "BD" }, { "hho", "HH Open" }, { "hhc", "HH Closed" }, { "sd", "Snare" }, { "tom", "Tom" }, { "cym", "Cymbal" }
            };
            keys->knob (id + ".keylo", shortName.at (id) + " Lo", noteText);
            keys->knob (id + ".keyhi", shortName.at (id) + " Hi", noteText);
            keys->gap (10);
        }

        minContentWidth = 1000;
        layout = [this] (int w)
        {
            auto area = juce::Rectangle<int> (0, 0, w, 10000).reduced (8);
            hint->setBounds (area.removeFromTop (20));
            area.removeFromTop (6);

            const int gap = 8, n = (int) instruments.size();
            const int colW = (area.getWidth() - (n - 1) * gap) / n;
            int h = 0;
            for (auto* g : instruments) h = juce::jmax (h, g->preferredHeight (colW));
            for (int i = 0; i < n; ++i)
            {
                const int x = area.getX() + i * (colW + gap);
                envs[(size_t) i]->setBounds (x, area.getY(), colW, 56);
                instruments[(size_t) i]->setBounds (x, area.getY() + 62, colW, h);
            }
            area.removeFromTop (62 + h + gap);
            const int y = Page::columns (channels, 3, area, gap);
            keys->setBounds (area.getX(), y, area.getWidth(), keys->preferredHeight (area.getWidth()));
            return keys->getBottom() + gap;
        };
    }

private:
    juce::Label* hint = nullptr;
    std::vector<ParamGroup*> instruments, channels;
    ParamGroup* keys = nullptr;
    std::vector<DrumEnvelopeView*> envs;
};
} // namespace

DrumEditor::DrumEditor (Session& s)
{
    addPage ("Drum Set", std::make_unique<DrumSetPage> (s));
}
} // namespace ui
