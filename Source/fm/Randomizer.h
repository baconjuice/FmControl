#pragma once

// Patch randomizer, modelled on the MIDIbox FM control surface's random
// generator (mbfm_rnd.inc): the same sections (All, OP, LFO, EG5, WTC, WTE)
// and the same rules that keep results playable (modulators OP1/OP3 with
// low multipliers and high volume, OP2/OP4 at full volume, sustain always
// audible, LFO/EG5 not touching the carriers' volume, ...).

#include "Patch.h"

#include <random>
#include <string>
#include <vector>

namespace fm
{
struct RandomSection
{
    std::string id;    // "all", "op", "lfo", "eg5", "wtc", "wte"
    std::string label; // shown on the button
    std::string help;  // tooltip
};

const std::vector<RandomSection>& randomSections();

// Randomize one section of the patch. amount 1 = completely new values,
// smaller values only move each parameter part of the way (0.25 = a gentle
// variation of the current sound).
void randomize (Patch& patch, const std::string& section, float amount, std::mt19937& rng);

// The same for a drum set (the box has no drum randomizer, so these rules
// are FmControl's own): sections All, BD, HH/SD, Tom/Cym, Envelopes,
// Waveforms. Key ranges and audio outputs are never touched, so the kit
// keeps playing on the same notes and outputs.
const std::vector<RandomSection>& drumRandomSections();
void randomizeDrums (Patch& drums, const std::string& section, float amount, std::mt19937& rng);
} // namespace fm
