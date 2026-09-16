# GlitchPad research

Illformed Glitch² is a sequencer-driven multi-effect: 128 MIDI-triggered
scenes, each with a pattern of modules (Retrigger, Reverser, Shuffler,
Tape Stop, Stretcher, Gater, Lofi, Delay, Distortion, Modulator) plus a
Randomizer that only picks weights.

NTS-3 cannot host that editor. A genericfx unit has an XY pad, Depth,
and at most eight parameters. `unit_render` input is muted while the pad
is up; firmware 1.4+ `get_raw_input` still carries AUDIO IN, which is
how TransitionLooper pre-rolls a loop.

## Mapping

Glitch² scenes are MIDI notes. GlitchPad plays four core modules with a
Passort-style region lock: the effect is chosen from where the finger
first touches, then Y drives musical slice / rate while held.

| Region | Module |
| --- | --- |
| Top-left | Retrigger |
| Top-right | Reverse |
| Bottom-left | Shuffle |
| Bottom-right | Gate |

| Control | Role |
| --- | --- |
| Touch start | Mode lock |
| Y while held | Slice length / gate rate (bottom = long, top = short) |
| Depth | MIX |
| DECAY | Retrigger / shuffle fade, gate smoothing |
| SYNC | EVEN / TRIP / DOT / FREE |
| HOLD | GATE or LATCH |

Buffer modules (RTRG, REV, SHUF) freeze the most recent slice when the
pad goes down. GATE runs on live input. Silent pre-roll arms a live
capture, same as TransitionLooper.

What is intentionally not ported: 128-scene banks, the multi-lane
pattern editor, Tape Stop / Stretch / Crush / Delay as separate scenes,
per-module filters/mixers, Modulator, Distortion, and the Randomizer
module. Those need a host UI NTS-3 does not have, or were dropped to
keep the pad map to four clear edges.

## Memory

NTS-3 genericfx SDRAM is 3 MB per runtime. The stereo capture buffer is
one bar at 40 BPM (288000 frames): 576000 floats, about 2.2 MB.

## Sources

- [Illformed Glitch²](https://illformed.com/glitch/)
- [Glitch 2.1.3 User Guide](https://illformed.com/downloads/glitch_2_1_4/Glitch2_User_Guide.pdf)
