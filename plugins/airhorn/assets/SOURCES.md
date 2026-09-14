# Sample sources

Embedded data is a single 16-bit PCM loop at 24 kHz. The settled DJ-horn tone is stored
as a short seamless loop; the opening pitch drop is a pitch envelope.

Measured settled fundamental on the shipping loop is ~302.03 Hz (about D4 + 49 cents),
not tempered D#4. Playback applies `kAirhornTuneRatio` so Fixed lands on exact D4
(MIDI 62); Key mode tracks concert pitch from that root.

| Horn | File | License | Source | Playback |
| --- | --- | --- | --- | --- |
| DJ | `assets/dj-airhorn.wav` | Apache-2.0 | [brendanjryan/airhorn](https://github.com/brendanjryan/airhorn) | loop + pitch env (~+6 semitones → settle) |

The WAV is not committed (see `.gitignore`). Fetch it before regenerating:

```bash
curl -L -o plugins/airhorn/assets/dj-airhorn.wav \
  https://raw.githubusercontent.com/brendanjryan/airhorn/master/assets/airhorn.wav
# sha256 66dc09de689ff2302590c4c75bb5a9de292d5003ceacedb57a1f17524e0510fb
```

Regenerate `dsp/airhorn_pcm.h` after swapping the source WAV (defaults pin the shipping
aligned-late cut at 24 kHz: start=18568, length=3020):

```bash
python3 plugins/airhorn/scripts/embed_pcm.py \
  --out plugins/airhorn/dsp/airhorn_pcm.h \
  plugins/airhorn/assets/dj-airhorn.wav
```

Pass `--loop-length 0` to re-run the automatic search instead of the pinned cut.

## Loop conditioning

The recorded horn drifts about 10 cents flat, loses high end, and slowly changes
loudness over the sustain. A long cut restarts on brighter / louder material than it
ended on, which is heard as a once-per-loop click or volume jump. `embed_pcm.py`
answers that by:

1. Cutting a short slice (~126 ms / ~38 cycles) from late early-sustain, where the
   tone is settled but has not yet started the release fade.
2. Ranking auto-search candidates by phase match, head/tail level agreement, and
   brightness agreement (optional; shipping uses the pinned cut above).
3. Applying a smoothstep gain ramp so the cut's tail matches the head before the
   seam is baked (`level trend compensate`).
4. Folding the last 12 fundamental periods back into the loop head as a crossfade.
5. Dividing out any remaining slow loudness contour with a circular RMS flatten.

Regeneration prints the resulting seam figures; a healthy loop lands near or below
0 dB on the seam excess and well under 0.5 dB on the level swell:

```
period=79.359 samples (302.42 Hz) crossfade=952 samples
loop start=18568 (774 ms) length=3020 cycles=38.05 match=0.901 (pinned)
level trend compensate: -0.18 dB (tail -> head)
level flatten: swell 0.52 dB -> 0.06 dB (removed 0.46 dB)
seam: sample jump=0.05937 excess over loop interior=+0.03 dB level swell=0.06 dB
```

`scripts/fade_experiment.py` renders the loop through a model of the engine and scores
the wrap with spectral flux. Keep the single wrapping player at playback time; fix
seams and slow swell in `embed_pcm.py` instead of adding a runtime dual-player
crossfade (that re-introduces level pumping on the baked loop).

```bash
python3 plugins/airhorn/scripts/fade_experiment.py --compare-loop-modes
```
