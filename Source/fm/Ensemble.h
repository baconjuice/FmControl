#pragma once

// MIDIbox FM ensemble: which stored patch each of the four voices plays, on
// which MIDI channel and key range, with velocity mode, volume, transpose,
// unisono and audio outputs (midibox_fm_par_ensemble.txt). It travels in the
// same 256-byte dump as a voice patch (type 70 = stored ensemble, 78 = the
// one playing now): 16 bytes per voice at 0x00/0x10/0x20/0x30, of which
// 0x0-0xa are used; the rest is always 0. 32 ensembles per bank, no name.
//
// Loading an ensemble on the box (RAM dump, store, or reading a stored one,
// which also switches to it) reloads all four voices from their stored
// patches and the drum set from its stored set.

#include "Params.h"

#include <string>

namespace fm
{
constexpr int kEnsemblesPerBank = 32;
constexpr int kEnsemblePart = kVoices + 1; // "voice" number used for the ensemble in edit/live code
constexpr int kEnsembleDataSize = 0x40;
constexpr int kEnsembleVoiceStride = 0x10;

const ParamTable& ensembleParams();

// Parameter id of a voice's setting: ensembleId (1, "patch") -> "v2.patch".
std::string ensembleId (int voice, const char* param);

// Init ensemble: the MIDIbox FM preset (A005, A037, A084, A099 on channels
// 1-4, full keyboard, all outputs) (c) Thorsten Klose.
Patch makeInitEnsemble();

// Settings of one voice, read from an ensemble.
int ensembleBank (const Patch& e, int voice);
int ensemblePatch (const Patch& e, int voice);
int ensembleChannel (const Patch& e, int voice); // 1..16
} // namespace fm
