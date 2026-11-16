#include "Randomizer.h"
#include "Params.h"

#include <algorithm>
#include <cmath>

namespace fm
{
namespace
{
using Rng = std::mt19937;

int rnd (Rng& r, int lo, int hi) { return std::uniform_int_distribution<int> (lo, hi) (r); }
bool chance (Rng& r, double p) { return std::uniform_real_distribution<double> (0.0, 1.0) (r) < p; }
bool startsWith (const std::string& s, const std::string& p) { return s.rfind (p, 0) == 0; }

bool inSection (const std::string& section, const std::string& id)
{
    const bool all = section == "all";
    if (all || section == "op")
        if (startsWith (id, "op") || id == "feedback" || id == "algorithm")
            return true;
    if (all || section == "lfo")
        if (startsWith (id, "lfo1.") || startsWith (id, "lfo2."))
            return true;
    if (all || section == "eg5")
        if (startsWith (id, "eg5."))
            return true;
    if (all || section == "wtc")
        if (id == "wt.rate" || id == "wt.par1" || id == "wt.par2" || id == "wt.par3")
            return true;
    return false;
}

void set (Patch& p, const std::string& id, int raw)
{
    if (const auto* d = voiceParams().find (id))
        p.setRaw (*d, std::clamp (raw, 0, d->maxRaw()));
}

// Rules from mbfm_rnd.inc, applied after every parameter got a random value.
void applyRules (const std::string& section, Patch& p, Rng& r)
{
    const bool all = section == "all";
    if (all || section == "op")
    {
        // "better results when OP1/OP3 multiplier between 0..7, and OP2/4 always 0"
        set (p, "op1.mult", rnd (r, 0, 7));
        set (p, "op3.mult", rnd (r, 0, 7));
        set (p, "op2.mult", 0);
        set (p, "op4.mult", 0);
        // Volume OP1/OP3 48..63, OP2/OP4 always 63 for max output.
        set (p, "op1.volume", rnd (r, 48, 63));
        set (p, "op3.volume", rnd (r, 48, 63));
        set (p, "op2.volume", 63);
        set (p, "op4.volume", 63);
        // Sustain between 8..15 so that we always hear the operator.
        for (int o = 1; o <= 4; ++o)
            set (p, "op" + std::to_string (o) + ".sustain", rnd (r, 8, 15));
    }
    if (all || section == "lfo")
    {
        // LFO waveform 0..3, and no depth on OP2/OP4 (only the modulators).
        for (const std::string l : { "lfo1", "lfo2" })
        {
            set (p, l + ".wave", rnd (r, 0, 3));
            set (p, l + ".op2", 64);
            set (p, l + ".op4", 64);
        }
    }
    if (all || section == "eg5")
    {
        set (p, "eg5.op2", 64);
        set (p, "eg5.op4", 64);
    }
    if (all || section == "wtc")
    {
        // Rate 64..127, wavetable playing with a probability of 1:2.
        set (p, "wt.rate", chance (r, 0.5) ? rnd (r, 64, 127) : 0);
        // Parameters picked from a list of useful targets.
        static const int targets[] = { 20, 21, 22, 23, 48, 49, 50, 51, 66, 67, 72, 82, 83, 88, 105, 110, 116 };
        for (int k = 1; k <= 3; ++k)
            set (p, "wt.par" + std::to_string (k), targets[rnd (r, 0, 16)]);
    }
}
} // namespace

const std::vector<RandomSection>& randomSections()
{
    static const std::vector<RandomSection> s {
        { "all", "All", "A completely new patch: operators, LFOs, EG5 and wavetable" },
        { "op", "OP", "Operators: waveforms, multipliers, volumes, envelopes, flags, feedback and algorithm" },
        { "lfo", "LFO", "LFO1 and LFO2: waveform, rate, phase and depths (not on the OP2/OP4 volume)" },
        { "eg5", "EG5", "The modulation envelope and its depths (not on the OP2/OP4 volume)" },
        { "wtc", "WT Config", "Wavetable rate (plays half of the time) and its three parameter assignments" },
        { "wte", "WT Steps", "All 32 wavetable steps: commands and values" },
    };
    return s;
}

void randomize (Patch& patch, const std::string& section, float amount, Rng& rng)
{
    amount = std::clamp (amount, 0.0f, 1.0f);
    const auto& t = voiceParams();
    const Patch before = patch;
    Patch target = patch;

    std::vector<const ParamDef*> defs;
    for (auto& d : t.all())
        if (inSection (section, d.id))
        {
            defs.push_back (&d);
            target.setRaw (d, rnd (rng, 0, d.maxRaw()));
        }
    applyRules (section, target, rng);

    const bool steps = section == "all" || section == "wte";
    if (steps)
        for (size_t a = kWavetableAddr; a < kPatchSize; ++a)
            target.data[a] = (uint8_t) rnd (rng, 0, 127);
    if (section == "all")
        target.setName ("Random Patch");

    if (amount >= 1.0f)
    {
        patch = target;
        return;
    }

    // Partial: move each value part of the way; switches and choices flip
    // with a probability of `amount`, wavetable bytes too.
    for (auto* d : defs)
    {
        const int a = before.getRaw (*d), b = target.getRaw (*d);
        if (a == b) continue;
        const bool discrete = d->kind == ParamDef::Kind::Bool || d->kind == ParamDef::Kind::Choice || d->maxRaw() < 16;
        patch.setRaw (*d, discrete ? (chance (rng, amount) ? b : a) : (int) std::lround (a + (b - a) * (double) amount));
    }
    if (steps)
        for (size_t a = kWavetableAddr; a < kPatchSize; a += 4) // one step = 4 bytes, changed together
            if (chance (rng, amount))
                for (size_t k = 0; k < 4; ++k)
                    patch.data[a + k] = target.data[a + k];
}
} // namespace fm
