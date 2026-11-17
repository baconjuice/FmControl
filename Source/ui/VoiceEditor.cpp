#include "VoiceEditor.h"
#include "LookAndFeel.h"
#include "PixelFont.h"

namespace ui
{
namespace
{
const fm::ParamTable& T() { return fm::voiceParams(); }

// Which operators reach the output for each algorithm (OP1..OP4 = bits 0..3).
int carrierMask (int algorithm)
{
    static const int masks[] = { 0b1000, 0b1001, 0b1010, 0b1101 };
    return masks[algorithm & 3];
}

class Listening : public juce::Component,
                  protected ParamTarget::Listener
{
public:
    explicit Listening (ParamTarget& t) : target (t) { target.addTargetListener (this); }
    ~Listening() override { target.removeTargetListener (this); }

protected:
    void targetReplaced() override { repaint(); }
    void targetChanged (const std::vector<uint16_t>&) override { repaint(); }
    int v (const std::string& id) const { return target.getParam (T().get (id)); }
    ParamTarget& target;
};

// ---------------------------------------------------------------------------
// Operators
// ---------------------------------------------------------------------------

// The four operators and how they connect, with OP1's feedback loop.
class AlgorithmView : public Listening
{
public:
    using Listening::Listening;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        drawScope (g, r);

        const int algo = v ("algorithm");
        // Chains of operators (0-based) that are summed at the output.
        static const std::vector<std::vector<int>> chains[] = {
            { { 0, 1, 2, 3 } }, { { 0 }, { 1, 2, 3 } }, { { 0, 1 }, { 2, 3 } }, { { 0 }, { 1, 2 }, { 3 } } };
        const auto& cs = chains[algo & 3];

        const float boxW = 46.0f, boxH = 26.0f, gapX = 22.0f, gapY = 10.0f;
        const float totalH = (float) cs.size() * boxH + (float) (cs.size() - 1) * gapY;
        float y = r.getCentreY() - totalH / 2.0f;
        const float outX = r.getRight() - 40.0f;
        const float startX = r.getX() + 24.0f;
        const float busX = outX - 14.0f;
        const int fb = v ("feedback");

        g.setFont (font (12.5f, true));
        for (auto& chain : cs)
        {
            float x = startX + (float) (4 - chain.size()) * (boxW + gapX);
            for (size_t i = 0; i < chain.size(); ++i)
            {
                const int op = chain[i];
                juce::Rectangle<float> box (x, y, boxW, boxH);
                const bool carrier = i == chain.size() - 1;
                g.setColour (colours::panelEdge.darker (0.6f));
                g.fillRect (box.translated (3.0f, 3.0f));
                g.setColour (carrier ? colours::accent2 : colours::text);
                g.fillRect (box);
                g.setColour (colours::onAccent);
                pixel::draw (g, "OP" + juce::String (op + 1), box, 2.0f, juce::Justification::centred);

                if (op == 0 && fb > 0) // feedback loop over OP1
                {
                    juce::Path loop;
                    loop.startNewSubPath (box.getRight() - 6, box.getY());
                    loop.quadraticTo (box.getCentreX(), box.getY() - 14, box.getX() + 6, box.getY());
                    g.setColour (colours::accent2);
                    g.strokePath (loop, juce::PathStrokeType (1.5f));
                    g.setFont (font (10.5f));
                    g.drawText ("fb " + juce::String (fb), box.translated (0, -24).withHeight (12), juce::Justification::centred);
                    g.setFont (font (12.5f, true));
                }

                const float nextX = x + boxW + gapX;
                g.setColour (colours::dimText);
                if (! carrier)
                    g.drawArrow ({ box.getRight(), box.getCentreY(), nextX, box.getCentreY() }, 1.5f, 7.0f, 6.0f);
                else
                    g.drawLine (box.getRight(), box.getCentreY(), busX, box.getCentreY(), 1.5f);
                x = nextX;
            }
            y += boxH + gapY;
        }
        // Output bus.
        const float top = r.getCentreY() - totalH / 2.0f + boxH / 2.0f, bottom = r.getCentreY() + totalH / 2.0f - boxH / 2.0f;
        g.setColour (colours::dimText);
        g.drawLine (busX, top, busX, bottom, 1.5f);
        g.drawArrow ({ busX, r.getCentreY(), outX + 6.0f, r.getCentreY() }, 1.5f, 7.0f, 6.0f);
        g.setFont (font (11.0f));
        g.drawText ("out", juce::Rectangle<float> (outX + 8.0f, r.getCentreY() - 8.0f, 30.0f, 16.0f), juce::Justification::centredLeft);
    }
};

// OPL envelope of one operator. Values are times (0 = fastest), sustain is a level.
class OpEnvelopeView : public Listening
{
public:
    OpEnvelopeView (ParamTarget& t, std::string op) : Listening (t), p (std::move (op)) {}

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        drawScope (g, r);
        r = r.reduced (6.0f, 6.0f);

        auto len = [] (int rate) { return 0.04f + (float) rate / 15.0f; };
        const float a = len (v (p + ".attack")), d = len (v (p + ".decay")), rel = len (v (p + ".release"));
        const float s = (float) v (p + ".sustain") / 15.0f;
        const bool hold = v (p + ".egt") != 0; // without EGT the sound decays straight on
        const float hold_ = hold ? 0.6f : 0.0f;
        const float total = a + d + hold_ + rel;
        std::vector<juce::Point<float>> pts { { 0, 0 }, { a / total, 1 }, { (a + d) / total, s } };
        if (hold)
            pts.push_back ({ (a + d + hold_) / total, s });
        pts.push_back ({ 1.0f, 0.0f });
        drawCurve (g, r, pts, colours::accent);
        if (! hold)
        {
            g.setColour (colours::dimText);
            g.setFont (font (10.5f));
            g.drawText ("no sustain", r.removeFromTop (12), juce::Justification::topRight);
        }
    }

private:
    std::string p;
};

// Title of an operator group: "OP2 · Carrier" depending on the algorithm.
class OperatorsPage : public Page,
                      private ParamTarget::Listener
{
public:
    explicit OperatorsPage (Session& s) : Page (s, T())
    {
        auto& algo = group ("Algorithm");
        algo.combo ("algorithm", "Connection", 130);
        algo.knob ("feedback", "OP1 Feedback");
        algoGroup = &algo;
        algoView = &add (std::make_unique<AlgorithmView> (s));

        for (int o = 1; o <= 4; ++o)
        {
            const std::string p = "op" + std::to_string (o);
            auto& g = group ("OP" + juce::String (o));
            g.knobWidth = 52;
            g.combo (p + ".wave", "Waveform", 104);
            g.knob (p + ".mult", "Multiplier", multText);
            g.knob (p + ".volume", "Volume");
            g.lineBreak();
            g.knob (p + ".attack", "Attack");
            g.knob (p + ".decay", "Decay");
            g.knob (p + ".sustain", "Sustain");
            g.knob (p + ".release", "Release");
            g.lineBreak();
            g.toggle (p + ".egt", "Sustain");
            g.toggle (p + ".vib", "Vibrato");
            g.toggle (p + ".trem", "Tremolo");
            g.toggle (p + ".ksr", "KSR");
            g.lineBreak();
            g.combo (p + ".ksl", "Key Scale Level", 110);
            ops.push_back (&g);
            envs.push_back (&add (std::make_unique<OpEnvelopeView> (s, p)));
        }
        s.addTargetListener (this);
        updateTitles();

        minContentWidth = 980;
        layout = [this] (int w)
        {
            auto area = juce::Rectangle<int> (0, 0, w, 10000).reduced (8);
            const int top = 150;
            algoGroup->setBounds (area.getX(), area.getY(), 300, top);
            algoView->setBounds (area.getX() + 308, area.getY(), area.getWidth() - 308, top);
            int y = area.getY() + top + 8;
            const int colW = (area.getWidth() - 3 * 8) / 4;
            int h = 0;
            for (auto* g : ops) h = juce::jmax (h, g->preferredHeight (colW));
            for (size_t i = 0; i < ops.size(); ++i)
            {
                const int x = area.getX() + (int) i * (colW + 8);
                envs[i]->setBounds (x, y, colW, 90);
                ops[i]->setBounds (x, y + 96, colW, h);
            }
            return y + 96 + h + 8;
        };
    }

    ~OperatorsPage() override { target.removeTargetListener (this); }

private:
    void targetReplaced() override { updateTitles(); }
    void targetChanged (const std::vector<uint16_t>&) override { updateTitles(); }

    void updateTitles()
    {
        const int mask = carrierMask (target.getParam (T().get ("algorithm")));
        for (int i = 0; i < 4; ++i)
        {
            static const juce::String dot (juce::CharPointer_UTF8 ("  \xc2\xb7  "));
            const juce::String t = "OP" + juce::String (i + 1) + dot
                                   + ((mask >> i) & 1 ? "Carrier (volume = loudness)" : "Modulator (volume = brightness)");
            if (ops[(size_t) i]->title != t)
            {
                ops[(size_t) i]->title = t;
                ops[(size_t) i]->repaint();
            }
        }
    }

    ParamGroup* algoGroup = nullptr;
    AlgorithmView* algoView = nullptr;
    std::vector<ParamGroup*> ops;
    std::vector<OpEnvelopeView*> envs;
};

// ---------------------------------------------------------------------------
// Modulation: LFO1, LFO2, EG5
// ---------------------------------------------------------------------------
class Eg5View : public Listening
{
public:
    using Listening::Listening;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        drawScope (g, r);
        r = r.reduced (8.0f);
        auto len = [] (int rate) { return 0.04f + (float) rate / 127.0f; };
        auto lvl = [] (int l) { return (float) l / 127.0f; };
        float t = 0;
        std::vector<std::pair<float, float>> raw { { 0.0f, 0.0f } };
        t += len (v ("eg5.attack"));  raw.push_back ({ t, lvl (v ("eg5.attacklvl")) });
        t += len (v ("eg5.decay1"));  raw.push_back ({ t, lvl (v ("eg5.decaylvl")) });
        t += len (v ("eg5.decay2"));  raw.push_back ({ t, lvl (v ("eg5.sustain")) });
        t += 0.6f;                    raw.push_back ({ t, lvl (v ("eg5.sustain")) });
        t += len (v ("eg5.release")); raw.push_back ({ t, 0.0f });
        std::vector<juce::Point<float>> pts;
        for (auto& [x, y] : raw) pts.push_back ({ x / t, y });
        drawCurve (g, r, pts, colours::accent2);
    }
};

class LfoView : public Listening
{
public:
    LfoView (ParamTarget& t, std::string lfo) : Listening (t), p (std::move (lfo)) {}

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        drawScope (g, r);
        r = r.reduced (8.0f, 10.0f);
        const bool on = v (p + ".on") != 0;
        const int wave = v (p + ".wave");
        const float cycles = 1.0f + (float) v (p + ".rate") / 32.0f;
        juce::Path path;
        juce::Random rnd (7);
        float held = 0.0f;
        for (int i = 0; i <= 200; ++i)
        {
            const float x = (float) i / 200.0f, ph = std::fmod (x * cycles, 1.0f);
            float y = 0.0f;
            switch (wave)
            {
                case 0: y = std::sin (ph * juce::MathConstants<float>::twoPi); break;
                case 1: y = ph < 0.5f ? ph * 4.0f - 1.0f : 3.0f - ph * 4.0f; break;
                case 2: y = ph * 2.0f - 1.0f; break;
                case 3: y = ph < 0.5f ? 1.0f : -1.0f; break;
                default:
                    if (i == 0 || std::fmod ((float) (i - 1) / 200.0f * cycles, 1.0f) > ph) held = rnd.nextFloat() * 2.0f - 1.0f;
                    y = held;
                    break;
            }
            const float px = r.getX() + r.getWidth() * x, py = r.getCentreY() - y * r.getHeight() * 0.5f;
            if (i == 0) path.startNewSubPath (px, py); else path.lineTo (px, py);
        }
        g.setColour ((on ? colours::accent : colours::dimText).withAlpha (on ? 1.0f : 0.5f));
        g.strokePath (path, juce::PathStrokeType (2.0f));
        if (! on)
        {
            g.setColour (colours::dimText);
            g.setFont (font (11.0f));
            g.drawText ("off", getLocalBounds().reduced (6), juce::Justification::topRight);
        }
    }

private:
    std::string p;
};

class ModulationPage : public Page
{
public:
    explicit ModulationPage (Session& s) : Page (s, T())
    {
        for (int l = 1; l <= 2; ++l)
        {
            const std::string p = "lfo" + std::to_string (l);
            auto& g = group ("LFO " + juce::String (l));
            g.toggles ({ { p + ".on", "On" }, { p + ".sync", "Note Sync" } });
            g.combo (p + ".wave", "Waveform", 110);
            g.knob (p + ".rate");
            g.knob (p + ".phase");
            g.lineBreak();
            g.knob (p + ".pitch", "Pitch", signedText);
            for (int o = 1; o <= 4; ++o)
                g.knob (p + ".op" + std::to_string (o), {}, signedText);
            g.knob (p + (l == 1 ? ".lfo2" : ".lfo1"), {}, signedText);
            g.knob (p + ".aout", {}, signedText);
            groups.push_back (&g);
            views.push_back (&add (std::make_unique<LfoView> (s, p)));
        }

        auto& e = group ("EG5 (modulation envelope)");
        e.knob ("eg5.attack");
        e.knob ("eg5.attacklvl");
        e.knob ("eg5.decay1");
        e.knob ("eg5.decaylvl");
        e.knob ("eg5.decay2");
        e.knob ("eg5.sustain");
        e.knob ("eg5.release");
        e.lineBreak();
        e.knob ("eg5.curve", {}, signedText);
        e.toggles ({ { "eg5.curvea", "Curve on Attack" }, { "eg5.curved1", "Curve on Decay 1" } });
        e.toggles ({ { "eg5.curved2", "Curve on Decay 2" }, { "eg5.curver", "Curve on Release" } });
        e.lineBreak();
        e.knob ("eg5.pitch", "Pitch", signedText);
        for (int o = 1; o <= 4; ++o)
            e.knob ("eg5.op" + std::to_string (o), {}, signedText);
        e.knob ("eg5.lfo1", {}, signedText);
        e.knob ("eg5.aout", {}, signedText);
        groups.push_back (&e);
        views.push_back (&add (std::make_unique<Eg5View> (s)));

        layout = [this] (int w)
        {
            auto area = juce::Rectangle<int> (0, 0, w, 10000).reduced (8);
            int y = area.getY();
            const int viewW = 260;
            for (size_t i = 0; i < groups.size(); ++i)
            {
                const int gw = area.getWidth() - viewW - 8;
                const int h = groups[i]->preferredHeight (gw);
                groups[i]->setBounds (area.getX(), y, gw, h);
                views[i]->setBounds (area.getRight() - viewW, y, viewW, h);
                y += h + 8;
            }
            return y;
        };
    }

private:
    std::vector<ParamGroup*> groups;
    std::vector<juce::Component*> views;
};

// ---------------------------------------------------------------------------
// Play settings and controller assignments
// ---------------------------------------------------------------------------
class PlayPage : public Page
{
public:
    explicit PlayPage (Session& s) : Page (s, T())
    {
        auto& play = group ("Play");
        play.combo ("play.mode", "Mode", 100);
        play.toggle ("play.suskey", "SusKey");
        play.knob ("porta", "Portamento");
        play.knob ("finetune", {}, signedText);
        play.knob ("pitchrange", "Bend Range");
        groups.push_back (&play);

        auto& sync = group ("MIDI Clock Sync");
        sync.toggle ("sync.wt", "Wavetable");
        sync.toggle ("sync.lfo", "LFOs");
        sync.toggle ("sync.env", "EG5");
        groups.push_back (&sync);

        for (auto [p, title] : { std::pair { "vel", "Velocity" }, { "at", "Aftertouch" }, { "mw", "Modwheel" } })
        {
            auto& g = group (title);
            g.combo (std::string (p) + ".assign", "Controls", 230);
            g.lineBreak();
            g.knob (std::string (p) + ".init", "Init");
            g.knob (std::string (p) + ".depth", "Depth", signedText);
            groups.push_back (&g);
        }

        layout = [this] (int w) { return columns (groups, 2, juce::Rectangle<int> (0, 0, w, 10000).reduced (8)); };
    }

private:
    std::vector<ParamGroup*> groups;
};

// ---------------------------------------------------------------------------
// Wavetable
// ---------------------------------------------------------------------------

// 32 steps: command, then three parameters that are either absolute values
// or changes relative to the current value.
class WavetableGrid : public Listening
{
public:
    explicit WavetableGrid (ParamTarget& t) : Listening (t) {}

    static constexpr int kRowH = 22, kHeaderH = 26, kStepW = 44, kCmdW = 80;

    int preferredHeight() const { return kHeaderH + fm::kWavetableSteps * kRowH + 4; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::panel);
        const int pw = paramWidth();
        g.setFont (font (12.0f, true));
        g.setColour (colours::accent2);
        g.fillRect (0, kHeaderH - 2, getWidth(), 1);
        g.fillRect (0, kHeaderH - 4, getWidth(), 1);
        g.drawText ("Step", 0, 0, kStepW, kHeaderH, juce::Justification::centred);
        g.drawText ("Command", kStepW, 0, kCmdW, kHeaderH, juce::Justification::centred);
        for (int k = 1; k <= 3; ++k)
        {
            const int assign = v ("wt.par" + std::to_string (k));
            const juce::String name = assign == 0 ? "Par " + juce::String (k) + " (not assigned)"
                                                  : juce::String (k) + ": " + fm::ccName (assign);
            g.drawText (name, paramX (k), 0, pw, kHeaderH, juce::Justification::centred);
        }

        bool ended = false;
        for (int s = 0; s < fm::kWavetableSteps; ++s)
        {
            const int y = kHeaderH + s * kRowH;
            const auto p = "wt." + std::to_string (s);
            const int cmd = v (p + ".cmd");
            // Tracker rows: every 4th row is highlighted, step numbers in hex.
            if (s % 4 == 0) { g.setColour (colours::panelEdge.withAlpha (0.35f)); g.fillRect (0, y, getWidth(), kRowH); }

            g.setFont (font (12.0f, s % 4 == 0));
            g.setColour (ended ? colours::panelEdge : (s % 4 == 0 ? colours::accent : colours::dimText));
            g.drawText (juce::String::toHexString (s).toUpperCase().paddedLeft ('0', 2), 0, y, kStepW, kRowH,
                        juce::Justification::centred);

            static const juce::Colour cmdColours[] = { colours::panelEdge, colours::accent, colours::accent2, colours::bad };
            auto cmdBox = juce::Rectangle<int> (kStepW, y, kCmdW, kRowH).reduced (3, 2).toFloat();
            g.setColour (colours::background);
            g.fillRect (cmdBox);
            g.setColour (cmd == 0 ? colours::panelEdge : cmdColours[cmd]);
            g.setFont (font (12.0f, cmd != 0));
            g.drawText (cmd == 0 ? juce::String ("---") : juce::String (fm::wtCommands()[(size_t) cmd]).toUpperCase(), cmdBox,
                        juce::Justification::centred);

            for (int k = 1; k <= 3; ++k)
            {
                auto cell = juce::Rectangle<int> (paramX (k), y, pw, kRowH).reduced (3, 2);
                const int value = v (p + ".p" + std::to_string (k));
                const bool abs = v (p + ".abs" + std::to_string (k)) != 0;
                const bool active = cmd == 1 && v ("wt.par" + std::to_string (k)) != 0;
                const bool gotoTarget = cmd == 2 && k == 1;

                g.setColour (colours::background);
                g.fillRect (cell.toFloat());
                if (gotoTarget)
                {
                    g.setColour (colours::accent2);
                    g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92 step ")) + juce::String (value & 0x1f), cell,
                                juce::Justification::centred);
                    continue;
                }
                if (cmd != 1)
                    continue;
                auto tag = cell.removeFromLeft (34);
                g.setColour ((abs ? colours::accent2 : colours::accent).withAlpha (active ? 0.4f : 0.15f));
                g.fillRect (tag.toFloat().reduced (2.0f, 1.0f));
                g.setColour (active ? colours::text : colours::dimText);
                g.setFont (font (10.5f, true));
                g.drawText (abs ? "ABS" : "REL", tag, juce::Justification::centred);

                // Bar for absolute values, centred bar for relative ones.
                auto bar = cell.reduced (4, 4).toFloat();
                if (abs)
                {
                    g.setColour (colours::accent2.withAlpha (active ? 0.6f : 0.25f));
                    g.fillRect (bar.withWidth (bar.getWidth() * (float) value / 127.0f));
                }
                else
                {
                    const int rel = value < 64 ? value : value - 128;
                    const float cx = bar.getCentreX(), wid = bar.getWidth() * 0.5f * (float) std::abs (rel) / 64.0f;
                    g.setColour (colours::accent.withAlpha (active ? 0.6f : 0.25f));
                    g.fillRect (rel >= 0 ? juce::Rectangle<float> (cx, bar.getY(), wid, bar.getHeight())
                                         : juce::Rectangle<float> (cx - wid, bar.getY(), wid, bar.getHeight()));
                }
                g.setColour (active ? colours::text : colours::dimText);
                g.setFont (font (11.5f));
                g.drawText (abs ? juce::String (value) : signedText (value < 64 ? value : value - 128), cell.reduced (6, 0),
                            juce::Justification::centredRight);
            }
            if (cmd == 3) ended = true;
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto hit = hitCell (e.getPosition());
        drag = hit;
        dragStartY = e.y;
        if (hit.step < 0) return;
        const auto p = "wt." + std::to_string (hit.step);
        if (hit.column == 0)
        {
            if (e.mods.isPopupMenu())
            {
                juce::PopupMenu m;
                for (int c = 0; c < 4; ++c)
                    m.addItem (c + 1, fm::wtCommands()[(size_t) c], true, c == v (p + ".cmd"));
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                                 [this, p] (int r) { if (r > 0) set (p + ".cmd", r - 1); });
            }
            else
            {
                set (p + ".cmd", (v (p + ".cmd") + 1) % 4);
            }
            drag.step = -1;
            return;
        }
        if (hit.onTag && v (p + ".cmd") == 1)
        {
            const auto id = p + ".abs" + std::to_string (hit.column);
            set (id, v (id) ? 0 : 1);
            drag.step = -1;
            return;
        }
        dragStartValue = displayValue (hit);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag.step < 0 || drag.column == 0) return;
        setDisplayValue (drag, dragStartValue + (dragStartY - e.y) / 2);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto hit = hitCell (e.getPosition());
        if (hit.step >= 0 && hit.column > 0 && ! hit.onTag)
            setDisplayValue (hit, isAbs (hit) ? 64 : 0);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const auto hit = hitCell (e.getPosition());
        if (hit.step >= 0 && hit.column > 0)
            setDisplayValue (hit, displayValue (hit) + (w.deltaY > 0 ? 1 : -1));
    }

private:
    struct Hit { int step = -1, column = -1; bool onTag = false; };

    int paramWidth() const { return (getWidth() - kStepW - kCmdW) / 3; }
    int paramX (int k) const { return kStepW + kCmdW + (k - 1) * paramWidth(); }

    Hit hitCell (juce::Point<int> pt) const
    {
        Hit h;
        if (pt.y < kHeaderH) return h;
        h.step = (pt.y - kHeaderH) / kRowH;
        if (h.step >= fm::kWavetableSteps) { h.step = -1; return h; }
        if (pt.x < kStepW) { h.step = -1; return h; }
        if (pt.x < kStepW + kCmdW) { h.column = 0; return h; }
        h.column = juce::jlimit (1, 3, 1 + (pt.x - kStepW - kCmdW) / juce::jmax (1, paramWidth()));
        h.onTag = pt.x - paramX (h.column) < 37;
        return h;
    }

    std::string id (const Hit& h, const char* what) const
    {
        return "wt." + std::to_string (h.step) + "." + what + std::to_string (h.column);
    }
    bool isAbs (const Hit& h) const { return v (id (h, "abs")) != 0; }
    bool isGoto (const Hit& h) const { return h.column == 1 && v ("wt." + std::to_string (h.step) + ".cmd") == 2; }

    int displayValue (const Hit& h) const
    {
        const int raw = v (id (h, "p"));
        if (isGoto (h)) return raw & 0x1f;
        return isAbs (h) ? raw : (raw < 64 ? raw : raw - 128);
    }

    void setDisplayValue (const Hit& h, int d)
    {
        int raw;
        if (isGoto (h)) raw = juce::jlimit (0, 31, d);
        else if (isAbs (h)) raw = juce::jlimit (0, 127, d);
        else raw = juce::jlimit (-64, 63, d) & 0x7f;
        set (id (h, "p"), raw);
    }

    void set (const std::string& paramId, int raw) { target.setParam (T().get (paramId), raw); }

    Hit drag;
    int dragStartY = 0, dragStartValue = 0;
};

class WavetablePage : public Page
{
public:
    explicit WavetablePage (Session& s) : Page (s, T())
    {
        auto& cfg = group ("Wavetable Sequencer");
        cfg.combo ("wt.mode", "Mode", 130);
        cfg.knob ("wt.rate", "Rate");
        cfg.combo ("wt.par1", "Parameter 1", 230);
        cfg.combo ("wt.par2", "Parameter 2", 230);
        cfg.combo ("wt.par3", "Parameter 3", 230);
        config = &cfg;
        grid = &add (std::make_unique<WavetableGrid> (s));
        hint = &add (makeLabel ("Click a command to cycle it (right-click for a menu). Click ABS/REL to switch a step between "
                                "an absolute value and a change of the current value. Drag or scroll a value; double-click "
                                "resets it. \"Goto\" jumps to the step given in parameter 1. Rate 0 stops the sequencer."));
        hint->setJustificationType (juce::Justification::topLeft);

        layout = [this] (int w)
        {
            auto area = juce::Rectangle<int> (0, 0, w, 10000).reduced (8);
            const int ch = config->preferredHeight (area.getWidth());
            config->setBounds (area.removeFromTop (ch));
            area.removeFromTop (6);
            hint->setBounds (area.removeFromTop (36));
            area.removeFromTop (4);
            grid->setBounds (area.removeFromTop (grid->preferredHeight()));
            return area.getY() + 8;
        };
    }

private:
    ParamGroup* config = nullptr;
    WavetableGrid* grid = nullptr;
    juce::Label* hint = nullptr;
};
} // namespace

VoiceEditor::VoiceEditor (Session& s)
{
    addPage ("Operators", std::make_unique<OperatorsPage> (s));
    addPage ("Modulation", std::make_unique<ModulationPage> (s));
    addPage ("Play & Controllers", std::make_unique<PlayPage> (s));
    addPage ("Wavetable", std::make_unique<WavetablePage> (s));
}
} // namespace ui
