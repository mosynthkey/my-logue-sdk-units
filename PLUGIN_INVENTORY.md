# プラグイン一覧

- **airFM** (`airfm`) — fm
  - 対応: nts-3_kaoss
  - Alesis airSynth 風の FM 効果音シンセ。

- **AirHorn** (`airhorn`) — osc
  - 対応: nts-1_mkii, nts-3_kaoss, microkorg2
  - エアホーン。実測ルート〜302 Hz を D4 に補正。NTS-1/mk2 は PMODE=Fixed|Key（鍵盤は D4 基準のコンサートピッチ）。NTS-3 は DECAY（127=Sustain）と PMODE=Fixed|Pitch＋連続 PITCH(±2oct, X)。

- **AmenTime** (`amentime`) — drum（実験的）
  - 対応: nts-3_kaoss
  - 合成した 1 小節の amen 風ブレイクをホスト BPM に同期してスライス再生。オリジナル録音は同梱しない。X=開始 16 分、Y=グリッド。

- **WavSlice** (`wavslice`) — drum（実験的）
  - 対応: nts-3_kaoss
  - 汎用 1 小節 WAV スライサー。同梱はオリジナル CC0 ドラムループ。`assets/loop.wav` または `make embed WAV=...` で差し替え。

- **BeatRepeat** (`beatrepeat`) — fx（実験的）
  - 対応: nts-3_kaoss
  - テンポ同期の Beat Repeat。AUDIO IN を常時キャプチャし、グリッド上でスライスをスタッター。X=ループ長、Y=発火確率、タッチで強制フリーズ。

- **DataBend** (`databend`) — fx（実験的）
  - 対応: nts-3_kaoss
  - CDスキップ・テープ切れ・ビット腐敗をまとめたメディア破損バッファ。X=ベンド、Y=コラプト。タッチで破損フレームをノイズ壁に固定。

- **DredBass** (`dredbass`) — synth（実験的）
  - 対応: nts-3_kaoss
  - UKG / ジャングル風サクションベース。LPF エンベロープが逆向きに開き、反転したような低音。X=ピッチ、Y=サクション深さ。

- **EucGate** (`eucgate`) — fx（実験的）
  - 対応: nts-3_kaoss
  - DJ Transform ゲート＋ユークリッド/確率チョップ。LFO トレモロではなくグリッド上のヒット。X=ヒット数、Y=デューティ/確率。タッチ=Fill。

- **EucRoll** (`eucroll`) — fx（実験的）
  - 対応: nts-3_kaoss
  - ユークリッド・ステップロール。常時録音。タッチ中はヒット・ステップだけをロール（非ヒットはドライ）。X=密度、Y=細分化、Depth=ランダムパン幅。

- **FbOsc** (`fbackosc`) — osc（実験的）
  - 対応: nts-1_mkii, microkorg2
  - JP-8080 風フィードバックオシレータ。帯域制限ソーをキー追従の共振コムで加工。

- **GlitchPad** (`glitchpad`) — fx（実験的）
  - 対応: nts-3_kaoss
  - Glitch² 風 XY パッド。テンポ同期の AUDIO IN バッファからシーン（RTRG/REV/SHUF 等）を再生。非タッチ=バイパス。X=シーン、Y=スライス/レート。

- **GrainPad** (`grainpad`) — fx（実験的）
  - 対応: nts-3_kaoss
  - 直近 AUDIO IN を最大3秒フリーズ→Hann風グラニュラー。X=疎↔密、Y=±1oct。ENV=粒のA/R、SPRD / HPF / REVS。

- **ReGrain** (`regrain`) — fx（実験的）
  - 対応: nts-3_kaoss
  - 深いウェット・リバーブを常時かけてその音をキャプチャ。タッチでフリーズ→グレイン雲。X=密度、Y=SIZE（残響の深さ）、TONE/ENV/SPRD/REVS。

- **GridsDrum** (`gridsdrum`) — synth（実験的）
  - 対応: nts-3_kaoss
  - Mutable Grids 風の BD/SD/HH 生成フレーズ。X=キック/スネアマップ、Y=ハット密度。右上フリックで 1 小節 Fill。

- **UKGarage** (`ukgarage`) — drum（実験的）
  - 対応: nts-3_kaoss
  - テンポ同期の 2-step UK Garage キット。X=ゴーストノート密度、Y=ハット／Fill。右上フリックで 1 小節 Fill。ゴーストがキモ。

- **BoomBap** (`boombap`) — drum（実験的）
  - 対応: nts-3_kaoss
  - テンポ同期のブームバップキット。X=ゴースト、Y=ハット。既定〜90 BPM。

- **BreakBeat** (`breakbeat`) — drum（実験的）
  - 対応: nts-3_kaoss
  - Amen 風シンセ・ブレイクビー（サンプルではない）。X=シンコペ／ゴースト、Y=エネルギー。既定〜174 BPM。

- **DnBass** (`dnbass`) — drum（実験的）
  - 対応: nts-3_kaoss
  - ドラムンベース。ハーフタイム・スネア（3拍目）。X=ローリング・ハット、Y=ブレイク。既定〜174 BPM。

- **Trance** (`trance`) — drum（実験的）
  - 対応: nts-3_kaoss
  - トランス。ハットは TR-909 ROM、キック／クラップは 909 アナログ回路モデル。四つ打ち＋裏拍オープンハット。X=ハット、Y=ビルド。既定〜138 BPM。

- **Dembow** (`dembow`) — drum（実験的）
  - 対応: nts-3_kaoss
  - テンポ同期のレゲトン・デンボウキット。X=リム／チャ応答、Y=パーカッション。既定〜96 BPM。

- **Footwork** (`footwork`) — drum（実験的）
  - 対応: nts-3_kaoss
  - シカゴ・フットワーク／ジューク。X=キック・スタッター、Y=スネアロール。既定〜160 BPM。

- **HClap** (`hclap`) — drum（実験的）
  - 対応: nts-3_kaoss
  - 808/909 風アナログハンドクラップのフレーズパッド。X=ヒット密度、Y=808↔909。調整中。

- **HHat** (`hhat`) — drum（実験的）
  - 対応: nts-3_kaoss
  - TR-909 ハイハット PCM 回路モデル。X=ユークリッド密度、Y=Close→Open（CH/OH ROM 窓＋減衰）。調整中。

- **HSnare** (`hsnare`) — drum（実験的）
  - 対応: nts-3_kaoss
  - 808/909 風アナログスネアのフレーズパッド。X=ヒット密度、Y=808↔909。調整中。

- **HyperSaw** (`hypersaw`) — osc（実験的）
  - 対応: nts-1_mkii, microkorg2
  - Virus TI 風 9 ボイス・デチューンソー。Density / Spread / HyperSub / ステレオ幅。

- **Kaocid** (`kaocid`) — synth
  - 対応: nts-3_kaoss
  - XY パッドで弾く 303 風シンセ。タップでフレーズ生成。調整中。正式リリース予定。

- **LoopKey** (`loopkey`) — loopkey（実験的）
  - 対応: nts-1_mkii
  - キーボード制御のマイクロルーパー osc。外部入力をループし、MIDI ノートでループ長を設定。テンポ同期・ゲート・進化。

- **MoHowl** (`mohowl`) — synth
  - 対応: nts-3_kaoss
  - 作者モチーフのフィードバック・ハウル。JP-8080 風コムスクリーム。X=LFO 深さ、Y=ハーモニクス。

- **PercIter** (`perciter`) — synth（実験的）
  - 対応: nts-3_kaoss
  - Basimilus 風パーカッションパッド。加算/FM ボディ＋ノイズ＋フォルダ。Y で Skin↔Metal。タッチがトリガ、連打でフィル。

- **ReesePhr** (`reesephr`) — synth（実験的）
  - 対応: nts-3_kaoss
  - デチューン・リース・ドローン＋疎なフレーズ生成。タッチでビートするスーパーソーをゲート。1–2 小節ごとにルートが短3度歩いて圧力変化。

- **RevRoll** (`revroll`) — fx（実験的）
  - 対応: nts-3_kaoss
  - DJM Rev Roll。タッチでテンポ同期スライスをキャプチャし逆再生で繰り返す。X=スライス長、Y=逆再生カーブ。

- **Ride909** (`ride909`) — drum
  - 対応: nts-3_kaoss
  - テクノ定番の裏拍 909 ライドシンバル。ピッチ調整可。調整中。正式リリース予定。

- **RingExcit** (`ringexcit`) — fx（実験的）
  - 対応: nts-3_kaoss
  - 入力励起レゾネータ。キックやノイズで Karplus-Strong 弦＋3 モーダル倍音を弾く。タッチで内部ノイズ・プラック（ワンショットにも）。

- **Shaker** (`shaker`) — shaker
  - 対応: nts-1_mkii, nts-3_kaoss
  - PhISEM シェイカーの移植。XY パッドやキーボードで各種パーカッション。調整中。正式リリース予定。

- **StepFenv** (`stepfenv`) — fx（実験的）
  - 対応: nts-3_kaoss
  - テンポ同期のステップ・フィルタ・エンベロープ。各ステップでカットオフEG（アタック0、ディケイ可変、サスティン0、リリース0）。X=カットオフ、Y=エンベロープ深さ。タッチでグリッドリセット。

- **StepRndFlt** (`steprndflt`) — fx（実験的）
  - 対応: nts-3_kaoss
  - テンポ同期の S&H LFO をマルチモード・フィルタのカットオフへ。各ステップでランダム値をホールド。X=LFO深さ、Y=レゾナンス。TYPE=LP12/LP24/BPF/HP12/HP24。タッチでエンゲージ。

- **Trap808** (`trap808`) — drum（実験的）
  - 対応: nts-3_kaoss
  - トラップ風ドラム＋TR-909 ROMハイハット＋スライド808。ホールドはゲートのみで発音はビート同期。X=ハット密度／ロール、Y=グルーヴ、ROOT=808キー。右上フリックで次の拍頭から1小節フィル。

- **TapeOsc** (`tapeosc`) — osc（実験的）
  - 対応: nts-1_mkii
  - テープモーター始動/停止つきバリスピード・オシレータ。ピッチエンベロープではなくテープデッキのように減速・凍結。

- **TechnoRumble** (`technorumble`) — fx（実験的）
  - 対応: nts-1_mkii, nts-3_kaoss
  - テクノ・ランブルキック処理。長いリバーブテール、サブ LPF、ドライブ、トランジェント連動サイドチェーン。

- **TransitionLooper** (`transitionlooper`) — fx（実験的）
  - 対応: nts-3_kaoss
  - DJ トランジション・ルーパー。AUDIO IN をテンポ同期 16 ステップ・ステレオループにキャプチャ。ホールドでループへフェード、離すと復帰。

- **WarpsMorph** (`warpsmorph`) — fx（実験的）
  - 対応: nts-3_kaoss
  - Mutable Warps 風クロスモッド・モーフ。ダイオードリング / XOR / コンパレータ / ミニボコーダ / ウェーブフォルダを横断。

