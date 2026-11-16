# FmControl

Editor and librarian for the **MIDIbox FM** (V1.4, e.g. the sammichFM). It runs as a standalone Mac app, or as an
AU or VST3 plugin. A sibling of [SidControl](https://github.com/baconjuice/SidControl).

It talks to the box with the MIDIbox FM SysEx protocol (`F0 00 00 7E 49 <device> ...`). The spec is
`apps/synthesizers/midibox_fm_v1/doc/midibox_fm_sysex_implementation.txt` in
[midibox/mios8](https://github.com/midibox/mios8).

## Build

Requires Xcode (or the command line tools) and CMake (`brew install cmake`).

```bash
git submodule update --init
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build
```

Output is in `build/FmControl_artefacts/Release/`, in the `Standalone/`, `AU/` and `VST3/` folders.

## Using it

1. Pick the **MIDI In** and **MIDI Out** your MIDIbox FM is connected to, then press **Test**.
2. Choose the **Voice** (1-4) to edit. Each voice plays its own patch; the first time you pick a voice
   its patch is read from the box.
3. **Live** (on by default): every edit is sent to that voice as you make it. **Get Voice** reads its patch again,
   **Send Voice** sends the whole patch.
4. Pages:
   - **Operators**: algorithm (with a diagram), OP1 feedback, and per operator the OPL3 waveform, frequency
     multiplier, volume, envelope (with a graph), sustain/vibrato/tremolo/KSR flags and key scaling.
   - **Modulation**: LFO1, LFO2 and EG5 with their routings to pitch, operator volumes, the other LFO and AOUT.
   - **Play & Controllers**: play mode, portamento, finetune, bend range, MIDI clock sync, and what velocity,
     aftertouch and the modwheel control.
   - **Wavetable**: the 32-step wavetable sequencer.
5. Librarian (left): banks A-H with 128 patches each, fetch/store/bank transfers, `.syx` files (the official
   preset banks open directly), right-click for copy, paste, init and clear.
6. **Keys**: MIDI from your keyboard or the DAW track goes to the box, by default on the edited voice's channel
   (channels 1-4 for voices 1-4 in the default ensemble).

`fmcli` (built with the app) is a diagnostics tool: `list`, `scan`, `ping`, `get`, `getslot`, `monitor`, `play`, and `resend` / `parburst` / `rawwrite` / `fetchbank` to test whether patch writes get through.

Patch dumps are sent in small pieces (4 bytes every 10 ms, about 0.9 s per patch): the FM drops bytes of a dump that arrives at full MIDI speed and then never answers.

## Credits

FmControl would not exist without **Thorsten Klose (TK.)** and the [MIDIbox](http://www.midibox.org) community.
The MIDIbox FM, its firmware, the SysEx protocol and the preset patches are TK.'s work. FmControl is only a remote
control for them.

- The parameter tables follow `midibox_fm_cc_voice.txt` and the firmware sources in
  [midibox/mios8](https://github.com/midibox/mios8).
- `Source/fm/CcNames.cpp` (parameter names) is generated from `midibox_fm_cc_voice.txt`.
- `Source/fm/InitPatch.cpp` and `Tests/fixtures/midibox_fm_voices.syx` come from the firmware's preset bank.

Those files are Copyright 1998–2007 Thorsten Klose (tk@midibox.org) and are "licensed for personal
non-commercial use only". FmControl is a free, non-commercial hobby project. Please don't use any of it commercially.

FmControl is built with [JUCE](https://juce.com). The sammichFM hardware comes from the MIDIbox community.
