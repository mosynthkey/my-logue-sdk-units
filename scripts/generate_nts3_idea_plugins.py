#!/usr/bin/env python3
"""Generate NTS-3 target boilerplate for the custom FX/OSC idea pack."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
KAOCID_WASM = ROOT / "plugins/kaocid/targets/nts-3_kaoss/wasm.cc"
GLITCH_MAKE = ROOT / "plugins/retrig/targets/nts-3_kaoss/Makefile"

# Param tuple: name, min, max, init, type, frac, frac_mode
# type: none | percent | strings | midi_note
# assign: x | y | depth | none

NONE = "k_unit_param_type_none"
PCT = "k_unit_param_type_percent"
STR = "k_unit_param_type_strings"
NOTE = "k_unit_param_type_midi_note"

PLUGINS = [
    {
        "id": "beatrepeat",
        "class": "BeatRepeat",
        "name": "BeatRepeat",
        "unit_id": "0x00000010U",
        "type": "fx",
        "description": (
            "Clock-synced Beat Repeat for NTS-3. Continuously captures AUDIO IN "
            "(get_raw_input on firmware 1.4+) and stutters slices on the grid. "
            "X = loop length (1/32–2 beats), Y = fire probability, touch = force freeze. "
            "SLICE picks which fragment of the last beat to chew on."
        ),
        "ja": (
            "テンポ同期のBeat Repeatです。AUDIO INを常時取り込み、拍に吸着したスライスを繰り返します。"
            "Xはループ長（1/32〜2拍）、Yは発火確率、タッチで強制フリーズです。"
        ),
        "params": [
            ("LEN", 0, 1023, 410, NONE, 0, 0, "x", "Loop length, tempo-synced from 1/32 to 2 beats"),
            ("PROB", 0, 1023, 700, NONE, 0, 0, "y", "Chance a new slice starts on the next 16th"),
            ("MIX", 0, 1000, 1000, PCT, 1, 1, "depth", "Dry/wet while repeating"),
            ("SLICE", 0, 1023, 0, NONE, 0, 0, "none", "Which fragment of the captured beat to loop"),
            ("FEED", 0, 1023, 200, NONE, 0, 0, "none", "How much the repeated slice recirculates"),
        ],
    },
    {
        "id": "echofreeze",
        "class": "EchoFreeze",
        "name": "EchoFreeze",
        "unit_id": "0x00000011U",
        "type": "fx",
        "description": (
            "Octatrack-style echo freeze. A tempo-synced delay keeps writing AUDIO IN "
            "until you lock it. Touch toggles Lock: Send drops out and the captured buffer "
            "loops forever. X = buffer length in beats, Y = playback rate / pitch."
        ),
        "ja": (
            "Octatrack風のEcho Freezeです。タッチでディレイバッファをLockし、入力を止めずに中身だけ無限再生します。"
            "Xはバッファ長（拍）、Yは再生ピッチです。"
        ),
        "params": [
            ("SIZE", 0, 1023, 512, NONE, 0, 0, "x", "Locked buffer length, tempo-synced (1/8–2 beats)"),
            ("PITCH", 0, 1023, 512, NONE, 0, 0, "y", "Playback rate when locked (down an octave to up a fifth)"),
            ("MIX", 0, 1000, 1000, PCT, 1, 1, "depth", "Wet level of the frozen buffer"),
            ("DECAY", 0, 1023, 900, NONE, 0, 0, "none", "Feedback fade while locked"),
            ("MODE", 0, 1, 0, STR, 0, 0, "none", "HOLD = lock while touching, TOGL = tap to lock/unlock"),
        ],
    },
    {
        "id": "ringexcit",
        "class": "RingExcit",
        "name": "RingExcit",
        "unit_id": "0x00000012U",
        "type": "fx",
        "description": (
            "Input-excited resonator. Kick or noise plucks a Karplus-Strong string plus "
            "three modal partials. X = frequency / structure, Y = brightness and decay. "
            "Touch fires an internal noise pluck so it also works as a one-shot."
        ),
        "ja": (
            "入力で弦/モーダル共振体を叩くエキサイターです。キックやノイズがピッチのあるトーンになります。"
            "Xは周波数、Yは明るさ/減衰、タッチで内部ノイズ励起です。"
        ),
        "params": [
            ("FREQ", 0, 1023, 280, NONE, 0, 0, "x", "Fundamental frequency (~40–800 Hz)"),
            ("TONE", 0, 1023, 620, NONE, 0, 0, "y", "Brightness and damping"),
            ("MIX", 0, 1000, 800, PCT, 1, 1, "depth", "Dry/wet"),
            ("STRUC", 0, 2, 0, STR, 0, 0, "none", "Resonator: STR, MOD, HYB"),
            ("POS", 0, 1023, 400, NONE, 0, 0, "none", "Pickup / partial spread"),
        ],
    },
    {
        "id": "speccloud",
        "class": "SpecCloud",
        "name": "SpecCloud",
        "unit_id": "0x00000013U",
        "type": "fx",
        "description": (
            "FFT spectral band clouds. Input is split into logarithmic bands whose gains "
            "are randomly re-rolled. X = band count, Y = re-roll probability / smoothing. "
            "Touch instantly redraws the cloud."
        ),
        "ja": (
            "対数帯域のゲインを確率で張り替えるスペクトラル雲です。フィルタスイープとは違い、帯域がちらつきます。"
            "Xは帯域数、Yは再ロール確率、タップでスペクトラム再抽選です。"
        ),
        "params": [
            ("BANDS", 0, 1023, 512, NONE, 0, 0, "x", "Number of log bands (4–32)"),
            ("FLUX", 0, 1023, 380, NONE, 0, 0, "y", "Per-hop chance a band gain is redrawn"),
            ("MIX", 0, 1000, 1000, PCT, 1, 1, "depth", "Dry/wet"),
            ("SMOOTH", 0, 1023, 500, NONE, 0, 0, "none", "How slowly band gains glide after a roll"),
            ("SEED", 0, 1023, 1, NONE, 0, 0, "none", "Cloud seed for repeatable chaos"),
        ],
    },
    {
        "id": "warpsmorph",
        "class": "WarpsMorph",
        "name": "WarpsMorph",
        "unit_id": "0x00000014U",
        "type": "fx",
        "description": (
            "Cross-mod morph in the spirit of Mutable Warps. One axis walks diode ring, "
            "XOR, comparator, mini-vocoder, and wavefolder. Y sets the internal carrier "
            "and harshness. Touch blends in the internal oscillator."
        ),
        "ja": (
            "ダイオードリング / XOR / コンパレータ / ミニボコーダ / フォールダを1軸で横断するクロス変調です。"
            "Xはアルゴリズム、Yはキャリヤ周波数、タッチで内部キャリヤ混合です。"
        ),
        "params": [
            ("ALGO", 0, 1023, 0, NONE, 0, 0, "x", "Morph: RING → XOR → CMP → VOC → FOLD"),
            ("TONE", 0, 1023, 360, NONE, 0, 0, "y", "Internal carrier frequency / harshness"),
            ("MIX", 0, 1000, 850, PCT, 1, 1, "depth", "Dry/wet"),
            ("CARR", 0, 1023, 200, NONE, 0, 0, "none", "Internal carrier mix when the pad is up"),
            ("DRV", 0, 1023, 400, NONE, 0, 0, "none", "Output fold / drive"),
        ],
    },
    {
        "id": "pumpduck",
        "class": "PumpDuck",
        "name": "PumpDuck",
        "unit_id": "0x00000015U",
        "type": "fx",
        "description": (
            "Envelope-follower sidechain. Follows AUDIO IN (raw input when available) and "
            "ducks amplitude, a resonant filter, or an internal plate. X = attack/release "
            "feel, Y = depth. Touch cycles the duck destination."
        ),
        "ja": (
            "エンベロープフォロワーで音量 / フィルタ / 内部Plateをダックします。キック込みのステムを呼吸させます。"
            "Xはアタック/リリース感、Yは深さ、タッチでダック先切替です。"
        ),
        "params": [
            ("TIME", 0, 1023, 420, NONE, 0, 0, "x", "Follower attack/release (pump feel)"),
            ("DPTH", 0, 1023, 780, NONE, 0, 0, "y", "How far the destination is ducked"),
            ("MIX", 0, 1000, 1000, PCT, 1, 1, "depth", "Dry/wet of the processed path"),
            ("DEST", 0, 2, 0, STR, 0, 0, "none", "Duck target: AMP, FILT, PLAT"),
            ("HPF", 0, 1023, 180, NONE, 0, 0, "none", "High-pass the detector so it locks to kick"),
        ],
    },
    {
        "id": "rmxscene",
        "class": "RmxScene",
        "name": "RmxScene",
        "unit_id": "0x00000016U",
        "type": "fx",
        "description": (
            "Pioneer RMX-style Scene FX with a defined release. X morphs Build (noise+HPF) "
            "into Break (crush+echo). Y is intensity. Lifting the pad leaves a short echo "
            "or snaps back, instead of just dropping the wet mix."
        ),
        "ja": (
            "Pioneer RMXのScene FX思想です。XはBuild（ノイズ+HPF）とBreak（クラッシュ+エコー）のモーフ、"
            "Yは深さ。離すとRelease Echoで本編に戻ります。"
        ),
        "params": [
            ("SCENE", 0, 1023, 700, NONE, 0, 0, "x", "Build (right) ↔ Break (left) scene morph"),
            ("DPTH", 0, 1023, 700, NONE, 0, 0, "y", "Scene intensity"),
            ("MIX", 0, 1000, 1000, PCT, 1, 1, "depth", "Wet level while the pad is held"),
            ("REL", 0, 1, 0, STR, 0, 0, "none", "Release: ECHO tail or SNAP back"),
            ("NOISE", 0, 1023, 512, NONE, 0, 0, "none", "Build-up noise amount"),
        ],
    },
    {
        "id": "eucgate",
        "class": "EucGate",
        "name": "EucGate",
        "unit_id": "0x00000017U",
        "type": "fx",
        "description": (
            "DJ Transform gate plus Euclidean / probability chopping. Not an LFO tremolo: "
            "hits sit on a musical grid. X = hit count, Y = duty or probability. "
            "Touch = Fill (all gates open / roll)."
        ),
        "ja": (
            "DJ TransformとEuclidean / 確率ゲートを統合したリズムミュートです。"
            "Xはヒット数、Yはデューティ/確率、タッチでFill（全開ゲート）です。"
        ),
        "params": [
            ("HITS", 0, 1023, 400, NONE, 0, 0, "x", "Euclidean hits per bar (1–16)"),
            ("DUTY", 0, 1023, 550, NONE, 0, 0, "y", "Gate length and extra probability"),
            ("MIX", 0, 1000, 1000, PCT, 1, 1, "depth", "How completely closed steps mute"),
            ("STEPS", 0, 2, 2, STR, 0, 0, "none", "Grid: 8, 12, or 16 steps"),
            ("ROT", 0, 1023, 0, NONE, 0, 0, "none", "Rotate the Euclidean pattern"),
        ],
    },
    {
        "id": "talkform",
        "class": "TalkForm",
        "name": "TalkForm",
        "unit_id": "0x00000018U",
        "type": "fx",
        "description": (
            "Talk / Digi Talk formant filter for input. X and Y set the first and second "
            "formants (vowel mouth shape). Touch snaps to a quantized Japanese-vowel grid "
            "so the pad reads as あいうえお instead of a free sweep."
        ),
        "ja": (
            "入力を母音フォルマントで喋らせるトークフィルタです。Vocoderともトーク合成OSCとも別物です。"
            "Xは第1フォルマント、Yは第2、タッチで母音量子化です。"
        ),
        "params": [
            ("F1", 0, 1023, 400, NONE, 0, 0, "x", "First formant (vowel brightness)"),
            ("F2", 0, 1023, 600, NONE, 0, 0, "y", "Second formant (vowel width)"),
            ("MIX", 0, 1000, 1000, PCT, 1, 1, "depth", "Dry/wet"),
            ("Q", 0, 1023, 640, NONE, 0, 0, "none", "Formant resonance"),
            ("DIGI", 0, 1, 0, STR, 0, 0, "none", "FREE sweep or DIGI vowel snap (also via touch)"),
        ],
    },
    {
        "id": "mswidth",
        "class": "MsWidth",
        "name": "MsWidth",
        "unit_id": "0x00000019U",
        "type": "fx",
        "description": (
            "Mid/Side width and center kill. X = Mid gain, Y = Side gain. Touch snaps to "
            "a Center Kill preset (mute Mid, keep Side) for vocal-away DJ edits."
        ),
        "ja": (
            "M/S処理でサイド拡張やミッド（ボーカル寄り）殺しができます。"
            "XはMidゲイン、YはSideゲイン、タッチでCenter Killにスナップします。"
        ),
        "params": [
            ("MID", 0, 1023, 512, NONE, 0, 0, "x", "Mid (center) gain, unity at noon"),
            ("SIDE", 0, 1023, 640, NONE, 0, 0, "y", "Side (width) gain, unity at noon"),
            ("MIX", 0, 1000, 1000, PCT, 1, 1, "depth", "Dry/wet of the M/S stage"),
            ("HP", 0, 1023, 180, NONE, 0, 0, "none", "High-pass the Side channel so kick stays centered"),
        ],
    },
    {
        "id": "databend",
        "class": "DataBend",
        "name": "DataBend",
        "unit_id": "0x0000001AU",
        "type": "fx",
        "description": (
            "Media-failure buffer: CD skip, torn tape, and bit-rot as one macro. "
            "X = Bend (stutter / varispeed), Y = Corrupt (dropouts and broken words). "
            "Touch freezes the damaged frame into a noise wall."
        ),
        "ja": (
            "CDスキップ / 破損データ / テープ引っかかりを1マクロにしたメディア故障シミュです。"
            "XはBend、YはCorrupt、タッチで破損フレームをフリーズします。"
        ),
        "params": [
            ("BEND", 0, 1023, 220, NONE, 0, 0, "x", "Stutter / varispeed bend amount"),
            ("CRUD", 0, 1023, 180, NONE, 0, 0, "y", "Bit-rot, dropouts, held samples"),
            ("MIX", 0, 1000, 1000, PCT, 1, 1, "depth", "Dry/wet"),
            ("RATE", 0, 1023, 400, NONE, 0, 0, "none", "How often a new glitch event fires"),
            ("BITS", 0, 1023, 0, NONE, 0, 0, "none", "Extra word-length crush"),
        ],
    },
    {
        "id": "reesephr",
        "class": "ReesePhr",
        "name": "ReesePhr",
        "unit_id": "0x0000001BU",
        "type": "synth",
        "description": (
            "Detuned Reese drone with a sparse phrase generator. Touch gates a beating "
            "supersaw; Y sets detune / beat speed. Every 1–2 bars the root may walk a "
            "minor third inside the scale so the pressure changes without a melody."
        ),
        "ja": (
            "デチューンソウのReeseドローンです。Hooverではなく、低速で疎に動く低域フレーズ付き。"
            "Xはピッチ、Yはデチューン、タッチでゲートです。"
        ),
        "params": [
            ("PITCH", 0, 1023, 360, NONE, 0, 0, "x", "Root pitch of the drone"),
            ("DETUN", 0, 1023, 520, NONE, 0, 0, "y", "Detune / beating speed"),
            ("MIX", 0, 1000, 1000, PCT, 1, 1, "depth", "Dry/wet"),
            ("ROOT", 24, 48, 36, NOTE, 0, 0, "none", "Phrase key"),
            ("RATE", 0, 1023, 300, NONE, 0, 0, "none", "How often the drone walks"),
            ("SUB", 0, 1023, 400, NONE, 0, 0, "none", "Sub square one octave down"),
        ],
    },
    {
        "id": "perciter",
        "class": "PercIter",
        "name": "PercIter",
        "unit_id": "0x0000001CU",
        "type": "synth",
        "description": (
            "Basimilus-inspired percussion pad. Additive / FM body, noise, and folder "
            "morph from Skin to Metal on Y. X is pitch. Each touch is a trigger; "
            "rapid taps make a fill."
        ),
        "ja": (
            "Skin / Liquid / Metalをモーフするパーカッションパッドです。"
            "Xはピッチ、YはSkin↔Metal、タッチでトリガです。"
        ),
        "params": [
            ("PITCH", 0, 1023, 420, NONE, 0, 0, "x", "Body pitch"),
            ("MORPH", 0, 1023, 200, NONE, 0, 0, "y", "Skin ↔ Metal morph"),
            ("MIX", 0, 1000, 1000, PCT, 1, 1, "depth", "Dry/wet"),
            ("FOLD", 0, 1023, 300, NONE, 0, 0, "none", "Wavefold amount"),
            ("DEC", 0, 1023, 480, NONE, 0, 0, "none", "Amp / noise decay"),
            ("NOIS", 0, 1023, 350, NONE, 0, 0, "none", "Noise / rattle mix"),
            ("MODE", 0, 2, 0, STR, 0, 0, "none", "SKIN, LIQ, METL bias"),
        ],
    },
    {
        "id": "shepard",
        "class": "Shepard",
        "name": "Shepard",
        "unit_id": "0x0000001DU",
        "type": "synth",
        "description": (
            "Infinite Shepard / Risset riser. Not a reverb swell: stacked octaves keep "
            "climbing forever. X = rise speed, Y = brightness / noise. Hold to build; "
            "release dumps the tone so the drop can hit."
        ),
        "ja": (
            "ピッチが永遠に上がる錯覚のシェパード・ライザーです。純正RISER REVERBとは別物。"
            "Xは上昇速度、Yは明るさ、離すと急減衰でドロップ合図になります。"
        ),
        "params": [
            ("RATE", 0, 1023, 400, NONE, 0, 0, "x", "How fast the tone climbs"),
            ("TONE", 0, 1023, 560, NONE, 0, 0, "y", "Brightness and noise mix"),
            ("MIX", 0, 1000, 1000, PCT, 1, 1, "depth", "Dry/wet"),
            ("PART", 0, 1023, 700, NONE, 0, 0, "none", "How many octave layers stay audible"),
            ("NOIS", 0, 1023, 220, NONE, 0, 0, "none", "Air / noise layer for RMX-style spiral"),
        ],
    },
    {
        "id": "gridsdrum",
        "class": "GridsDrum",
        "name": "GridsDrum",
        "unit_id": "0x0000001EU",
        "type": "synth",
        "description": (
            "Generative BD/SD/HH phrase engine mapped like Mutable Grids. X = kick/snare "
            "map, Y = hat density. Hold the pad to run the clocked sequencer. Flicking "
            "into the top-right corner fires a one-bar Fill."
        ),
        "ja": (
            "MI Grids的なXYでジャンル密度を決め、BD/SD/HHフレーズを生成します。"
            "タッチでシーケンサ走行、右上フリックで1小節Fillです。"
        ),
        "params": [
            ("MAPX", 0, 1023, 300, NONE, 0, 0, "x", "Pattern map X (kick vs snare geography)"),
            ("HATS", 0, 1023, 450, NONE, 0, 0, "y", "Hi-hat density"),
            ("MIX", 0, 1000, 1000, PCT, 1, 1, "depth", "Dry/wet"),
            ("TONE", 0, 1023, 480, NONE, 0, 0, "none", "Drum voicing (pitch / noise color)"),
            ("DEC", 0, 1023, 500, NONE, 0, 0, "none", "Shared decay"),
            ("SWING", 0, 1023, 0, NONE, 0, 0, "none", "16th swing amount"),
        ],
    },
    {
        "id": "riffdice",
        "class": "RiffDice",
        "name": "RiffDice",
        "unit_id": "0x0000001FU",
        "type": "synth",
        "description": (
            "Elektron-style conditional riff generator. A 16-step scale sequence carries "
            "always / 1:2 / percent trigs. Touch runs the phrase. X walks the root, "
            "Y raises the chance that conditional steps fire, so the same pad position "
            "re-rolls each bar."
        ),
        "ja": (
            "Elektronのconditional trigを音源側に内蔵したリフ・ダイスです。"
            "Xはルート、Yは密度/確率、タッチでフレーズ走行です。"
        ),
        "params": [
            ("ROOT", 24, 48, 36, NOTE, 0, 0, "x", "Phrase root (also driven by the pad X)"),
            ("PROB", 0, 1023, 500, NONE, 0, 0, "y", "How easily 1:2 and % steps fire"),
            ("MIX", 0, 1000, 1000, PCT, 1, 1, "depth", "Dry/wet"),
            ("SCALE", 0, 3, 0, STR, 0, 0, "none", "MIN, MAJ, DOR, PENT"),
            ("LEN", 0, 1, 1, STR, 0, 0, "none", "Phrase length 8 or 16"),
            ("SLIDE", 0, 1023, 300, NONE, 0, 0, "none", "Portamento on marked steps"),
            ("OCT", 0, 2, 1, STR, 0, 0, "none", "Octave register"),
        ],
    },
    {
        "id": "chordres",
        "class": "ChordRes",
        "name": "ChordRes",
        "unit_id": "0x00000020U",
        "type": "synth",
        "description": (
            "Pad chords with a tempo-synced residue arpeggio after release. Touch plays "
            "a voicing; lift starts a decaying 1/16 descent that can overlap a second "
            "chord. X = progression axis, Y = voicing spread."
        ),
        "ja": (
            "触った位置のコードを鳴らしたあと、離すと残存アルペジオがテンポ同期で溶けていきます。"
            "Xはコード進行軸、Yはヴォイシング、離すとResidueが始まります。"
        ),
        "params": [
            ("PROG", 0, 1023, 0, NONE, 0, 0, "x", "Diatonic progression axis (I–vi–IV–V…)"),
            ("VOIC", 0, 1023, 400, NONE, 0, 0, "y", "Voicing openness"),
            ("MIX", 0, 1000, 1000, PCT, 1, 1, "depth", "Dry/wet"),
            ("ROOT", 24, 48, 36, NOTE, 0, 0, "none", "Key center"),
            ("TYPE", 0, 1, 0, STR, 0, 0, "none", "MIN or MAJ"),
            ("RES", 0, 1023, 640, NONE, 0, 0, "none", "Residue length / density"),
            ("DEC", 0, 1023, 500, NONE, 0, 0, "none", "Residue fade time"),
        ],
    },
]

LICENSE = """/*
    BSD 3-Clause License

    Copyright (c) 2023, KORG INC.
    Copyright (c) 2026, {name} contributors
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this
      list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.

    * Neither the name of the copyright holder nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
    FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
    OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

//*/
"""

ASSIGN = {
    "x": "k_genericfx_param_assign_x",
    "y": "k_genericfx_param_assign_y",
    "depth": "k_genericfx_param_assign_depth",
    "none": "k_genericfx_param_assign_none",
}

UNIT_CC = """{license}

/*
 * File: unit.cc
 *
 * NTS-3 generic effect unit interface for {name}
 *
 */

#include "{header}.h"
#include "unit_genericfx.h"
#include "utils/int_math.h"
#include <algorithm>

static {cls} s_{ident}_instance;
static unit_runtime_desc_t s_runtime_desc;
static unit_runtime_genericfx_get_raw_input_ptr s_get_raw_input = nullptr;

static int32_t cached_values[UNIT_GENERICFX_MAX_PARAM_COUNT];

__unit_callback int8_t unit_init(const unit_runtime_desc_t *desc)
{{
  if (!desc)
    return k_unit_err_undef;

  if (desc->target != unit_header.common.target)
    return k_unit_err_target;

  if (!UNIT_API_IS_COMPAT(desc->api))
    return k_unit_err_api_version;

  if (desc->samplerate != s_{ident}_instance.getSampleRate())
    return k_unit_err_samplerate;

  if (desc->input_channels != 2 || desc->output_channels != 2)
    return k_unit_err_geometry;

  const uint32_t buffer_floats = s_{ident}_instance.getBufferSize();
  float *allocated_buffer = nullptr;
  if (buffer_floats > 0U)
  {{
    if (!desc->hooks.sdram_alloc)
      return k_unit_err_memory;
    allocated_buffer =
        (float *)desc->hooks.sdram_alloc(buffer_floats * sizeof(float));
    if (!allocated_buffer)
      return k_unit_err_memory;
    std::fill(allocated_buffer, allocated_buffer + buffer_floats, 0.f);
  }}

  s_runtime_desc = *desc;
  s_get_raw_input = nullptr;
  if (s_runtime_desc.hooks.runtime_context != nullptr)
  {{
    const unit_runtime_genericfx_context_t *fx_context =
        static_cast<const unit_runtime_genericfx_context_t *>(s_runtime_desc.hooks.runtime_context);
    s_get_raw_input = fx_context->get_raw_input;
  }}
  s_{ident}_instance.init(allocated_buffer);

  for (uint8_t paramIndex = 0; paramIndex < UNIT_GENERICFX_MAX_PARAM_COUNT; ++paramIndex)
    cached_values[paramIndex] = static_cast<int32_t>(unit_header.common.params[paramIndex].init);

  for (uint8_t paramIndex = 0; paramIndex < unit_header.common.num_params; ++paramIndex)
    s_{ident}_instance.setParameter(paramIndex, cached_values[paramIndex]);

  return k_unit_err_none;
}}

__unit_callback void unit_teardown()
{{
  s_{ident}_instance.teardown();
  s_get_raw_input = nullptr;
}}

__unit_callback void unit_reset()
{{
  s_{ident}_instance.reset();
}}

__unit_callback void unit_resume()
{{
  s_{ident}_instance.resume();
}}

__unit_callback void unit_suspend()
{{
  s_{ident}_instance.suspend();
}}

__unit_callback void unit_render(const float *in, float *out, uint32_t frames)
{{
  const float *raw = nullptr;
  if (s_runtime_desc.hooks.runtime_context != nullptr)
  {{
    const unit_runtime_genericfx_context_t *fx_context =
        static_cast<const unit_runtime_genericfx_context_t *>(s_runtime_desc.hooks.runtime_context);
    if (fx_context->get_raw_input != nullptr)
      raw = fx_context->get_raw_input();
  }}
  if (raw == nullptr && s_get_raw_input != nullptr)
    raw = s_get_raw_input();
  s_{ident}_instance.process(in, raw, out, frames);
}}

__unit_callback void unit_set_param_value(uint8_t id, int32_t value)
{{
  value = clipminmaxi32(unit_header.common.params[id].min, value, unit_header.common.params[id].max);
  cached_values[id] = value;
  s_{ident}_instance.setParameter(id, value);
}}

__unit_callback int32_t unit_get_param_value(uint8_t id)
{{
  return cached_values[id];
}}

__unit_callback const char *unit_get_param_str_value(uint8_t id, int32_t value)
{{
  value = clipminmaxi32(unit_header.common.params[id].min, value, unit_header.common.params[id].max);
  return s_{ident}_instance.getParameterStrValue(id, value);
}}

__unit_callback void unit_touch_event(uint8_t id, uint8_t phase, uint32_t x, uint32_t y)
{{
  s_{ident}_instance.touchEvent(id, phase, x, y);
}}

__unit_callback void unit_set_tempo(uint32_t tempo)
{{
  float bpm = (tempo >> 16) + (tempo & 0xFFFF) / static_cast<float>(0x10000);
  s_{ident}_instance.setTempo(bpm);
}}

__unit_callback void unit_tempo_4ppqn_tick(uint32_t counter)
{{
  s_{ident}_instance.tempo4ppqnTick(counter);
}}
"""

CONFIG_MK = """##############################################################################
# Configuration for Makefile
#

PROJECT := {ident}
PROJECT_TYPE := genericfx

##############################################################################
# Sources
#

UCSRC = header.c

UCXXSRC = unit.cc

UASMSRC =

UASMXSRC =

##############################################################################
# Include Paths
#

UINCDIR = ../../dsp ../../../common

##############################################################################
# Library Paths
#

ULIBDIR =

##############################################################################
# Libraries
#

ULIBS = -lm

##############################################################################
# Macros
#

UDEFS =
"""


def param_row(p):
    name, pmin, pmax, init, ptype, frac, frac_mode, _assign, _detail = p
    center = 0
    return f'            {{{pmin}, {pmax}, {center}, {init}, {ptype}, {frac}, {frac_mode}, 0, {{"{name}"}}}}'


def mapping_row(p):
    name, pmin, pmax, init, _ptype, _frac, _frac_mode, assign, _detail = p
    return (
        f"        {{{ASSIGN[assign]}, k_genericfx_curve_linear, "
        f"k_genericfx_curve_unipolar, {pmin}, {pmax}, {init}}}"
    )


def header_c(plugin):
    params = list(plugin["params"])
    while len(params) < 8:
        params.append(("", 0, 0, 0, NONE, 0, 0, "none", ""))
    param_lines = ",\n".join(param_row(p) if p[0] else '            {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}}' for p in params)
    map_lines = ",\n".join(mapping_row(p) if p[0] else "        {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 0, 0}" for p in params)
    real_count = sum(1 for p in plugin["params"])
    return f"""{LICENSE.format(name=plugin["name"])}

/*
 * File: header.c
 *
 * NTS-3 generic effect unit header for {plugin["name"]}
 *
 */

#include "unit_genericfx.h"
#include "dev_id.h"

const __unit_header genericfx_unit_header_t unit_header = {{
    .common = {{
        .header_size = sizeof(genericfx_unit_header_t),
        .target = UNIT_TARGET_PLATFORM | k_unit_module_genericfx,
        .api = UNIT_API_VERSION,
        .dev_id = MLSA_DEV_ID,
        .unit_id = {plugin["unit_id"]},
        .version = MLSA_VERSION_EXPERIMENTAL,
        .name = "{plugin["name"]}",
        .num_params = {real_count},
        .params = {{
{param_lines}}},
    }},
    .default_mappings = {{
{map_lines},
    }},
}};
"""


def plugin_json(plugin):
    import json

    role = {"x": "X", "y": "Y", "depth": "Depth", "none": "Edit"}
    params = [
        {"name": p[0], "role": role[p[7]], "detail": p[8]}
        for p in plugin["params"]
    ]
    return json.dumps(
        {
            "id": plugin["id"],
            "experimental": True,
            "name": plugin["name"],
            "type": plugin["type"],
            "targets": ["nts-3_kaoss"],
            "description": plugin["description"],
            "params": params,
        },
        indent=2,
    ) + "\n"


def write_wasm(plugin, dest):
    text = KAOCID_WASM.read_text()
    text = text.replace('#include "kaocid.h"', f'#include "{plugin["id"]}.h"')
    text = text.replace("Kaocid processor;", f'{plugin["class"]} processor;')
    text = text.replace("audioThreadStack[4096]", "audioThreadStack[8192]")
    dest.write_text(text)


def generate_one(plugin):
    plugin_dir = ROOT / "plugins" / plugin["id"]
    target = plugin_dir / "targets" / "nts-3_kaoss"
    dsp = plugin_dir / "dsp"
    target.mkdir(parents=True, exist_ok=True)
    dsp.mkdir(parents=True, exist_ok=True)

    (plugin_dir / "plugin.json").write_text(plugin_json(plugin))
    (target / "config.mk").write_text(CONFIG_MK.format(ident=plugin["id"]))
    (target / "header.c").write_text(header_c(plugin))
    (target / "unit.cc").write_text(
        UNIT_CC.format(
            license=LICENSE.format(name=plugin["name"]),
            name=plugin["name"],
            header=plugin["id"],
            cls=plugin["class"],
            ident=plugin["id"],
        )
    )
    makefile = GLITCH_MAKE.read_text()
    (target / "Makefile").write_text(makefile)
    write_wasm(plugin, target / "wasm.cc")


def main():
    for plugin in PLUGINS:
        generate_one(plugin)
        print("generated", plugin["id"])


if __name__ == "__main__":
    main()
