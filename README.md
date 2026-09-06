# LiteHost

DAWを開くほどではない、という人向けの軽量 VST3 ホストです。録音もタイムラインもありません。トラックを足して入力を選び、エフェクトを挿して鳴らすだけです。

## できること

- オーディオインターフェイスの入力をトラックごとに指定。MIDI 入力機器も指定でき、シンセ／ドラム音源を発音可能（`MIDI: すべて` で全入力）
- 各トラックとメインアウトに VST3 を **最大 10 個まで** 直列挿入（SyncRoom はメインアウト向け）。プラグイン欄は縦スクロール可
- トラック名をドラッグして並べ替え
- **排他ソロ**（Cakewalk Exclusive Solo）と **Shift+ソロ**（Solo Override）。排他ソロ ON では通常の S は1本だけ。Override したトラックは他をソロしても残る
- メインアウトに簡易リバーブ、ノイズゲート、セーフティシーリング（初期状態はオフ）、出力レベルフェーダー。専用プラグインほどの調整幅や音質は想定していない
- トラックに **Trim**（フェーダー前ゲイン、丸ノブ −24〜+24 dB）とパン（どちらもセンター印付きの丸ノブ）
- **オーディオ停止／開始**（過負荷時にエンジンを切って復帰）
- **コントロールサーフェス**: Mackie Control / HUI / MMC（フェーダー・V-Pot=パン・Mute/Solo・バンク・再生/停止）
- **MIDI 学習**: 任意 MIDI 入力の CC/Note を、トラックの Trim・フェーダー・パン・Mute・Solo と、メインフェーダー・リバーブ（On/Mix/Size）・リミッター（On/Ceiling）・ゲート（On/閾値）に割り当て（コントロールを右クリック）
- 初回起動時のデバイスタイプは Windows では **ASIO**、macOS では **Core Audio**。バッファは利用可能値のうち **128 前後** を選択（以後は前回の設定を復元）
- プロジェクトファイル（`.litehost`）でトラック構成・プラグイン・メイン設定を保存／読込
- ファイルメニューから新規・開く・保存・最近使ったプロジェクト。起動時は最後に開いたプロジェクトを復元。`LiteHost.exe path\to\project.litehost` でも開けます
- VST3 スキャンの標準対象は、Windows では `Common Files\VST3`、macOS では `/Library/Audio/Plug-Ins/VST3`（ユーザー領域は含めない）。追加フォルダはユーザーが指定
- ウィンドウ位置とサイズを記憶。GitHub に新しいリリースがあれば起動時に通知

信号の流れは次のとおりです。

`入力（オーディオ or MIDI） → トラック VST（最大10） → Trim → フェーダー／パン → ミックス → ゲート → リバーブ → メイン VST（最大10、SyncRoom など） → シーリング → メイン出力`

## 仕様メモ

| 項目 | 内容 |
|------|------|
| トラック／メインの VST 数 | 各チェイン最大 **10**（UI でも追加を制限） |
| プロジェクト | `Documents\LiteHost\*.litehost` など。終了時に現在のプロジェクトを保存 |
| オーディオ設定 | Windows: `%AppData%\LiteHost\audio.xml` / macOS: `~/Library/Application Support/LiteHost/audio.xml` |
| プラグイン一覧 | 同上ディレクトリの `knownPlugins.xml` |
| アプリ設定 | 同上ディレクトリの `settings.xml`（最終プロジェクト・最近・追加 VST パス・サーフェス） |
| 内蔵リバーブ／リミッター | 簡易（JUCE 標準 DSP）。本格的な空間系・マスタリング用途には VST を挿す |

## ビルド

ソースは Windows / macOS 共通です。分岐は `#if JUCE_WINDOWS` / `#if JUCE_MAC` と CMake の OS 判定だけです。成果物だけ分かれます。

### Windows

Visual Studio 2022 以降（C++ ワークロード）が必要です。

```powershell
cd D:\Documents\Github\LiteHost
.\scripts\build.ps1
```

初回は JUCE を `third_party\JUCE` に取得するので時間がかかります。成功すると `build\LiteHost_artefacts\Release\LiteHost.exe` ができます。

未署名の exe を Windows Defender が `Bearfoos` などで誤検知し、削除することがあります。開発用なら管理者で `.\scripts\add-defender-exclusion.ps1` を一度実行し、`build`（と `dist`）を除外してください。

### macOS

Xcode と CMake が必要です。

```bash
./scripts/build-mac.sh
```

成功すると `build/LiteHost_artefacts/Release/LiteHost.app` ができます。AU はまだホストしていません（VST3 のみ。Windows と同じ）。

`master` への push と GitHub Release の公開時に、Actions が Apple Silicon 向け `LiteHost-*-macos-arm64.zip` を作り、同じバージョンのリリース（`vX.Y.Z`）へ添付します。未署名なので Gatekeeper が初回起動を止めることがあります。署名と公証は別途必要です。

Windows ではデバイスタイプに **ASIO** が出ます（JUCE 同梱の ASIO ヘッダを使用。配布物は Steinberg ASIO SDK のライセンスに従う必要があります）。独自 SDK を使う場合は `third_party\asiosdk` に置き、CMake の `JUCE_ASIO_USE_EXTERNAL_SDK` を有効化してください。

## 使い方

1. **ファイル** メニューでプロジェクトを新規作成／開く（または前回のプロジェクトが自動で開く）。
2. **オーディオ / MIDI 設定**でインターフェイス、バッファ、入力チャンネル、MIDI 入力機器を選ぶ（初回は Windows なら ASIO、Mac なら Core Audio。バッファ約 128）。負荷時は **オーディオ停止／開始**でエンジンを切って復帰できる。
3. **サーフェス**で Mackie Control / HUI / MMC と MIDI 入出力を指定（NanoKontrol2 などは機器側を Mackie モードに）。
4. **VST3 スキャン**で `Common Files\VST3`（および追加したフォルダ）を読む。ユーザーフォルダ内のプラグインは「フォルダを追加」で指定。
5. トラックの入力にオーディオ ch または **MIDI:** 機器を選び、**+ VST** でエフェクト／音源を挿す（最大 10）。
6. SyncRoom 連携はメインアウトの **+ VST** に SyncRoom の VST3 を挿す。

## ライセンス

LiteHost 本体のソースコードは [MIT License](LICENSE) です。

JUCE 9 は AGPLv3（または Raw Material Software の商用ライセンス）です。JUCE とリンクしたバイナリを配布する場合は、JUCE 側の条件にも従ってください。ASIO を含む配布は Steinberg のライセンスにも従う必要があります。
