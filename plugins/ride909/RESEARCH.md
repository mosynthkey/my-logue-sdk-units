# Ride909 Research and Implementation Notes

This document records what the TR-909 Ride circuit actually does, where the PCM comes from, and how this logue unit maps that path into DSP. The NTS-3 layering (hold-to-play, off-beat 3-7-11-15, kick pump on 1-5-9-13) is unchanged. The *voice* is no longer a WAV player.

## Hardware path

Roland's Service Notes describe Hi-Hat / Ride / Crash as sampled cymbals stored as PCM in mask ROM, not analog synthesis. The Ride voicing board is:

```
clock oscillator (4011UB, Tune pot)
      ↓
4013 divide-by-2
      ↓
4040 / 4520 address counters
      ↓
HN61256P C44 / 27C256  (32 KB)
      ↓
6-bit data latch (40174)
      ↓
binary-weighted resistor DAC
      ↓
address-derived anti-log VCA
      ↓
analog reconstruction / level (TL072, transistor LPFs)
      ↓
RIDE OUT
```

The 9090 clone redraws the same Ride section (schematic page "9090 - RIDE CYMBAL"). Component values below are taken from that page.

### ROM

| | |
| --- | --- |
| Part | Hitachi HN61256P C44 (IC54) |
| Size | 32 KB = 0x8000 |
| MAME | `hn61256p__c44.ic54` |
| CRC32 | `01a9b435` |
| SHA1 | `daa54c58c7e3ae3398f125568537ec82d5bd1dfd` |
| 9090 archive | `909ride.hex` (Intel HEX of the same image) |

The dump is 8-bit wide but **bits 1:0 are always zero**. Audio is unsigned 6-bit offset binary in bits 7:2, midpoint 32. That matches Colin Fraser's extraction note (shift left by two, unused LSBs) and the 40174 latch wiring on only D0–D5.

The 6-bit codes are not a fully flattened/companded brick: RMS still falls about 14 dB from start to end of the ROM. The analog VCA restores the rest of the cymbal envelope.

### Clock / Tune

There is no crystal. A 4011UB NAND astable (9090: R478=6.8k, R477=10k, C168=470pF, VR30=10kB Tune) runs around 60 kHz and a 4013 divides it by two. That clock is the ROM sample rate.

This unit uses **30 kHz at Tune center** (panel mid), which is the figure given in service discussions (60 kHz osc / 2) and is close to Colin Fraser's 32 kHz working rate.

9090 Ride clock parts (same on Crash): **C168=470pF**, timing **R = R478 (6.8k) + VR30 (10kB linear)**. **R477=10k is input protection on the 4011UB**, not part of the timing R. So:

| Pot | R | vs panel mid | ROM clock @ 30 kHz mid |
| --- | --- | --- | --- |
| CW (min R) | 6.8k | **+9.54 st** | ~52.1 kHz |
| Mid | 11.8k | 0 | 30 kHz |
| CCW (max R) | 16.8k | **−6.12 st** | ~21.1 kHz |

A linear B pot is linear in **R**, hence in **1/f**, not in semitones. Mapping X as a symmetric ±octave (or ±7.8 st around the geometric mean) makes the **low end ~1.7 st too low** and the high end ~1.7 st too narrow. This unit maps X through `clock_ratio = R_mid / (R478 + pot)`, matching the panel. Playback is **zero-order hold**. Because the address counter *is* the envelope DAC, faster Tune also shortens the decay.

### DAC

9090 Ride uses a 6-bit binary-weighted ladder:

`5.1k, 10k, 20k, 40.2k, 80.6k, 160k` (1%)

Weights are within ~2% of ideal 32:16:8:4:2:1, so the DSP uses `(code - 32) / 32`.

### Envelope / VCA

Ride and Crash do **not** have a Decay knob. Network-909 and Fraser both describe a second DAC on the ROM address lines, anti-log tapered into the VCA, so envelope duration always matches the sample at the current Tune. The 9090 Ride page has a second 5.1k–160k ladder plus R430=470k on that path (the eurorack 909-cymbals clone uses 470k for Ride vs 270k for Crash).

This unit treats A14..A9 as that 6-bit address DAC (`address >> 9`) and applies `exp(-2.8 * code/63)` (~24 dB over the ROM). Combined with the 14 dB already in the PCM, the tail sits around 40 dB down at the end of the chip.

### Reconstruction filters

After the DAC, 9090 Ride has transistor/TL072 stages with

| Cap | With | 1-pole fc (approx) |
| --- | --- | --- |
| C151 2.7 nF | R426 10k | 5.9 kHz |
| C136 1.2 nF | R408 5.6k | 23.7 kHz |
| C144 1.0 nF | R412 5.6k | 28.4 kHz |
| C158 390 pF | R447 5.6k | 72.8 kHz |

Exact transistor topology is more than a single RC, but those are the time constants on the board. The DSP keeps the two poles that sit inside the 48 kHz Nyquist band (5.9 kHz and 23.7 kHz) as analog-style one-pole LPFs with **fixed Hz**, independent of Tune. A 30 Hz DC block sits on the mix. Images from the variable-rate ZOH are therefore filtered in the analog domain, which is why Tune does not sound like a modern sampler.

## Extra Edit parameters

Hardware Ride only has **Tune** and **Level**. This unit keeps those as X (PITCH) and Depth (MIX), plus Y (PUMP) for the techno sidechain layer.

Two Edit knobs extend the voice the same way sibling PCM units (HHat) and Roland Cloud’s Ride do:

| Param | Role | Behaviour |
| --- | --- | --- |
| TONE | Reconstruction LPF tilt | Moves the first ~5.9 kHz pole darker ↔ brighter (HHat-style). Second pole stays fixed. |
| DEC | Soft VCA choke | Age-based `exp(-age/τ)` on top of the address envelope. Max = full ROM envelope (hardware). Lower shortens the audible body without time-stretching the sample. |

## What this unit does not do

- It does not interpolate, granulate, or time-stretch. Previous Ride909 versions did, so that decay stayed constant while X changed pitch. That is gone on purpose.
- It does not model every transistor in the VCA, accent/velocity CV, or analog clock jitter.
- It does not include Crash or Hi-Hat ROMs.
- Hardware has one Ride voice. This effect still stacks four overlapping hits so the 3-7-11-15 wash works.

## Regenerating the PCM header

The packed 6-bit array is `plugins/ride909/dsp/ride909_pcm.h` (~24.6 KB). Together with the analog voice the NTS-3 unit fits under the 32 KB genericfx cap (~31.0 KB text+data). `powf`/`expf` are avoided so libm is not linked: Tune uses a 4th-order `2^x` on [-1, 1], filter coeffs are baked for 48 kHz, and the anti-log VCA is a 64-point LUT of `exp(-2.8 * code/63)`.

To rebuild the PCM header from a 32 KB dump or Intel HEX:

```bash
python3 plugins/ride909/scripts/embed_rom.py \
  --rom /path/to/909ride.hex \
  --out plugins/ride909/dsp/ride909_pcm.h
```

The script refuses images that are not 6-bit left-aligned. CRC/SHA1 of the source ROM are written into the header comment.

## Host-side verification

```bash
INC="-I plugins/ride909/dsp -I plugins/common \
  -I third_party/logue-sdk/platform/nts-3_kaoss/common \
  -I third_party/logue-sdk/platform/nts-3_kaoss \
  -I third_party/logue-sdk/platform/ext/CMSIS/CMSIS/Include"

g++ -O2 -std=c++11 $INC plugins/ride909/scripts/measure_params.cc -o /tmp/ride909_params -lm
/tmp/ride909_params
```

`measure_params` checks: Tune ratios match R478+VR30 at panel mid (−6.12 / +9.54 st), TONE brightens the first LPF pole, and DEC shortens the late tail.

## Sources

- Roland TR-909 Service Notes (voicing board, digital cymbal path)
- [MAME `roland_tr909.cpp`](https://github.com/mamedev/mame/blob/master/src/mame/roland/roland_tr909.cpp) ROM region `ride`
- [9090 schematics](https://nuxx.net/files/9090/9090_schems_11sept06.pdf) Ride page
- [Network-909 HiHat/Cymbal notes](http://www.network-909.de/hihatand.htm)
- [Colin Fraser, Cloning the TR-909 Cymbals](http://www.colinfraser.com/tr909/909cyms.htm)
- Sound on Sound / Gordon Reid, Practical Cymbal Synthesis (address-derived envelope)

PCM is the 9090-archived Ride EPROM, matching the MAME hash. Not affiliated with Roland.
