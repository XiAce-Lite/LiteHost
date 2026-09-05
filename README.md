# LiteHost

DAWを開くほどではない、という人向けの軽量 VST3 ホストです。録音もタイムラインもありません。トラックを足して入力を選び、エフェクトを挿して鳴らすだけです。

## できること

- オーディオインターフェイスの入力をトラックごとに指定。MIDI 入力機器も指定でき、シンセ／ドラム音源を発音可能（`MIDI: すべて` で全入力）
- 各トラックとメインアウトに VST3 を **最大 10 個まで** 直列挿入（SyncRoom はメインアウト向け）
- メインアウトに簡易リバーブとセーフティリミッター（初期状態はオフ）、出力レベルフェーダー。どちらも JUCE の基本 DSP で、専用プラグインほどの調整幅や音質は想定していない
- トラックに **Trim**（フェーダー前ゲイン、丸ノブ −24〜+24 dB）とパン
- **オーディオ停止／開始**（過負荷時にエンジンを切って復帰）
- **コントロールサーフェス**: Mackie Control / HUI / MMC（フェーダー・V-Pot=パン・Mute/Solo・バンク・再生/停止）
- **MIDI 学習**: 任意 MIDI 入力の CC/Note を Trim・フェーダー・パン・Mute・Solo に割り当て（コントロールを右クリック）
- Windows では初回起動時のデバイスタイプを **ASIO**、バッファは利用可能値のうち **128 前後** を選択（以後は前回の設定を復元）
- プロジェクトファイル（`.litehost`）でトラック構成・プラグイン・メイン設定を保存／読込
- ファイルメニューから新規・開く・保存・最近使ったプロジェクト。起動時は最後に開いたプロジェクトを復元。`LiteHost.exe path\to\project.litehost` でも開けます
- VST3 スキャンの標準対象は `Common Files\VST3` のみ（`C:\Users` 配下は含めない）。追加フォルダはユーザーが指定

信号の流れは次のとおりです。

`入力（オーディオ or MIDI） → トラック VST（最大10） → Trim → フェーダー／パン → ミックス → リバーブ → メイン VST（最大10、SyncRoom など） → リミッター → メイン出力`

## 仕様メモ

| 項目 | 内容 |
|------|------|
| トラック／メインの VST 数 | 各チェイン最大 **10**（UI でも追加を制限） |
| プロジェクト | `Documents\LiteHost\*.litehost` など。終了時に現在のプロジェクトを保存 |
| オーディオ設定 | `%AppData%\LiteHost\audio.xml`（プロジェクトとは別） |
| プラグイン一覧 | `%AppData%\LiteHost\knownPlugins.xml` |
| アプリ設定 | `%AppData%\LiteHost\settings.xml`（最終プロジェクト・最近・追加 VST パス・サーフェス） |
| 内蔵リバーブ／リミッター | 簡易（JUCE 標準 DSP）。本格的な空間系・マスタリング用途には VST を挿す |

## ビルド（Windows）

Visual Studio 2022 以降（C++ ワークロード）が必要です。

```powershell
cd D:\Documents\Github\LiteHost
.\scripts\build.ps1
```

初回は JUCE を `third_party\JUCE` に取得するので時間がかかります。成功すると `build\LiteHost_artefacts\Release\LiteHost.exe` ができます。

Windows ではデバイスタイプに **ASIO** が出ます（JUCE 同梱の ASIO ヘッダを使用。配布物は Steinberg ASIO SDK のライセンスに従う必要があります）。独自 SDK を使う場合は `third_party\asiosdk` に置き、CMake の `JUCE_ASIO_USE_EXTERNAL_SDK` を有効化してください。

## 使い方

1. **ファイル** メニューでプロジェクトを新規作成／開く（または前回のプロジェクトが自動で開く）。
2. **オーディオ / MIDI 設定**でインターフェイス、バッファ、入力チャンネル、MIDI 入力機器を選ぶ（初回は ASIO・バッファ約 128）。負荷時は **オーディオ停止／開始**でエンジンを切って復帰できる。
3. **サーフェス**で Mackie Control / HUI / MMC と MIDI 入出力を指定（NanoKontrol2 などは機器側を Mackie モードに）。
4. **VST3 スキャン**で `Common Files\VST3`（および追加したフォルダ）を読む。ユーザーフォルダ内のプラグインは「フォルダを追加」で指定。
5. トラックの入力にオーディオ ch または **MIDI:** 機器を選び、**+ VST** でエフェクト／音源を挿す（最大 10）。
6. SyncRoom 連携はメインアウトの **+ VST** に SyncRoom の VST3 を挿す。

## ライセンス

JUCE 9 を AGPLv3 で利用しています。このソースを配布する場合は AGPL の条件に従ってください。クローズドな配布には JUCE の商用ライセンスが必要です。
