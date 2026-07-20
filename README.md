# foo_input_asciimusiccom

ASCII `MUSIC.COM` 形式の MML ファイルを再生する、foobar2000 2.x（64-bit）向け入力コンポーネントです。PC-98 の YM2608（OPNA）を [ymfm](https://github.com/aaronsgiles/ymfm) でエミュレートします。

> [!NOTE]
> 開発中のコンポーネントです。MUSIC.COM のすべての構文や音源挙動との完全な互換性はありません。

## 現在の対応範囲

- FM 3 チャンネル、SSG 3 チャンネル
- 音符 `A`–`G`、休符 `R` / `W`
- `L`、`O`、`<`、`>`、`T`、`V`、`@`、`Q`、`Y`、`P`
- スラー `&`、付点、繰り返し、入れ子の繰り返し
- `STR:` マクロ、`SOUND:` FM 音色、`SSGENV:` ソフトウェアエンベロープ
- 無限ループ表記（回数省略、`0`、`99`）を 2 ループ再生後、8 秒間フェードアウト
- 48 kHz、ステレオ、32-bit floating-point PCM 出力

`N` / `I` / `U` / `S` / `M` など一部のコマンドは未実装または読み飛ばします。LFO、リズムパート、YM2608 ADPCM、細かなタイミングや音源挙動にも未実装・未検証の部分があります。

## 必要なもの

- Windows 10 または 11（x64）
- foobar2000 2.x（64-bit）
- Visual Studio 2022 の「C++ によるデスクトップ開発」
- foobar2000 SDK
- ymfm

依存ソースはそれぞれ `third_party/foobar2000_sdk` と `third_party/ymfm` に配置します。リポジトリには含まれていません。

## ビルド

x64 Native Tools Command Prompt for VS 2022 で実行します。

```powershell
powershell -ExecutionPolicy Bypass -File tools/build.ps1
```

成功すると `dist/foo_input_asciimusiccom.fb2k-component` が生成されます。ファイルを開くか、foobar2000 の Preferences → Components からインストールしてください。

foobar2000 SDK を使わないコアライブラリ、テスト、WAV レンダラーは CMake だけでもビルドできます。

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

単体レンダラーは次のように使用します。

```powershell
build\Release\musiccom_render.exe input.mml output.wav
```

## 互換性に関する資料

MUSIC.COM Ver. 2.21 のバイナリ解析メモは [docs/binary-analysis.md](docs/binary-analysis.md) にあります。これは実装仕様ではなく、互換性調査時の参考資料です。

## Release

[GitHub CLI](https://cli.github.com/) をインストールして認証後、`origin` と同期した
クリーンなブランチから次のコマンドを実行します。

```powershell
powershell -ExecutionPolicy Bypass -File tools/release.ps1
```

ソース内のバージョン確認、パッケージのビルド、`vX.Y.Z` タグの作成と push、
`.fb2k-component` の GitHub Releases への公開を一括で行います。

## Third-party licenses

- foobar2000 SDK: SDK license（`third_party/foobar2000_sdk/sdk-license.txt`）
- ymfm: BSD 3-Clause License, Copyright Aaron Giles（`third_party/ymfm/LICENSE`）

使用している ymfm のスナップショットは commit `17decfae857b92ab55fbb30ade2287ace095a381` です。
