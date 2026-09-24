# AcidTouch

New KAOCID for the KORG NTS-3 `genericfx` slot. The existing `plugins/kaocid` unit is left in place.

Tap rolls a 16-step acid phrase and the XY pad plays the filter. The slot ignores audio in and behaves as a monophonic oscillator plus an internal sequencer. It does not emit MIDI.

## Panel

| Input | Behavior |
| --- | --- |
| Touch down | **NEW**: new seed, full reroll, restart at step 0. **SAW MU / SQR MU** mutates about 25% of steps instead. |
| Touch hold | Gate keeps the phrase running. Latch and Thru ignore the hold for transport, and a hold of about 0.45 s mutates the current phrase once. |
| Touch up | Gate releases. Latch, Thru, and Mutate keep looping. |
| X | Cutoff, logarithmic, about 80 Hz–8 kHz. |
| Y | Resonance, eased off before self-oscillation. |
| DEN | Gate probability. |
| ACID | Accent probability and slide probability. |
| OCT | 1–3 octaves, natural minor from C2. |
| TIME | Step length around a host-synced 1/16 (half to double). |
| DRV | Soft tanh drive. |
| MODE | Saw/square × Gate / Latch / Thru / Mutate. Thru and Mutate loop from load. |

Phrase rules: 16 steps, rest / note / accent / slide, at least one note, accent velocity 118 and normal velocity 72, 50% gate, slide is a legato exponential glide of fixed time (~55 ms). The same seed reproduces the same pattern. Decay and envelope depth are internal, with a shorter, deeper sweep on accents.

## License

AcidTouch is **GPL-3.0-only**. See `LICENSE` in this directory. The rest of this repository stays BSD 3-Clause. NTS-3 unit boilerplate derived from the KORG logue SDK keeps its BSD 3-Clause notice in those files; the combined AcidTouch unit is distributed under GPL-3.0-only.

The voice and phrase generator are a clean-room reimplementation. Upstream GPL sources were not copied into this tree. They are behavior references:

Behavior and parameter meaning follow these public projects:

- [schwung-tb3po](https://github.com/charlesvestal/schwung-tb3po) (GPL-3.0) — Phazerville `TB_3PO` port. Density, accent, slide, octave, NEW / MUT, legato slide, accent velocity.
- [O_C-Phazerville](https://github.com/djphazer/O_C-Phazerville) `TB_3PO` (GPL) — fixed-time exponential slide, 50% gate, accent steps.
- [schwung-acid](https://github.com/sd88me/schwung-acid) — mutate about 25% of steps. Touch generates; hold or MUT mode mutates.
- [schwung-303](https://github.com/charlesvestal/schwung-303) (GPL-3.0; Open303 core MIT) — accent = velocity ≥ 100, slide = overlapping note-on, cutoff / resonance as the filter performance controls. Devilfish and RAT stages are not included.
- [Open303](https://github.com/RobinSchmidt/Open303) (MIT) — reference for the 303 voice contract. The runtime voice here is a lightweight saw/square, exponential slide, accent VCA, and a 4-pole ladder, not the Open303 engine.
- [jc303](https://github.com/midilab/jc303) (GPL-3.0) — additional public port of the same voice contract.
- [Schwung](https://github.com/charlesvestal/schwung) (MIT) — separate phrase generator and voice, joined here inside one unit.

## Host probe

```bash
g++ -O2 -std=c++11 -DACIDTOUCH_OFFLINE_TEST \
  -I plugins/acidtouch/dsp -I plugins/common \
  -I third_party/logue-sdk/platform/nts-3_kaoss/common \
  -I third_party/logue-sdk/platform/nts-3_kaoss \
  plugins/acidtouch/scripts/render_offline_test.cc -o /tmp/acidtouch_test
/tmp/acidtouch_test
```
