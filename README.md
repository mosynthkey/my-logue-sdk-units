# my-logue-sdk-plugins

Custom [logue SDK](https://github.com/korginc/logue-sdk) units.

**Under construction.** Hardware transfer and wasm preview are still being tested.

Each plugin lists its targets in `plugin.json`. CI cross-compiles those targets and GitHub Pages hosts the unit files. Web MIDI install is implemented for NTS-1 mkII and NTS-3.

## Layout

```
plugins/<name>/
  plugin.json                 # id, module, targets[]
  dsp/                        # shared algorithm
  targets/<platform>/         # header, unit glue, Makefile
third_party/logue-sdk/
website/                      # Web app source: Pages UI + SysEx sender
dist/website/                 # Generated deploy artifact (gitignored)
```

You do not duplicate the DSP per device. You do add a thin target adapter (`header.c`, `unit.cc` or v1 `OSC_CYCLE`, Makefile). SDK v1 and v2 APIs are not source-compatible. Module types are not interchangeable (an `osc` is not an NTS-3 `genericfx`).

Plugins:

- **HyperSaw** — Virus TI-inspired 9-voice detuned saw stack with Density, Spread, HyperSub, and stereo width. Targets `nts-1_mkii` and `microkorg2`.
- **FbOsc** — JP-8080-inspired Feedback oscillator (band-limited saw through a key-tracked resonant comb filter). Comb peak gain is compensated so FEED does not slam the output. Targets `nts-1_mkii` and `microkorg2`.
- **MoHowl** — Author-motif NTS-3 feedback howl (`genericfx`). Same comb as FbOsc with feedback locked at maximum. Pitch is LFO-wobbled: X = LFO depth, Y = harmonics.
- **Ride909** — NTS-3 off-beat 909 Ride Cymbal (v1.0.0). Clock-synced: press the pad on Step 1 timing for off-beat rides; pitch and sidechain-style pump. Voice is the TR-909 Ride ROM (6-bit PCM) through variable-rate playback, resistor DAC, and analog reconstruction, not a WAV sampler.
- **Shaker** — PhISEM percussion, `nts-1_mkii` (`osc`, note on = shake) and `nts-3_kaoss` (`genericfx`, pad motion = shake). Instrument constants follow STK Shakers (Cook / Scavone); not a copy of STK source.
- **airFM** — two-op phase-mod FM, `nts-3_kaoss` (`genericfx`, pad XY = carrier/modulator, touch gate). Inspired by Alesis airSynth Program 3.
- **StepFilter** — Tempo-synced multimode filter with periodically random Cutoff. Great for adding movement to long synth-pad chord patterns. Targets `nts-3_kaoss`.
- **AirHorn** — DJ air horn (native ~302 Hz; Key mode concert-tracks from measured root). Pitch envelope recreates the opening drop. NTS-1/mk2: PMODE Fixed|Key. NTS-3: DECAY (127=Sustain), PMODE Fixed|Pitch with continuous PITCH ±2 oct on X. Targets `nts-1_mkii`, `nts-3_kaoss`, and `microkorg2`.
- **Kaocid** — TB-303 style acid bass with auto phrase generator, `nts-3_kaoss` (`genericfx`, hold pad = tempo-synced 16-step pattern with glides, retouch = new phrase). Panel: Cutoff, Resonance, Wave, Env Mod, Decay, Accent, plus ROOT and Mix. Voice inspired by gsynth TB-303 (Andy Sloane, 2001).
- **TechnoRumble** — Techno rumble kick processor (`revfx` on mkII, `genericfx` on NTS-3): long reverb tail, sub LPF, drive, and transient-triggered sidechain duck. Feed a kick on AUDIO IN or synth output.
- **TransitionLooper** — NTS-3 DJ transition looper: captures AUDIO IN into a tempo-synced 16-step stereo loop plus wrap glue. Prefers `get_raw_input` (firmware 1.4+) while the pad is up; if that pre-roll is silent, the first hold records one live bar and then loops. Pad up bypasses; hold fades into the stored loop (volume, HPF/LPF, bass swap, echo out, brake, or roll).
- **Retrig / Reverse / Shuffle / Gater** — Four independent Glitch²-style NTS-3 FX split from the former GlitchPad. Each keeps a tempo-synced AUDIO IN buffer (Gater is live-only). Pad engages, Y = slice / rate, Depth = mix. Untouched = bypass.
- **KoPunch** — NTS-3 performance pad inspired by EP-133 KO II Punch-In FX 2.0. Twelve modes on X (pitch random, slice swap, granulizer, beat repeat, tape stop, filter LFO, LPF, HPF, send/delay throw, tremolo, octave down, decimator); Y = pressure; Depth = wet. Experimental; open with `?experimental`. Not affiliated with teenage engineering.
- **HClap** — NTS-3 808/909 analog hand clap. Hold the pad for a 16-step Euclidean phrase (X = density, spine on 2 and 4). Y morphs TR-808 (transistor noise, one VCA) into TR-909 (LFSR, dual VCA). Experimental; open with `?experimental`.
- **HSnare** — NTS-3 808/909 analog snare (separate unit from HClap). Same phrase pad; Y morphs 808 bridged-T shells + HPF snappy into 909 triangle VCOs, 20 ms pitch bend, and split snappy. Experimental; open with `?experimental`.
- **UKGarage** — NTS-3 tempo-synced 2-step UK Garage kit. Hold to run. X grows ghost notes (soft snares/rims); Y thickens hats and fill energy; top-right flick = one-bar Fill. Experimental; open with `?experimental`.
- **BoomBap** — NTS-3 boom-bap kit. Beat-locked hold gate; X = dusty ghosts, Y = swung hats. Default ~90 BPM. Experimental; open with `?experimental`.
- **Dembow** — NTS-3 reggaeton dembow kit. Beat-locked; X = rim/cha answers, Y = percussion. Default ~96 BPM. Experimental; open with `?experimental`.
- **Footwork** — NTS-3 Chicago footwork / juke kit. Beat-locked; X = kick stutters, Y = snare rolls. Default ~160 BPM. Experimental; open with `?experimental`.
- **BreakBeat** — NTS-3 Amen-inspired synthetic breakbeat (not a sample). Beat-locked; X = syncopation/ghosts, Y = break energy. Default ~174 BPM. Experimental; open with `?experimental`.
- **DnBass** — NTS-3 drum & bass kit. Beat-locked half-time snare on 3; X = rolling hats, Y = break energy. Default ~174 BPM. Experimental; open with `?experimental`.
- **Trance** — NTS-3 trance kit with TR-909 ROM hats and analog 909-style kick/clap models (BD/clap have no ROM on a real 909). Beat-locked four-on-the-floor + offbeat opens; X = hats, Y = build. Default ~138 BPM. Experimental; open with `?experimental`.
- **Trance2** — Trance drums plus a rolling bassline. X = bass complexity (offbeat 1/8 → rolling 16ths → walks); Y = drum complexity. Default ~138 BPM. Experimental; open with `?experimental`.
- **AmenTime** — NTS-3 tempo-synced amen-style break slicer. A synthesized 1-bar break (not the Winstons recording) is stored as 12 kHz PCM and chopped to the host clock. Hold the pad to walk a step grid (tap position does not pick the start 16th); X = reverse chance, Y = slice grid, STRT is Edit. Experimental; open with `?experimental`.
- **WavSlice** — Generic NTS-3 1-bar WAV slicer (same pad/chop as AmenTime). Ships an original CC0 drum loop; drop your own file at `assets/loop.wav` or `make -C plugins/wavslice embed WAV=...`. Experimental; open with `?experimental`.
- **Trap808** — NTS-3 trap drums with TR-909 ROM hi-hats and a sliding 808. Hold gates the phrase; hits lock to the beat. X = hat rolls, Y = groove, ROOT = 808 key. Experimental; open with `?experimental`.
- **NTS-3 idea pack (experimental)** — performance FX/OSC units that do not overlap stock NTS-3, Kaocid, airFM, TechnoRumble, or AirHorn. Remaining batch: BeatRepeat, RingExcit, WarpsMorph, EucGate, DataBend, DetuneSawLd, PercIter, GridsDrum, RevRoll, DredBass. Extra: EucRoll (Euclidean step roll), StepError (step DataBend errors), StepFenv (step filter envelope), StepDice, Trap808, UKGarage, BoomBap, Dembow, Footwork, BreakBeat, DnBass, Trance, Retrig, Reverse, Shuffle, Gater. Skipped as overlapping: Kick Rumble (#26 → TechnoRumble), Airhorn (#29 → AirHorn). Open the site with `?experimental` to preview them.

## Targets

| Platform | Unit | CI build | SysEx load | Notes |
| --- | --- | --- | --- | --- |
| `nts-1_mkii` | `.nts1mkiiunit` | yes (gcc 10.3, Cortex-M7) | yes | Implemented. Header `F0 42 3g 00 01 73`. |
| `nts-3_kaoss` | `.nts3unit` | yes (same M7 toolchain) | yes | Header `F0 42 3g 00 01 72`. `genericfx` only, 50 slots. |
| `nts-1` | `.ntkdigunit` | yes (gcc 5.4 / M4) | yes | v1 API. logue-cli + published MIDI spec. |
| `minilogue-xd` | `.mnlgxdunit` | yes (gcc 5.4 / M4) | yes | v1 API. Same as NTS-1 mkI at binary level. |
| `prologue` | `.prlgunit` | yes (gcc 5.4 / M4) | yes | v1 API. Header `F0 42 3g 00 01 4B`. |
| `microkorg2` | `.mk2unit` | yes (Docker / A7) | no | USB mass storage → `Units/Oscs/SLOTxx/`. FW >= 2.0. |

### microKORG2 install (HyperSaw / FbOsc)

1. Download the **`microkorg2`** build (`.mk2unit`), not **mkII** (`.nts1mkiiunit`).
2. Power off, hold **FUNCTION 1**, power on → USB mass storage mode.
3. Copy the file into an empty folder under **`Units/Oscs/`** (one unit per SLOT).
4. Eject, press **FUNCTION 5**, then pick the unit on an **OSC1/2/3** page.

Web SysEx send does **not** work on microKORG2.
| `drumlogue` | `.drmlgunit` | yes (Docker / A7) | no | USB mass storage. |

v1 units (prologue, minilogue xd, NTS-1 mkI) are binary-compatible with each other. Nothing else is.

## Commands

```bash
git submodule update --init
git -C third_party/logue-sdk submodule update --init platform/ext/CMSIS

make GCC_BIN_PATH=/path/to/gcc-arm-none-eabi-10.3-2021.10/bin
make test
make wasm EMCC_BIN_PATH=/path/to/emsdk/upstream/emscripten
```

Do not `git submodule update --recursive` (pulls the huge emsdk tree).

## License

BSD 3-Clause. logue-sdk remains KORG’s BSD 3-Clause. Not affiliated with KORG.
