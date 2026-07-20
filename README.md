# foo_musiccom

foobar2000 2.x (64-bit) 用の ASCII `MUSIC.COM` MML 入力コンポーネントです。PC-98の YM2608 (OPNA) を `ymfm` でエミュレートします。無限ループ (`{0 ... }` または回数省略の `{ ... }`) は2回展開し、末尾8秒をフェードアウトします。

## 現在の対応範囲

- FM 3ch / SSG 3ch、`A`–`G`, `R`, `W`, `L`, `O`, `<`, `>`, `T`, `V`, `@`, `Q`, `N`, `Y`
- `STR:` 文字列マクロ（再帰展開）、最大16段の反復、`SOUND:` FM音色、`SSGENV:` 読み込み
- 2ループ、8秒フェード、シーク、48 kHz stereo float出力
- MUSIC.COM Ver 2.21の静的解析補助ツール

`P/U/I`（ポルタメント、トレモロ、ビブラート）、SSGソフトウェアエンベロープ、リズムパートは解析途中で、現状は構文を読み飛ばします。音長丸めなどの未文書化挙動は実機バイナリのI/Oトレースを基準に詰める予定です。

## ビルド

Visual Studio 2022の「Desktop development with C++」を入れ、x64 Native Tools Command Promptで:

```powershell
powershell -ExecutionPolicy Bypass -File tools/build.ps1
```

コア単体のテストとWAVレンダラーはCMakeでもビルドできます。

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

生成された `dist/foo_musiccom.fb2k-component` を開くか、foobar2000のPreferences → Componentsからインストールします。単体確認は `musiccom_render input.mml output.wav` で行えます。

## バイナリ解析

```powershell
python tools/analyze_musiccom.py "Z:\Music\chiptune_or_like\MML\MML_MAN\MUSIC.COM" -o musiccom-disasm.txt
```

原資料や曲データは著作物なのでリポジトリには複製していません。テスト時に上記 `Z:` 配下を直接指定してください。

## Third-party

- foobar2000 SDK: SDK license (`third_party/foobar2000_sdk/sdk-license.txt`)
- ymfm: BSD 3-Clause, Copyright Aaron Giles (`third_party/ymfm/LICENSE`)

The vendored ymfm snapshot is commit `17decfae857b92ab55fbb30ade2287ace095a381`.
