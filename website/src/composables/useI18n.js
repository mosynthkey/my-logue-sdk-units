import { inject, provide, ref, watch } from "vue";

const I18N_KEY = Symbol("i18n");
const STORAGE_KEY = "logue-sdk-preview-language";

export const japanesePluginDescriptions = {
  airfm: "Alesis airSynthに着想を得た、FM効果音シンセです。Xはキャリア周波数（CARR）、Yはモジュレータ周波数（MOD）です。",
  airhorn: "AirHornを鳴らします。 Pitch Modeでピッチ固定か、鍵盤/Pitchパラメーター合わせるかを決められます。",
  amentime: "合成した1小節のamen風ブレイクを、ホストBPMに合わせてスライス再生します。オリジナル録音は入っていません。パッドを押しているあいだ、小節頭からステップ同期で再生（タップ位置は開始16分を決めません）。Xはリバース確率、Yはグリッド（1/4〜1/32）、STRTはEditで開始オフセット。手元のWAVはWavSliceを使います。",
  wavslice: "任意の1小節WAVをホストBPMに合わせてスライス再生します。同梱はオリジナルのCC0ドラムループ。パッドを押しているあいだ、小節頭からステップ同期で再生（タップ位置は開始16分を決めません）。Xはリバース確率、Yはグリッド。assets/loop.wav を置くか make embed WAV=... で差し替えます。",
  fbackosc: "JP-8080に着想を得たFeedback oscillatorです。band-limited sawをkey-tracked resonant comb filterに通します。FEEDを上げても1/(1-fb)で音量が跳ねないよう補償しています。",
  mohowl: "作者モチーフのフィードバックハウリングです。パッドを押すとJP-8080風の金切り声が出ます。周波数はLFOで揺れ、Xはその深さ、Yはハーモニクス、フィードバックは最大固定です。",
  hypersaw: "Virus TIに着想を得た9-voice detuned saw stackです。Density、Spread、HyperSub、stereo widthを調整できます。",
  kaocid: "XY Padで演奏する303系のシンセです。Tapで16ステップのフレーズを生成（位置はCUT/RESのみ、フレーズ種は連打で進みます）。\n現在も調整中で、正式版\u2060は近日公開予定。",
  loopkey: "keyboardで操作するmicro-looper oscillatorです。external audio inputをloopし、MIDI noteでloop lengthを設定します。Tempo sync、gate mode、evolutionに対応します。",
  ride909: "テクノでよく聞く、裏打ちの909 Ride Cymbalを再生します。ピッチを変更することができます。\n現在も調整中で、正式版\u2060は近日公開予定。",
  shaker: "PhISEM shakerの移植です。XY Pad / 鍵盤でさまざまなパーカッションを演奏できます。\n現在も調整中で、正式版\u2060は近日公開予定。",
  technorumble: "Techno rumble kick processorです。長いreverb tail、sub LPF、drive、kick transientに反応するsidechain duckを1ユニットにまとめています。NTS-3はAUDIO IN、mkIIはsynth出力にkickを入れて使います。X/TIME = decay、Y/DEPTH = cutoff、Depth/MIX = dry/wetです。",
  tapeosc: "tape motorのstart/stopを再現するVarispeed oscillatorです。band-limited synth waveformがpitch envelopeではなく、tape deckのように減速してfreezeします。",
  transitionlooper: "DJの繋ぎ用ルーパーです。テンポ同期した16ステップのステレオループをAUDIO INから取り込みます。ファーム1.4以降はパッドを離しているあいだも事前録音できます。事前録音が無音のときは、最初のホールドで1小節録ってからループします。パッドを離しているときはバイパス、押しているあいだは保存したループへフェードします。Xはフェード時間、Yはフィルタの振り幅、TYPEは音量 / ハイパス / ローパス / ベーススワップ / エコーアウト / ブレーキ / ループロールです。",
  glitchpad: "Illformed Glitch²に着想を得たNTS-3用グリッチです。AUDIO INをテンポ同期バッファに取り込み、Passort同様タッチ開始位置で4シーンをロックします。左上=リトリガー、右上=リバース、左下=シャッフル、右下=ゲート。押しているあいだYがスライス長／ゲート速度、Depthはwet。触っていないときはバイパス。",
  beatrepeat: "テンポ同期のBeat Repeatです。AUDIO INを常時取り込み、パッドはゲートのみ。次の16分でスライスを掴んで繰り返します（タップ瞬間では発火しません）。Xはループ長（1/32〜2拍）、Yは再アーム確率です。",
  stoneres: "XYパッドを擦ると石の摩擦音が出て、コード・レゾネーターで音階に共鳴します。Xは荷重、Yは粗さ、MAT/GRAIN/DAMPで石、ROOT/CHORDで共鳴コード。\n現在も調整中で、正式版\u2060は近日公開予定。",
  ringexcit: "入力で弦/モーダル共振体を叩くエキサイターです。キックやノイズがピッチのあるトーンになります。Xは周波数、Yは明るさ/減衰、タッチで内部ノイズ励起です。",
  warpsmorph: "ダイオードリング / XOR / コンパレータ / ミニボコーダ / フォールダを1軸で横断するクロス変調です。Xはアルゴリズム、Yはキャリヤ周波数、タッチで内部キャリヤ混合です。",
  stepgatedelay: "ユークリッド1/16ゲート＋テンポディレイです。パッドを押すと、いちばん近い16分クロックが相対ステップ0になります（Ride909方式）。Xは16ステップ中の密度、YはディレイのWet。SHAPEでゲート波形（SQR / 減衰SAW / RAMP / TRI / EXP）を選べます。デフォルトのディレイタイムは付点8分。フィードバックにハイ／ローダンプがあり、初期値ではローダンプが効いています。",
  eucgate: "DJ TransformとEuclidean / 確率ゲートを統合したリズムミュートです。Xはヒット数、Yはデューティ/確率、タッチでFill（全開ゲート）です。",
  eucroll: "ユークリッド・ステップロールです。常時録音し、タッチ中はユークリッドのヒット・ステップだけをロール（非ヒットはドライのまま）。Xは密度、Yはステップ内の細分化、Depthはマイクロロールごとのランダムパン幅です。",
  databend: "CDスキップ / 破損データ / テープ引っかかりを1マクロにしたメディア故障シミュです。XはBend、YはCorrupt、タッチで破損フレームをフリーズします。",
  reesephr: "デチューンソウのReeseドローンです。Hooverではなく、低速で疎に動く低域フレーズ付き。Xはピッチ、Yはデチューン、タッチでゲートです。",
  perciter: "Skin / Liquid / Metalをモーフするパーカッションパッドです。Xはピッチ、YはSkin↔Metal、タッチでトリガです。",
  gridsdrum: "MI Grids的なXYでジャンル密度を決め、BD/SD/HHフレーズを生成します。タッチでシーケンサ走行、右上フリックで1小節Fillです。",
  ukgarage: "テンポ同期の2-step UK Garageキットです。ホールドはゲートのみで、タップ瞬間ではなくホストの拍グリッドに合わせて発音します。Xはゴーストノート、Yはハット密度とFill感。右上フリックで1小節Fill。",
  boombap: "テンポ同期のブームバップキットです。ホールドはゲートのみで発音はビートロック。キック＋2と4のハードスネア。Xはダスティなゴースト、Yはスウィング・ハット。右上フリックで1小節Fill。",
  dembow: "テンポ同期のレゲトン・デンボウキットです。ホールドはゲートのみで発音はビートロック。キック／チャの骨格に、Xでリム応答、Yでパーカッション。右上フリックで1小節Fill。",
  footwork: "テンポ同期のシカゴ・フットワーク／ジュークキットです。ホールドはゲートのみで発音はビートロック。疎なキックスパインにXでスタッター、Yでスネアロール。右上フリックで1小節Fill。既定〜160 BPM。",
  breakbeat: "Amen風のシンセ・ブレイクビーキットです（サンプルではありません）。ホールドはゲートのみで発音はビートロック。Xはシンコペ／ゴースト、Yはブレイク・エネルギー。右上フリックで1小節Fill。既定〜174 BPM。",
  dnbass: "テンポ同期のドラムンベースキットです。ホールドはゲートのみで発音はビートロック。ハーフタイムのスネア（3拍目）＋シンコペ・キック。Xはローリング・ハット、Yはブレイク・エネルギー。右上フリックで1小節Fill。既定〜174 BPM。",
  trance: "テンポ同期のトランス・キットです。ハットはTR-909 ROM、キック／クラップは909アナログ回路モデル（実機BD/クラップにROMはありません）。ホールドはゲートのみで発音はビートロック。四つ打ち＋2と4のクラップ＋裏拍オープンハット。Xはハット、Yはビルド。右上フリックで1小節Fill。既定〜138 BPM。",
  trance2: "テンポ同期のトランス・ドラム＋ローリング・ベースです。ホールドはゲートのみで発音はビートロック。909系キットに、裏拍8分→16分ロール→ウォークアップのベースを追加。Xはベースの複雑さ、Yはドラムの複雑さ。右上フリックで1小節Fill。既定〜138 BPM。",
  hclap: "808 / 909のアナログ・ハンドクラップをXYパッドで鳴らします。ホールドでフレーズ走行。Xは手数（2と4から16分まで）、Yは808→909。\n現在も調整中で、正式版\u2060は近日公開予定。",
  hsnare: "808 / 909のアナログ・スネアをXYパッドで鳴らします。ホールドでフレーズ走行。Xは手数（2と4から16分まで）、Yは808→909。\n現在も調整中で、正式版\u2060は近日公開予定。",
  revroll: "DJMのRev Rollです。パッドはゲートのみで、次の16分で掴んだ区間を逆再生で繰り返します（タップ瞬間では掴みません）。Xは区間長、Yは逆再生の速度カーブ。離すと順方向に戻ります。",
  dredbass: "UKG／ジャングルのサクション低域です。LPF開口エンベロープを逆向きにして逆再生に聞こえるベースになります。Xはピッチ、Yはサクションの深さです。",
  subharm: "Subharmoniconに着想を得たポリリズム・コード声部です。パッドホールドで2つの4ステップ・シーケンサを整数分割クロックで動かし、2VCO＋4サブハーモニックをソフトLPFへ。Xはポリリズム密度、Yはサブの量です。",
  labipath: "Labyrinthに着想を得た生成的ウェストコースト声部です。Corrupt可能な二重シーケンサがサイン／三角をウェーブフォルダとFMへ駆動します。ホールドで走行、XはCorrupt、YはFoldです。",
  dfamperc: "DFAMに着想を得たミュータント・パーカッションです。デュアルOSC（シンク／FM）＋ノイズをレゾナントLPFとスナッピーな年齢ベースEGで整形し、8ステップのピッチ／ベロシティで走行。Xはグリット、Yはディケイです。",
  stepdice: "テンポに同期して1小節を16/8/4/2/1ステップに分け、ステップごとにバラバラのエフェクトをかけます。Xは強さ、Yはパターンの種、タッチで再ロール（RUN）またはグリッドON（HOLD）です。",
  stepfenv: "テンポ同期のステップ・フィルタ・エンベロープです。各ステップでカットオフEGが発火します（アタック0、ディケイ可変、サスティン0、リリース0）。Xはカットオフ、Yはエンベロープの深さ、タッチでグリッドをリセットします。",
  steprndflt: "テンポ同期のサンプル＆ホールドLFOをマルチモードTPT SVFのカットオフへかけます。各ステップでランダム値を引き直し、Xは深さ、Yはレゾナンス（上限付き）。TYPEはPeak/LP12/LP24/BPF/HP12/HP24/Var。VarはLPF24→…→HPF24の連続タイプ位置をステップS&H。LEVELでウェット→最終ソフトクリップ。パッドを押している間だけ効きます。",
  stepflanger: "テンポ同期のサンプル＆ホールド・フランジャーです。Yは中央基準の二重割り当て（|Y|=LFOデプス、符号×ステップ乱数=フィードバックで上=+・下=−）。Xは基点ディレイ。STEPSはFB乱数の更新周期、LFOはスイープ周期（別々のテンポ同期）。パッドを押している間だけ効きます。",
  trap808: "トラップ風ドラム＋909 ROMハイハット＋スライドする808ベースです。ホールドでゲートし、発音は常にビートにロック（タップタイミング無視）。Xはハット密度／ロール、Yはグルーヴ、ROOTで808のキー。右上フリックで次の拍頭から1小節フィル。",
  dubthrow: "ダブ机のセンド投げ用ディレイです。ドライは常時通し、パッドでBPM同期ディレイへ投げます。Xは投げ量、Yは帰還内バンドパスの音色。離すと新規取り込みだけ止まり、テールはFBで残ります。",
};

const messages = {
  en: {
    plugin: "Plugin",
    language: "Language",
    pluginList: "Plugin list",
    selectPlugin: "Select plugin",
    category: "Category",
    selectCategory: "Filter by category",
    categoryAll: "All",
    categoryOscillator: "Oscillator",
    categorySynth: "Synth",
    categoryDrum: "Drum",
    categoryFx: "FX",
    noPluginsInCategory: "No plugins in this category.",
    sendTo: "Send to {target}",
    sendToDevice: "Send to device",
    connectUsbHint: "Connect {target} over USB. It will be recognized automatically.",
    deviceNotFound: "No {target} found",
    download: "Download",
    downloadFor: "Download {target}",
    loading: "Loading plugins…",
    preview: "Preview",
    previewKeyboard: "Keyboard",
    previewXyPad: "XY Pad",
    latch: "Latch",
    hold: "Hold",
    play: "Play",
    stop: "Stop",
    inputSource: "Source",
    selectInputSource: "Select input source",
    sourceHouse: "House Loop",
    sourceTechno: "Techno Loop",
    sourceGarage: "UK Garage",
    sourceAcid: "Acid Line",
    sourceKick: "Four-on-the-floor Kick",
    sourceBreakbeat: "Breakbeat",
    sourceReese: "Reese Bass",
    sourceStab: "Chord Stab",
    sourceChord1: "Chord 1",
    sourceChord2: "Chord 2",
    sourceChord3: "Chord 3",
    sourceSawtooth: "Sawtooth",
    sourceSquare: "Square",
    sourceSine: "Sine",
    sourceTriangle: "Triangle",
    sourceNoise: "Noise",
    on: "On",
    off: "Off",
    close: "Close",
    dspHowItWorks: "How the DSP works",
    dspBlockDiagram: "Block diagram",
    dspExplainMissing: "DSP notes for this unit are not available yet.",
    midiRequired: "Chrome or Edge required for MIDI. Download the unit otherwise.",
    output: "Output",
    input: "Input",
    channel: "Channel",
    noPorts: "No ports",
    cancel: "Cancel",
    sendToSlot: "Send",
    nts1Slot: "NTS-1 mk2 Slot",
    nts3Slot: "NTS-3 Slot",
    slot: "Slot",
    transferSuccess: "{name} sent to slot {slot}",
    chrome152Hint: "Transfers can fail on Chrome 152. Please update to the latest Chrome.",
    log: "Log",
    octaveDown: "Octave down",
    octaveUp: "Octave up",
    appHeaderKicker: "",
    appHeaderTitle: "My Logue SDK Units",
    editProgram: "Program Edit",
    programEditorKicker: "NTS-3 kaoss pad",
    programEditorTitle: "Program Editor",
    routing: "Routing",
    fxSlots: "FX slots",
    slotFx: "Slot FX",
    clearSlot: "Clear",
    fxInSelect: "FX In Select",
    fxReleaseMode: "FX Release Mode",
    fxReleaseTime: "FX Release Time",
    outGain: "Out Gain",
  },
  ja: {
    plugin: "プラグイン",
    language: "言語",
    pluginList: "プラグイン一覧",
    selectPlugin: "プラグインを選択",
    category: "カテゴリー",
    selectCategory: "カテゴリーで絞り込み",
    categoryAll: "すべて",
    categoryOscillator: "オシレーター",
    categorySynth: "シンセ",
    categoryDrum: "ドラム",
    categoryFx: "FX",
    noPluginsInCategory: "このカテゴリーのプラグインはありません。",
    sendTo: "{target}へ送信",
    sendToDevice: "デバイスに送信",
    connectUsbHint: "{target}をUSB経由で接続してください。自動的に認識されます。",
    deviceNotFound: "No {target} found",
    download: "ダウンロード",
    downloadFor: "{target}をダウンロード",
    loading: "プラグインを読み込み中…",
    preview: "プレビュー",
    previewKeyboard: "Keyboard",
    previewXyPad: "XY Pad",
    latch: "ラッチ",
    hold: "ホールド",
    play: "再生",
    stop: "停止",
    inputSource: "音源",
    selectInputSource: "入力音源を選択",
    sourceHouse: "ハウスループ",
    sourceTechno: "テクノループ",
    sourceGarage: "UKガレージ",
    sourceAcid: "アシッドライン",
    sourceKick: "4つ打ちキック",
    sourceBreakbeat: "ブレイクビーツ",
    sourceReese: "リースベース",
    sourceStab: "コードスタブ",
    sourceChord1: "コード1",
    sourceChord2: "コード2",
    sourceChord3: "コード3",
    sourceSawtooth: "ノコギリ波",
    sourceSquare: "矩形波",
    sourceSine: "サイン波",
    sourceTriangle: "三角波",
    sourceNoise: "ノイズ",
    on: "オン",
    off: "オフ",
    close: "閉じる",
    dspHowItWorks: "DSPの仕組み",
    dspBlockDiagram: "ブロック図",
    dspExplainMissing: "このユニットのDSP解説はまだありません。",
    midiRequired: "MIDI送信にはChromeまたはEdgeが必要です。それ以外のブラウザではユニットをダウンロードしてください。",
    output: "出力",
    input: "入力",
    channel: "チャンネル",
    noPorts: "ポートなし",
    cancel: "キャンセル",
    sendToSlot: "送信",
    nts1Slot: "NTS-1 mk2 Slot",
    nts3Slot: "NTS-3 Slot",
    slot: "スロット",
    transferSuccess: "{name} をスロット {slot} に送信しました",
    chrome152Hint: "Chrome 152 では転送に失敗することがあります。最新の Chrome にアップデートしてください。",
    log: "ログ",
    octaveDown: "オクターブを下げる",
    octaveUp: "オクターブを上げる",
    appHeaderKicker: "",
    appHeaderTitle: "My Logue SDK Units",
    editProgram: "Program Edit",
    programEditorKicker: "NTS-3 kaoss pad",
    programEditorTitle: "Program Editor",
    routing: "ルーティング",
    fxSlots: "FXスロット",
    slotFx: "SLOT FX",
    clearSlot: "Clear",
    fxInSelect: "FX IN SELECT",
    fxReleaseMode: "FX RELEASE MODE",
    fxReleaseTime: "FX RELEASE TIME",
    outGain: "OUT GAIN",
  },
};

function initialLocale() {
  const saved = window.localStorage.getItem(STORAGE_KEY);
  if (saved === "en" || saved === "ja") return saved;
  return navigator.language.toLowerCase().startsWith("ja") ? "ja" : "en";
}

export function provideI18n() {
  const locale = ref(initialLocale());

  function setLocale(nextLocale) {
    if (messages[nextLocale]) locale.value = nextLocale;
  }

  function t(key, params = {}) {
    const template = messages[locale.value][key] ?? messages.en[key] ?? key;
    return Object.entries(params).reduce(
      (text, [name, value]) => text.replaceAll(`{${name}}`, String(value)),
      template,
    );
  }

  function pluginDescription(plugin) {
    if (locale.value !== "ja") return plugin.description;
    return japanesePluginDescriptions[plugin.id] ?? plugin.description;
  }

  watch(locale, (nextLocale) => {
    window.localStorage.setItem(STORAGE_KEY, nextLocale);
    document.documentElement.lang = nextLocale;
  }, { immediate: true });

  const i18n = { locale, setLocale, t, pluginDescription };
  provide(I18N_KEY, i18n);
  return i18n;
}

export function useI18n() {
  const i18n = inject(I18N_KEY);
  if (!i18n) throw new Error("i18n provider is missing");
  return i18n;
}
