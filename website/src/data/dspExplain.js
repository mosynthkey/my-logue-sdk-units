/** DSP architecture notes and Mermaid block diagrams per plugin id. */
export const dspExplainById = {
  airfm: {
    en: "Two-operator phase-modulation FM. Pad X (CARR) sets carrier frequency, Y (MOD) sets modulator frequency; touch gates a short amp envelope. INDEX is FM depth. The carrier is drive-shaped (tanh), then high-pass and fixed low-pass filtered. Dry audio input always passes; DRYWET scales the FM wet level added in parallel.",
    ja: "2オペレーターの位相変調FMです。パッドX（CARR）がキャリア周波数、Y（MOD）がモジュレータ周波数、タッチで短いアンプエンベロープを開きます。INDEXはFMの深さです。tanhドライブのあとHPFと固定LPFを通し、入力は常にバイパスしつつDRYWETでFMウェットを加算します。",
    mermaid: `flowchart LR
  CARR[CARR / Pad X] --> Car[Carrier PM sine]
  MOD[MOD / Pad Y] --> Mod[Modulator sine]
  Mod --> Car
  INDEX[INDEX] --> Car
  Touch[Touch gate] --> Env[Amp env]
  Car --> Drive[tanh drive]
  Env --> Drive
  Drive --> HPF[HPF] --> LPF[LPF]
  DRYWET[DRYWET] --> LPF
  In[Audio in] --> Mix[Sum]
  LPF --> Mix --> Out[Stereo out]`,
  },
  airhorn: {
    en: "Polyphonic fixed-pitch air-horn sample player. Note or touch triggers voices that Hermite-read a looping PCM with an opening pitch envelope, attack, and natural or release fade. FX targets can blend dry/wet.",
    ja: "固定ピッチのエアホーンPCMを複数ボイスで再生します。トリガでループ読み出しし、頭のピッチ包絡とアタック／自然減衰・リリースで消えます。FXターゲットではドライ／ウェット混合できます。",
    mermaid: `flowchart LR
  Trig[Note or Touch] --> Voice[Voice amp and pitch env]
  PCM[Embedded PCM loop] --> Herm[Hermite read]
  Voice --> Herm
  Herm --> Mix[Sum voices] --> Out[Wet or dry-wet]`,
  },
  amentime: {
    en: "Pad-gated 1-bar PCM slicer. An internal sample clock (not 4ppqn) walks equal 16th/32nd slices of a synthesized amen-style break from a fixed origin — tap XY does not pick the start 16th (REVS is X; STRT is Edit). Playback rate is PCM length over host bar length so pitch tracks BPM; TUNE is an extra octave. Two voices crossfade; slices wrap the bar.",
    ja: "パッド・ゲートの1小節PCMスライサーです。内部サンプル時計（4ppqnではない）で合成amen風ブレイクを等分スライスします。タップ位置は開始16分を決めず（XはREVS、STRTはEdit）、常にステップ同期で歩きます。再生速度はPCM長／ホスト1小節なのでピッチがBPMに追従し、TUNEで±1octします。",
    mermaid: `flowchart LR
  Pad[Pad gate] --> Clock[Internal slice clock]
  BPM[Host BPM] --> Rate[PCM length over bar]
  Edit[STRT Edit] --> Slice[SIZE grid walk]
  Clock --> Slice
  PCM[Synth 12 kHz PCM] --> Read[Linear wrap read]
  Slice --> Read
  Rate --> Read
  Read --> Xfade[2-voice xfade] --> Mix[Dry or wet] --> Out[Out]
  In[Audio in] --> Mix`,
  },
  wavslice: {
    en: "Same pad slicer as AmenTime, for any 1-bar WAV. Tap position does not pick the start 16th. Build embeds assets/loop.wav when present, otherwise the shipped CC0 default-loop.wav backbeat.",
    ja: "AmenTimeと同じパッド・スライサーで、任意の1小節WAVを再生します。タップ位置は開始16分を決めません。assets/loop.wav があればそれを埋め、無ければ同梱のCC0ドラムループを使います。",
    mermaid: `flowchart LR
  Wav[loop.wav or default-loop.wav] --> PCM[12 kHz 8-bit PCM]
  Pad[Pad gate] --> Clock[Internal slice clock]
  BPM[Host BPM] --> Rate[PCM length over bar]
  Edit[STRT Edit] --> Slice[SIZE grid walk]
  Clock --> Slice
  PCM --> Read[Linear wrap read]
  Slice --> Read
  Rate --> Read
  Read --> Xfade[2-voice xfade] --> Mix[Dry or wet] --> Out[Out]
  In[Audio in] --> Mix`,
  },
  beatrepeat: {
    en: "Stereo ring buffer of live input. Pad gates only; slices arm on the host 16th grid (internal 16th fallback), with PROB re-arm while held, then crossfade dry/wet by Mix.",
    ja: "入力をステレオリングバッファに常時録音します。パッドはゲートのみで、ホスト16分（なければ内部16分）でスライスを掴み、ホールド中はPROBで再アーム。Mixでドライ／ウェットします。",
    mermaid: `flowchart LR
  In[Live in] --> Buf[Stereo ring buffer]
  Pad[Pad gate] --> Hold[Hold]
  Clock[Host 16th or internal] --> Arm[Arm slice loop]
  Hold --> Arm
  Buf --> Loop[Loop and feedback]
  Arm --> Loop
  In --> Mix[Dry or wet] --> Out[Out]
  Loop --> Mix`,
  },
  databend: {
    en: "Media-failure buffer FX with varispeed reads, random hold/scramble crud, and bit crush. Touch freezes a damaged window into a looping frame; otherwise the read bends against the write head.",
    ja: "変則再生バッファです。可変速読み、ホールド／スクランブル、ビットクラッシュをかけます。タッチで破損窓をフリーズループし、通常時は書き込み位置付近をベンドします。",
    mermaid: `flowchart LR
  In[Live in] --> Buf[Stereo buffer]
  Touch[Touch freeze] --> Loop[Frozen window]
  Buf --> Read[Varispeed read]
  Loop --> Read
  Read --> Crud[Hold or scramble] --> Crush[Bit crush] --> Mix[Dry or wet] --> Out[Out]
  In --> Mix`,
  },
  dredbass: {
    en: "Suction bass: BLEP saw/pulse through a one-pole LPF whose mouth envelope can invert (open-shut-open). Touch gates amp and filter; tanh then dry/wet.",
    ja: "サクション・ベースです。BLEPソー／パルスをワンポールLPFに通し、口のエンベロープを反転（開→閉→開）できます。タッチでアンプとフィルタを開き、tanh後ドライ／ウェットします。",
    mermaid: `flowchart LR
  Touch[Touch] --> Env[Amp and mouth env]
  Osc[BLEP saw pulse] --> LPF[One-pole LPF]
  Env --> LPF
  LPF --> Drive[tanh] --> Mix[Dry or wet] --> Out[Out]
  In[Audio in] --> Mix`,
  },
  subharm: {
    en: "Polyrhythmic chord voice: two 4-step sequencers clocked by layered integer rhythm divisions drive two VCOs plus four subharmonics into a soft LPF. Pad hold runs the clock; X densifies rhythms, Y raises subs.",
    ja: "ポリリズム・コード声部です。整数分割のリズム層が2つの4ステップ・シーケンサを駆動し、2VCO＋4サブをソフトLPFへ。パッドホールドで走行、Xはリズム密度、Yはサブ量です。",
    mermaid: `flowchart LR
  Pad[Pad hold] --> Clock[16th grid]
  POLY[X poly] --> Divs[Rhythm divisions]
  Clock --> Divs --> Seq[Dual 4-step seq]
  Seq --> VCO[2 VCO plus 4 subs]
  SUBS[Y subs] --> VCO
  VCO --> LPF[Soft LPF] --> Mix[Dry or wet] --> Out[Out]
  In[Audio in] --> Mix`,
  },
  labipath: {
    en: "Generative West-Coast voice: dual corruptible sequencers (polymeter lengths) drive a sine carrier and triangle mod through FM and a soft wavefolder. X = Corrupt, Y = Fold.",
    ja: "生成的ウェストコースト声部です。Corrupt可能な二重シーケンサがサイン・キャリアと三角モジュレータをFMとウェーブフォルダへ。XはCorrupt、YはFoldです。",
    mermaid: `flowchart LR
  Pad[Pad hold] --> Seq[Dual generative seq]
  CORR[X corrupt] --> Seq
  Seq --> Car[Sine carrier]
  Seq --> Mod[Triangle mod]
  FM[FM] --> Car
  Mod --> Car
  Car --> Fold[Wavefolder]
  FOLD[Y fold] --> Fold
  Fold --> Mix[Dry or wet] --> Out[Out]
  In[Audio in] --> Mix`,
  },
  dfamperc: {
    en: "Mutant percussion: dual osc with sync/FM grit plus noise through a resonant LPF and age-based snappy envelope, stepped by an 8-step pitch/velocity pattern. X = grit, Y = decay.",
    ja: "ミュータント・パーカッションです。シンク／FMグリット付きデュアルOSCとノイズをレゾナントLPFと年齢ベースのスナッピーEGで整形し、8ステップで走行。Xはグリット、Yはディケイです。",
    mermaid: `flowchart LR
  Pad[Pad hold] --> Seq[8-step pitch vel]
  Seq --> Osc[Dual osc sync FM]
  GRIT[X grit] --> Noise[Noise]
  Osc --> Sum[Mix]
  Noise --> Sum
  Sum --> LPF[Resonant LPF]
  DEC[Y decay] --> Env[Age amp env]
  Env --> LPF --> Mix[Dry or wet] --> Out[Out]
  In[Audio in] --> Mix`,
  },
  stepgatedelay: {
    en: "Euclidean 1/16 gate into a tempo delay. Pad-down captures the nearest 16th as relative step 0. X = hit density among 16, Y = delay wet. SHAPE selects the per-step envelope (SQR / decaying SAW / RAMP / TRI / EXP). Default time is a dotted eighth. Feedback high/low damp; low damp is on by default.",
    ja: "ユークリッド1/16ゲートをテンポディレイへ送ります。パッド押下で最寄りの16分が相対ステップ0。Xは16ステップ中の密度、YはディレイWet。SHAPEでゲート波形（SQR / 減衰SAW / RAMP / TRI / EXP）。既定タイムは付点8分。FBにハイ／ローダンプ（既定でローダンプON）。",
    mermaid: `flowchart LR
  Clock[Nearest 16th] --> Euclid[Euclid density]
  Pad[Pad hold] --> Euclid
  Shape[SHAPE envelope] --> Gate
  In[Audio in] --> Gate[Step gate] --> Delay[Tempo delay]
  Euclid --> Gate
  Delay --> Mix[Dry or wet] --> Out[Out]
  In --> Mix
  Y[Y wet] --> Mix
  Damp[Hi and Lo damp] --> Delay`,
  },
  eucgate: {
    en: "Tempo Euclidean/probability gate on the input. Closed steps mute; duty sets the open fraction. Touch fills (opens every step). Mix blends gated vs ungated level.",
    ja: "テンポ上のユークリッド／確率ゲートです。閉じたステップはミュートし、デューティで開時間を決めます。タッチで全ステップ開放。Mixでゲート量を調整します。",
    mermaid: `flowchart LR
  Clock[Tempo steps] --> Euclid[Euclid and duty]
  Touch[Touch fill] --> Euclid
  In[Audio in] --> Gate[Level gate] --> Out[Out]
  Euclid --> Gate`,
  },
  eucroll: {
    en: "Euclidean step roll: always records. Touch rolls only Euclidean hit steps (non-hits stay dry). X = hit density, Y = subdivision inside a hit, Depth = random pan width per micro-roll.",
    ja: "ユークリッド・ステップロールです。常時録音し、タッチ中はヒット・ステップだけをループ（非ヒットはドライ）。Xは密度、Yはヒット内の細分化、Depthはマイクロロールごとのランダムパン幅です。",
    mermaid: `flowchart LR
  In[Live in] --> Buf[Always-on buffer]
  Clock[Tempo steps] --> Euclid[Euclid hits]
  Touch[Touch] --> Gate[Hit gate]
  Euclid --> Gate
  Gate -->|hit| Cap[Capture step]
  Gate -->|miss| Dry[Dry pass]
  Buf --> Cap --> Loop[Subdivided loop]
  Y[Y roll] --> Loop
  Depth[Depth pan] --> Pan[Random L/R]
  Loop --> Pan --> Mix[Dry or wet] --> Out[Out]
  Dry --> Out
  In --> Mix`,
  },
  fbackosc: {
    en: "JP-8080-style feedback oscillator: a band-limited saw into a key-tracked resonant comb (delay + feedback + damping), with level compensation, soft-clip, and DC block so high feedback stays usable.",
    ja: "JP-8080系フィードバックOSCです。帯域制限ソーをキー追従の共振コーム（遅延＋FB＋減衰）へ通し、レベル補正・ソフトクリップ・DCカットで高フィードバックでも扱いやすくしています。",
    mermaid: `flowchart LR
  Pitch[Note pitch] --> Saw[BL saw]
  Saw --> Comb[Comb delay and FB]
  Harm[Harmonics] --> Comb
  Feed[Feedback] --> Comb
  Comb --> Clip[Softclip and DC] --> Out[Out]`,
  },
  glitchpad: {
    en: "Pad-down glitch FX on a stereo capture buffer. X/mode picks retrigger, reverse, shuffle, tape stop, stretch, gate, crush, or delay; Y sets musical slice length. Pad up bypasses to dry.",
    ja: "パッド押下でステレオ捕獲バッファ上のグリッチをかけます。モードはリトリガ／逆再生／シャッフル／テープストップ／ストレッチ／ゲート／クラッシュ／ディレイ。離すとバイパスします。",
    mermaid: `flowchart LR
  In[Live in] --> Buf[Stereo ring buffer]
  Pad[Pad down] --> Mode[8 glitch modes]
  Buf --> Mode
  Mode --> Mix[Dry or wet fade] --> Out[Out]
  In --> Mix`,
  },
  grainpad: {
    en: "Live-capture granular pad. Touch freezes up to 3 s of AUDIO IN into long, slow grains (100–320 ms). X/FEEL: sparse stitches ↔ dense wash. Y: octave mix. ENV = grain attack/release; SPRD / HPF / REVS as edits.",
    ja: "AUDIO INを最大3秒フリーズし、長めのグレイン（100–320 ms）をゆっくり重ねます。Xは疎↔密、Yはoct混率。ENVで粒のアタック／リリース、SPRD／HPF／REVSあり。",
    mermaid: `flowchart LR
  In[Audio in] --> Ring[SDRAM max 3s]
  Touch[Touch freeze] --> Cloud[Grain cloud]
  Ring --> Cloud
  Feel[FEEL density] --> Cloud
  Env[ENV A/R] --> Cloud
  Cloud --> HPF[Wet HPF] --> Mix[Dry or wet] --> Out[Out]
  In --> Mix`,
  },
  regrain: {
    en: "Deep wet reverb continuously feeds the capture buffer; touch freezes it into a granular cloud. X/FEEL = density, Y/SIZE = reverb depth. ENV / TONE / SPRD / REVS.",
    ja: "深いウェット・リバーブを常時かけてその音を録音し、タッチでフリーズしてグレイン雲にします。Xは密度、Yは残響の深さ。ENV／TONE／SPRD／REVSあり。",
    mermaid: `flowchart LR
  In[Audio in] --> Tank[Deep reverb]
  Tank --> Ring[SDRAM capture]
  Touch[Touch freeze] --> Cloud[Grain cloud]
  Ring --> Cloud
  Size[SIZE decay] --> Tank
  Feel[FEEL density] --> Cloud
  Cloud --> Mix[Dry or wet] --> Out[Out]
  In --> Mix`,
  },
  gridsdrum: {
    en: "Hold-to-run 16-step generative BD/SD/HH with Grids-like density maps. BD/SD are decaying tones (SD adds noise); HH is noise. Top-right touch fills; Audio In stays full; MIX is drum level.",
    ja: "保持中に16ステップの生成BD/SD/HHを鳴らします。密度マップでトリガし、BD/SDは減衰トーン（SDはノイズ混在）、HHはノイズです。右上タッチでフィル。",
    mermaid: `flowchart TD
  Hold[Pad hold] --> Clock[16th and swing]
  Clock --> Map[Density map BD SD HH]
  Map --> BD[Sine kick]
  Map --> SD[Tone and noise snare]
  Map --> HH[Noise hat]
  BD --> Mix[Softclip then drum level over Audio In]
  SD --> Mix
  HH --> Mix`,
  },
  ukgarage: {
    en: "Hold-to-gate tempo-synced 2-step UK Garage kit. Hits lock to host 4ppqn (or a free-running 16th grid), not the tap moment. Spine kicks on 1 and the and-of-3, snares on 2 and 4. X grows low-velocity ghost snares/rims; Y thickens hats and fill energy. Top-right flick = one-bar Fill. Age-based envelopes keep bodies musical.",
    ja: "ホールドはゲートのみ。タップ瞬間ではなくホストの4ppqn（なければ内部16分グリッド）に合わせて2-step UK Garageを鳴らします。キックは1と3の裏、スネアは2と4。Xで弱いゴースト、Yでハット／Fill。右上フリックで1小節Fill。",
    mermaid: `flowchart TD
  Host[Host 4ppqn or free-run 16th] --> Gate{Pad held?}
  Gate -->|yes| Spine[2-step kick snare spine]
  Gate -->|yes| Ghost[X ghost seats]
  Gate -->|yes| Fill[Y hats and fill]
  Gate -->|no| Silence[No new hits]
  Spine --> Voices[Kick snare ghost hats]
  Ghost --> Voices
  Fill --> Voices
  Voices --> Mix[Softclip then drum level over Audio In] --> Out[Out]`,
  },
  boombap: {
    en: "Hold-to-gate boom-bap kit. Hits lock to host 4ppqn. Boom kick on 1 with late pushes; hard snares on 2 and 4. X grows dusty ghost snares/rims; Y thickens swung hats. Top-right flick = one-bar Fill. Default feel ~90 BPM.",
    ja: "ホールドはゲートのみ。ホストの4ppqnに合わせてブームバップを鳴らします。キックは1と遅めの押し、スネアは2と4。Xでダスティなゴースト、Yでスウィング・ハット。右上フリックで1小節Fill。既定〜90 BPM。",
    mermaid: `flowchart TD
  Host[Host 4ppqn] --> Gate{Pad held?}
  Gate -->|yes| Spine[Boom kick hard snare]
  Gate -->|yes| Ghost[X dusty ghosts]
  Gate -->|yes| Hats[Y swung hats]
  Spine --> Voices[Kick snare ghost hats]
  Ghost --> Voices
  Hats --> Voices
  Voices --> Mix[Softclip then drum level over Audio In] --> Out[Out]`,
  },
  dembow: {
    en: "Hold-to-gate reggaeton dembow kit. Hits lock to host 4ppqn. Kick/cha spine carries the dembow feel; X grows rim answers; Y adds percussion toward Fill. Top-right flick = one-bar Fill. Default ~96 BPM.",
    ja: "ホールドはゲートのみ。ホストの4ppqnに合わせてデンボウを鳴らします。キック／チャの骨格に、Xでリム応答、Yでパーカッション。右上フリックで1小節Fill。既定〜96 BPM。",
    mermaid: `flowchart TD
  Host[Host 4ppqn] --> Gate{Pad held?}
  Gate -->|yes| Spine[Dembow kick cha]
  Gate -->|yes| Rim[X rim answers]
  Gate -->|yes| Perc[Y hats perc]
  Spine --> Voices[Kick snare rim hats]
  Rim --> Voices
  Perc --> Voices
  Voices --> Mix[Softclip then drum level over Audio In] --> Out[Out]`,
  },
  footwork: {
    en: "Hold-to-gate Chicago footwork / juke kit. Hits lock to host 4ppqn. Sparse kick spine plus X stutter kicks; Y grows snare rolls and frantic hats. Short bodies for ~160 BPM. Top-right flick = one-bar Fill.",
    ja: "ホールドはゲートのみ。ホストの4ppqnに合わせてフットワークを鳴らします。疎なキックスパインにXでスタッター、Yでスネアロールと忙しいハット。短いボディで〜160 BPM向け。右上フリックで1小節Fill。",
    mermaid: `flowchart TD
  Host[Host 4ppqn] --> Gate{Pad held?}
  Gate -->|yes| Spine[Sparse kick snare]
  Gate -->|yes| Stut[X kick stutters]
  Gate -->|yes| Roll[Y snare rolls]
  Spine --> Voices[Short kick snare hats]
  Stut --> Voices
  Roll --> Voices
  Voices --> Mix[Softclip then drum level over Audio In] --> Out[Out]`,
  },
  breakbeat: {
    en: "Hold-to-gate Amen-inspired synthetic breakbeat (not a sample). Hits lock to host 4ppqn. Syncopated kick/snare map; X grows ghosts and secondary snares; Y pushes break energy toward Fill. Default ~174 BPM.",
    ja: "ホールドはゲートのみ。Amen風のシンセ・ブレイクビー（サンプルではない）をホストの4ppqnに合わせて鳴らします。シンコペしたキック／スネアに、Xでゴーストと副スネア、Yでブレイク・エネルギー。既定〜174 BPM。",
    mermaid: `flowchart TD
  Host[Host 4ppqn] --> Gate{Pad held?}
  Gate -->|yes| Spine[Amen-ish kick snare]
  Gate -->|yes| Sync[X ghosts secondary]
  Gate -->|yes| Energy[Y break energy]
  Spine --> Voices[Kick snare ghost hats]
  Sync --> Voices
  Energy --> Voices
  Voices --> Mix[Softclip then drum level over Audio In] --> Out[Out]`,
  },
  dnbass: {
    en: "Hold-to-gate drum & bass kit. Hits lock to host 4ppqn. Half-time snare on beat 3 with syncopated kicks; X rolls hats; Y pushes break energy toward Fill. Default ~174 BPM.",
    ja: "ホールドはゲートのみ。ホストの4ppqnに合わせてDnBを鳴らします。ハーフタイムのスネア（3拍目）とシンコペ・キック。Xでローリング・ハット、Yでブレイク。既定〜174 BPM。",
    mermaid: `flowchart TD
  Host[Host 4ppqn] --> Gate{Pad held?}
  Gate -->|yes| Spine[Half-time snare kicks]
  Gate -->|yes| Hats[X rolling hats]
  Gate -->|yes| Break[Y break energy]
  Spine --> Voices[Kick snare hats]
  Hats --> Voices
  Break --> Voices
  Voices --> Mix[Softclip then drum level over Audio In] --> Out[Out]`,
  },
  trance: {
    en: "Hold-to-gate trance kit. Hits lock to host 4ppqn. Hats are TR-909 ROM PCM (same dump as Trap808/HHat). Kick and clap are analog 909 circuit models — a real 909 has no BD/clap ROM to dump. X thickens 909 hats; Y builds clap rolls toward Fill. Default ~138 BPM.",
    ja: "ホールドはゲートのみ。ホストの4ppqnに合わせてトランスを鳴らします。ハットはTR-909 ROM、キック／クラップは909アナログ回路モデル（実機にBD/クラップROMはありません）。Xで909ハット、Yでビルド。既定〜138 BPM。",
    mermaid: `flowchart TD
  Host[Host 4ppqn] --> Gate{Pad held?}
  Gate -->|yes| Spine[909 BD clap models]
  Gate -->|yes| Hats[X 909 ROM hats]
  Gate -->|yes| Build[Y clap build]
  ROM[TR-909 HH ROM] --> Hats
  Spine --> Voices[Kick clap hats]
  Hats --> Voices
  Build --> Voices
  Voices --> Mix[Softclip then drum level over Audio In] --> Out[Out]`,
  },
  trance2: {
    en: "Hold-to-gate trance drums plus rolling bass. Hits lock to host 4ppqn. Same 909 kit as Trance. X grows bass complexity: offbeat 1/8 → rolling 16ths on the root → walk-ups near the bar end. Y thickens hats and clap rolls toward Fill. Saw mid + sine sub, short pluck, kick sidechain duck. Default ~138 BPM.",
    ja: "ホールドはゲートのみ。ホストの4ppqnに合わせてトランス・ドラム＋ローリング・ベースを鳴らします。ドラムはTranceと同じ909系。Xはベース複雑さ（裏拍8分→16分ロール→ウォーク）、Yはハット／クラップ・ビルド。Sawミッド＋サイン・サブ、短いプラック、キック・サイドチェイン。既定〜138 BPM。",
    mermaid: `flowchart TD
  Host[Host 4ppqn] --> Gate{Pad held?}
  Gate -->|yes| Spine[909 BD clap models]
  Gate -->|yes| Hats[Y 909 ROM hats]
  Gate -->|yes| Build[Y clap build]
  Gate -->|yes| Bass[X rolling bass]
  ROM[TR-909 HH ROM] --> Hats
  Bass --> Layers[Saw mid + sine sub]
  Layers --> Duck[Kick sidechain duck]
  Spine --> Voices[Kick clap hats bass]
  Hats --> Voices
  Build --> Voices
  Duck --> Voices
  Voices --> Mix[Softclip then kit level over Audio In] --> Out[Out]`,
  },
  drums: {
    en: "Hold-to-gate multi-genre drum kit. Hits lock to host 4ppqn. GENRE selects the pattern; KIT picks the synthetic voice feel (AUTO follows GENRE). X densifies; Y pushes fill energy; top-right fires a one-bar snare-roll Fill. Trap808 stays separate.",
    ja: "ホールドはゲートのみ。ホストの4ppqnに合わせてジャンル別ドラムを鳴らします。GENREはパターン、KITは音色（AUTOはGENRE追従）。Xは密度、YはFillエネルギー、右上で1小節のスネア連打Fill。Trap808は別ユニットです。",
    mermaid: `flowchart TD
  Host[Host 4ppqn] --> Gate{Pad held?}
  Genre[GENRE] --> Pattern[Genre pattern]
  Kit[KIT] --> Feel[Voice feel]
  Gate -->|yes| Pattern
  Gate -->|yes| Dens[X densify]
  Gate -->|yes| Fill[Y fill energy]
  Gate -->|yes| Corner[Top-right snare Fill]
  Pattern --> Voices[Kick snare hats]
  Feel --> Voices
  Dens --> Voices
  Fill --> Voices
  Corner --> Voices
  Voices --> Mix[Softclip then drum level over Audio In] --> Out[Out]`,
  },
  hclap: {
    en: "Tempo 16-step Euclidean clap. Analog noise and LFSR morph 808→909 dual-VCA style; burst and room bandpasses shape each voice. Density and flams follow pad-style params. Audio In stays full; MIX is drum level.",
    ja: "テンポ同期のユークリッド・クラップです。アナログノイズとLFSRを808→909的にモーフィングし、バースト／ルーム帯域で複数ボイスを重ねます。Audio Inは常時通し、MIXはドラム音量です。",
    mermaid: `flowchart LR
  Clock[16th Euclid] --> Trig[Voice triggers]
  Noise[Analog and LFSR noise] --> BP[Crack and room BP]
  Trig --> BP
  BP --> Sum[Morph 808 or 909] --> Mix[Drum level over Audio In] --> Out[Out]
  In[Audio in] --> Mix`,
  },
  hsnare: {
    en: "Tempo 16-step Euclidean snare, sibling of HClap. Y morphs 808 bridged-T sines plus one HPF snappy into 909 triangle VCOs with a 20 ms pitch bend and split LPF/HPF noise. Audio In stays full; MIX is drum level.",
    ja: "HClapの兄弟ユニットで、テンポ同期のユークリッド・スネアです。Yで808のブリッジドT正弦＋HPFスナッピーから、909の三角VCO・20msピッチベンド・分割スナッピーへモーフィングし、Audio Inは常時通し、MIXはドラム音量です。",
    mermaid: `flowchart LR
  Clock[16th Euclid] --> Trig[Voice triggers]
  Trig --> Shell[173/336 Hz shells or tri VCOs]
  Noise[Analog and LFSR] --> Snap[HPF or split snappy]
  Trig --> Snap
  Shell --> Sum[Morph 808 or 909] --> Mix[Drum level over Audio In] --> Out[Out]
  Snap --> Sum
  In[Audio in] --> Mix`,
  },
  hypersaw: {
    en: "Virus-style HyperSaw: up to 9 detuned band-limited saws plus square subs. Density fades voices in, spread/width pans them, then the stack is normalized (mono-sum on NTS-1).",
    ja: "Virus系ハイパーソーです。最大9本のデチューン帯域制限ソー（＋サブ矩形）を密度でフェードインし、スプレッド／幅でパンして正規化します（NTS-1ではモノ合算）。",
    mermaid: `flowchart LR
  Pitch[Note] --> Saws[BL saws x9]
  Pitch --> Subs[BL square subs]
  Dens[Density] --> Saws
  Spread[Spread and Width] --> Pan[Stereo pan]
  Saws --> Pan
  Subs --> Pan
  Pan --> Mix[Sub mix and norm] --> Out[Out]`,
  },
  kaocid: {
    en: "TB-303-style mono acid: each tap advances a 16-step phrase seed (pad XY is cutoff/resonance only, not the seed). VCO (saw/square) → gsynth-style VCF with accent sweep → VCA, then dry/wet.",
    ja: "TB-303風モノアシッドです。タップするたびに16ステップフレーズの種が進み（パッド位置はカットオフ／レゾナンスのみ）、ソー／矩形→VCF（アクセント掃引）→VCAの順でドライ／ウェットします。",
    mermaid: `flowchart LR
  Touch[Touch phrase] --> Seq[16-step and slide]
  Seq --> VCO[Saw or Square]
  VCO --> VCF[gsynth VCF and accent]
  VCF --> VCA[VCA] --> Mix[Dry or wet] --> Out[Out]
  In[Audio in] --> Mix`,
  },
  loopkey: {
    en: "Keyboard micro-looper: records input into a short buffer (note sets length; sync/gate/free modes), then plays with pitch rate, feedback, crossfade wraps, and 4PPQN evolution.",
    ja: "キーボード制御のマイクロルーパーです。入力を短バッファに録音（音程で長さ、SYNC/GATE/FREE）し、ピッチ速度・FB・クロスフェード周回と4PPQN進化で再生します。",
    mermaid: `flowchart LR
  In[Audio in] --> Rec[Record buffer]
  Note[MIDI note length] --> Rec
  Rec --> Play[Varispeed loop and FB]
  Evol[4PPQN evolution] --> Play
  Play --> Mix[Wet mix] --> Out[Out]`,
  },
  mohowl: {
    en: "Pad-gated JP-8080 comb howl (same core as FbOsc) at max feedback. Pitch LFO wobbles the note; attack/release envelope and level shape the scream, then dry/wet with input.",
    ja: "パッド・ゲートのJP-8080コームハウリング（FbOsc同系、FB最大）です。ピッチLFOで揺れ、アタック／リリースとレベル後にドライ／ウェットします。",
    mermaid: `flowchart LR
  Touch[Pad gate] --> Env[A/R env]
  LFO[Pitch LFO] --> Comb[FBackOsc comb]
  Env --> Comb
  Comb --> Mix[Dry or wet] --> Out[Out]
  In[Audio in] --> Mix`,
  },
  perciter: {
    en: "One-shot percussion on each touch: sine/FM body with wavefold, plus HP noise. Mode morphs skin/liquid/metal character; pitch decays. Audio In stays full; MIX is drum level.",
    ja: "タッチ毎のワンショット打楽器です。正弦／FMボディ＋ウェーブフォールドとHPノイズを重ね、スキン／リキッド／メタルへモーフします。ピッチ減衰。Audio Inは常時通し、MIXはドラム音量です。",
    mermaid: `flowchart LR
  Touch[Trigger] --> Body[Sine or FM body]
  Body --> Fold[Wavefold]
  Touch --> Noise[HP noise]
  Fold --> Sum[Softclip] --> Mix[Drum level over Audio In]
  Noise --> Sum
  In[Audio in] --> Mix --> Out[Out]`,
  },
  revroll: {
    en: "DJM Rev Roll: pad gates only; captures a tempo slice on the host 16th grid (internal fallback) and plays it backwards with a speed curve. Release returns to live.",
    ja: "DJM Rev Rollです。パッドはゲートのみで、ホスト16分（なければ内部16分）でテンポ切片を掴み、速度カーブ付きで逆再生します。離すとライブに戻ります。",
    mermaid: `flowchart LR
  In[Live in] --> Buf[Always-on buffer]
  Pad[Pad gate] --> Hold[Hold]
  Clock[Host 16th or internal] --> Cap[Capture slice]
  Hold --> Cap
  Buf --> Cap --> Loop[Reverse loop] --> Mix[Dry or wet] --> Out[Out]
  In --> Mix`,
  },
  reesephr: {
    en: "Pad-gated detuned Reese: 4 BLEP saws plus a sub pulse. A sparse tempo scale-walk shifts the root; soft-clipped and dry/wet mixed with a slight stereo imbalance.",
    ja: "パッド・ゲートのデチューン・リースです。4本BLEPソー＋サブ矩形。テンポ疎なスケール歩行で根音が動き、ソフトクリップ後ドライ／ウェットします。",
    mermaid: `flowchart LR
  Touch[Pad gate] --> Amp[Amp smooth]
  Walk[Scale walk] --> Saws[4 detuned BLEP saws]
  Walk --> Sub[Sub pulse]
  Saws --> Sum[Softclip]
  Sub --> Sum
  Amp --> Sum
  Sum --> Mix[Dry or wet] --> Out[Out]
  In[Audio in] --> Mix`,
  },
  ride909: {
    en: "Tempo TR-909 ride wash: off-beat triggers play ROM via variable clock, ZOH DAC path, dual LPF, and DC block. Quarter notes pump the tail; pad gates the phrase.",
    ja: "テンポ同期のTR-909ライドです。裏拍でROMを可変クロック＋ZOH→二段LPFで再生し、四分でテールをパンプします。パッドでフレーズ・ゲートします。",
    mermaid: `flowchart LR
  Clock[16th ride or kick] --> Voices[ROM clock voices]
  ROM[6-bit Ride ROM] --> Voices
  Voices --> LPF[Dual recon LPF] --> Pump[Kick pump] --> Mix[Drum level over Audio In] --> Out[Out]
  In[Audio in] --> Mix`,
  },
  stoneres: {
    en: "Rubbing the XY pad scans a fractal stone surface into a modal body; a chord bandpass bank remaps that friction to ROOT/CHORD. LOAD/ROUGH on the pad; MAT/GRAIN/DAMP shape the stone; MIX blends dry stone vs resonator. Instrument-only (no dry oscillator pass-through).",
    ja: "XYパッドを擦るとフラクタル表面を走査して石のモーダル体を鳴らし、ROOT/CHORDのバンドパス群で共鳴させます。パッドはLOAD/ROUGH、石はMAT/GRAIN/DAMP、MIXは石ドライとレゾのブレンド。楽器出力のみ（ドライOSC通過なし）。",
    mermaid: `flowchart LR
  Rub[Pad rub speed] --> Excite[Friction excite]
  Excite --> Stone[Stone modal bank]
  Stone --> Dry[Stone dry]
  Stone --> Chord[Chord bandpass bank]
  Dry --> Mix[Equal-power MIX]
  Chord --> Mix --> Out[Stereo wet]`,
  },
  ringexcit: {
    en: "Input (plus a touch noise burst) excites a Karplus–Strong delay and three complex modal resonators. Structure blends string vs modal; tone/pos set damping and pickup; then dry/wet.",
    ja: "入力とタッチ・ノイズでKarplus–Strong遅延と3つのモーダル共振を励起します。構造で弦／モーダル比、トーン／位置で減衰とピックアップを決め、ドライ／ウェットします。",
    mermaid: `flowchart LR
  In[Audio in] --> Excite[Excite and noise burst]
  Touch[Touch pluck] --> Excite
  Excite --> KS[KS delay string]
  Excite --> Modal[3 modal resonators]
  KS --> Sum[Structure mix] --> Mix[Dry or wet]
  Modal --> Sum
  In --> Mix --> Out[Out]`,
  },
  shaker: {
    en: "PhISEM shaker: shake energy drives noise into a preset bank of resonators (maraca, cabasa, and more). Note/touch adds energy; stereo out is instrument-only with no dry pass-through.",
    ja: "PhISEMシェイカーです。シェイク・エネルギーがノイズをプリセット共振器群へ駆動します。ノート／タッチでエネルギーを追加し、楽器出力のみ（ドライ通過なし）です。",
    mermaid: `flowchart LR
  Shake[Note or Touch energy] --> Noise[Collision noise]
  Noise --> Res[Resonator bank] --> Out[Stereo wet]`,
  },
  stepdice: {
    en: "Tempo-synced step FX dice. A bar is split into 16/8/4/2/1 steps; each step is a seeded permutation of gate, filter, crush, ring, pan, drive, stutter, reverse, or echo. X scales intensity, Y re-seeds the pattern, touch re-rolls in RUN.",
    ja: "テンポ同期のステップFXダイスです。1小節を16/8/4/2/1ステップに分け、ゲート／フィルタ／クラッシュ／リング／パン／ドライブ／スタッタ／リバース／エコーをステップごとに割り当てます。Xは強さ、Yはパターンの種、RUN中のタッチで再ロールです。",
    mermaid: `flowchart LR
  Tempo[BPM clock] --> Grid[16 8 4 2 1 steps]
  Dice[Y seed and throw] --> Pattern[FX permutation]
  Grid --> Pattern
  In[Audio in] --> Buf[Stereo ring]
  Buf --> FX[Per-step FX]
  Pattern --> FX
  FX --> Mix[Dry or wet] --> Out[Out]
  In --> Mix`,
  },
  stepfenv: {
    en: "Tempo-synced step filter envelope on AUDIO IN. Each grid step snaps the cutoff open (attack 0) then decays to the X floor with sustain/release at 0. Y is env depth; DEC sets decay; RES is resonance. Touch resets the grid.",
    ja: "AUDIO INへのテンポ同期ステップ・フィルタEGです。各ステップでカットオフが即開いて（アタック0）Xの床まで減衰します（サスティン／リリース0）。Yは深さ、DECは減衰、RESはレゾナンス。タッチでグリッドをリセットします。",
    mermaid: `flowchart LR
  Tempo[BPM clock] --> Grid[8 12 16 steps]
  Grid --> Env[Cutoff env A0 D S0 R0]
  In[Audio in] --> LPF[Resonant LPF]
  Env --> LPF
  Cut[X cutoff] --> LPF
  Depth[Y env depth] --> Env
  LPF --> Mix[Dry or wet] --> Out[Out]
  In --> Mix`,
  },
  steprndflt: {
    en: "Tempo-synced sample-and-hold LFO into a multimode resonant filter on AUDIO IN. Each step redraws a random bipolar offset around CUT; X (DEPTH) scales that swing in octaves, Y is resonance. TYPE selects LP12 / LP24 / BPF / HP12 / HP24. Hold the pad to engage.",
    ja: "AUDIO INへのテンポ同期S&H LFO→マルチモード共振フィルタです。各ステップでCUT周りのバイポーラ乱数を引き直し、X（DEPTH）がその振れ幅（オクターブ）、Yがレゾナンス。TYPEはLP12/LP24/BPF/HP12/HP24。パッド押下中のみ効きます。",
    mermaid: `flowchart LR
  Tempo[BPM clock] --> Grid[Step period]
  Grid --> SH[Sample and hold]
  SH --> CutMod[Cutoff offset]
  Depth[X depth] --> CutMod
  Cut[CUT center] --> CutMod
  In[Audio in] --> Flt[LP12 LP24 BPF HP12 HP24]
  CutMod --> Flt
  Res[Y resonance] --> Flt
  Type[TYPE] --> Flt
  Flt --> Mix[Dry or wet] --> Out[Out]
  In --> Mix`,
  },
  trap808: {
    en: "Beat-locked trap phrase pad. Hold gates the clock; hits ignore tap phase and fire on 4ppqn. Closed/open hats play packed 6-bit PCM from the TR-909 Hi-Hat ROM (CH top quarter, truncated OH). X = hat rolls, Y = groove, ROOT = 808 key. Age-based kick/snare/808; top-right arms a fill on the next downbeat.",
    ja: "ビートロックのトラップ・フレーズパッドです。ホールドはゲートのみで、発音は4ppqnに同期（タップ位相は無視）。ハットはTR-909ハイハットROMの6bit PCM（CH=上位1/4、OHは短縮）。Xはロール、Yはグルーヴ、ROOTは808キー。キック／スネア／808は年齢エンベロープ。右上で次の拍頭からフィル。",
    mermaid: `flowchart LR
  Pad[Hold gate] --> Arm[Arm running]
  Host[4ppqn beat] --> Seq[Kick snare hat sched]
  Arm --> Seq
  Hats[X hats] --> Seq
  Groove[Y groove] --> Seq
  Seq --> Roll[32nd triplet rolls]
  Roll --> HH[909 ROM hats choke]
  ROM[CH OH PCM] --> HH
  Seq --> Kick[Pitch-drop kick]
  Seq --> SD[Tone plus noise snare]
  Seq --> Bass[Sine 808 glide]
  Root[ROOT] --> Bass
  Kick --> Sum[Sum and softclip]
  SD --> Sum
  HH --> Sum
  Bass --> Sum
  Sum --> Mix[Drum level over Audio In] --> Out[Out]
  In[Audio in] --> Mix`,
  },
  tapeosc: {
    en: "Tape-style oscillator: a band-limited source is written into a circular buffer while a varispeed read head ramps start/stop. Grit blends ZOH vs linear; wear LPF and wow/flutter modulate rate.",
    ja: "テープ風OSCです。帯域制限波形を円形バッファへ書き、読みヘッドが起動／停止で変速します。グリットでZOH／線形、摩耗LPFとワウ／フラッタで速度を変調します。",
    mermaid: `flowchart LR
  Src[BL waveform] --> Buf[Circular tape buffer]
  Transport[Start or Stop rate] --> Read[Varispeed read]
  Buf --> Read
  Wow[Wow Flutter] --> Read
  Read --> LPF[Motor LPF and wear] --> Out[Out]`,
  },
  technorumble: {
    en: "Insert rumble FX: mono into a 4-comb + 2-allpass Schroeder reverb, sub LPF, soft-clip drive, and input-transient sidechain duck on the wet path; dry/wet stereo.",
    ja: "インサート・ランブルです。モノ→4コーム＋2オールパスのシュレーダー残響→サブLPF→ドライブし、入力トランジェントでウェットをダックしてステレオ・ドライ／ウェットします。",
    mermaid: `flowchart LR
  In[Stereo in] --> Mono[Mono]
  Mono --> Rev[4 comb and 2 allpass]
  Rev --> LPF[Sub LPF] --> Drive[Softclip]
  Mono --> SC[Transient duck] --> Drive
  Drive --> Mix[Dry or wet] --> Out[Out]
  In --> Mix`,
  },
  transitionlooper: {
    en: "Tempo bar looper: continuously captures raw/live audio; pad freezes and fades into the loop. Transition types (vol, HPF/LPF, bass, echo, break, roll) shape the live vs loop blend.",
    ja: "テンポ・バー・ルーパーです。常時録音しパッドでフリーズしてループへフェードします。VOL/HPF/LPF/BASS/ECHO/BRK/ROLLでライブとループの遷移を整形します。",
    mermaid: `flowchart LR
  In[Live or raw in] --> Cap[Bar capture buffer]
  Pad[Pad freeze] --> Loop[Frozen loop play]
  Cap --> Loop
  Loop --> Type[Transition type FX]
  In --> Type
  Type --> Mix[Wet fade] --> Out[Out]`,
  },
  warpsmorph: {
    en: "Cross-mod morph of diode ring, digital XOR, comparator, mini-vocoder (carrier×env), and folder. An internal sine carrier mixes in harder while the pad is held; drive then dry/wet.",
    ja: "ダイオード・リング／XOR／コンパレータ／ミニボコーダ／フォルダをモーフします。内部正弦キャリアはパッド保持で強めに混入し、ドライブ後ドライ／ウェットします。",
    mermaid: `flowchart LR
  In[Audio in] --> Algos[Ring XOR Cmp Voc Fold]
  Carr[Sine carrier] --> Algos
  Touch[Pad carrier mix] --> Carr
  Algos --> Drive[Softclip drive] --> Mix[Dry or wet] --> Out[Out]
  In --> Mix`,
  },
};

export function dspExplainFor(pluginId) {
  return dspExplainById[pluginId] ?? null;
}
