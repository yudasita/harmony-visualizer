# AGENTS.md

> [!IMPORTANT]
> **このファイルはこのリポジトリにおける規則の正典です。**
> 人間・AIエージェントを問わず、作業を始める前に必ず最初に読んでください。
> 他のドキュメント（README.md など）と内容が食い違う場合は、このファイルが優先されます。

---

## 1. プロジェクト概要

| 項目 | 内容 |
|---|---|
| 名前 | HarmonyVisualizer |
| 種別 | オーディオプラグイン（VST3 / Standalone） |
| カテゴリ | アナライザー / ビジュアライザー（Synth / MIDI Effect 特性） |
| フレームワーク | JUCE (C++17) |
| プロジェクト管理 | Projucer (`HarmonyVisualizer.jucer`) |
| バンドル ID | `world.yuda.HarmonyVisualizer` |
| リポジトリ | https://github.com/yudasita/harmony-visualizer |
| 目的 | MIDI 入力された和音・単音を五度圏（Circle of Fifths）上に配置し、音程間隔に応じた色彩の線分・多角形としてリアルタイムに可視化する |

---

## 2. どこに何があるか

```
harmony-visualizer/               # リポジトリルート
├── AGENTS.md                     # 規則の正典（このファイル）
├── HarmonyVisualizer.jucer       # Projucer プロジェクト設定（モジュール・ビルド構成の正典）
├── Source/                       # C++ ソースコード
│   ├── PluginProcessor.h         # 音声・MIDI 処理プロセッサのヘッダ
│   ├── PluginProcessor.cpp       # MIDI 受信・ノート番号の保持（processBlock）
│   ├── PluginEditor.h            # UI / 描画エディタのヘッダ
│   └── PluginEditor.cpp          # 五度圏・多角形・ノート名の描画ロジック（paint / resized）
├── Builds/                       # 各プラットフォーム向けビルドファイル（Git 管理外推奨）
│   └── MacOSX/                   # macOS Xcode プロジェクト（HarmonyVisualizer.xcodeproj）
└── JuceLibraryCode/              # JUCE 自動生成ラッパーコード（Git 管理外推奨）
```

- `Builds/` および `JuceLibraryCode/` は Projucer によって再生成されるファイル群を含むため、直接手動編集せず、原則として `Source/` および `HarmonyVisualizer.jucer` を編集する。
- `.gitignore` に指定された一時ファイルや生成物はコミットしない。

---

## 3. ビルドと確認方法

macOS 環境で Xcode コマンドラインツール（`xcodebuild`）を使用してビルドと動作確認を行う。

### 3.1 スタンドアロンアプリのビルド

```sh
xcodebuild -project Builds/MacOSX/HarmonyVisualizer.xcodeproj \
  -scheme "HarmonyVisualizer - Standalone Plugin" \
  -configuration Debug build
```

### 3.2 実行と動作確認

```sh
# Debug ビルド成果物の起動
open "Builds/MacOSX/build/Debug/HarmonyVisualizer(Debug).app"
```

### 3.3 VST3 プラグインのビルド

```sh
xcodebuild -project Builds/MacOSX/HarmonyVisualizer.xcodeproj \
  -scheme "HarmonyVisualizer - VST3" \
  -configuration Debug build
```

- 生成された VST3 プラグインは `AudioPluginHost`、Reaper、Logic Pro などの DAW に読み込ませてテストする。

### 3.4 確認事項

- コンパイルエラーおよびリンクエラーがないこと。
- スタンドアロンアプリがクラッシュせずに起動すること。
- MIDI キーボードや仮想 MIDI 入力からノート ON/OFF を送信した際、正しく五度圏上の位置にプロットされ、線で結ばれること。
- ウィンドウをリサイズした際、描画要素（円、点、フォント、線）が崩れず適切に拡大縮小されること。

---

## 4. 守ること・設計原則

### 4.1 オーディオリアルタイムスレッドの安全性（Audio Safety）
`PluginProcessor::processBlock` は高優先度のオーディオリアルタイムスレッドで実行される。

- **ブロッキング操作の禁止**: UI スレッドとの競合による音途切れ（Priority Inversion）を防ぐため、安易な排他ロック（`juce::CriticalSection` や `std::mutex`）の長時間保持を避ける。
- **ヒープメモリ確保の禁止**: `processBlock` 内での `std::vector::push_back` や `new` など、動的メモリ再確保が発生する処理を避ける。
- **システムコール / I/O の禁止**: `processBlock` 内でのファイル読み書きやログ出力（`juce::Logger::writeToLog` など）は行わない。
- **ロックフリー化の志向**: プロセッサからエディタへのデータ受け渡しは、`std::atomic`（12音のビットマスクなど）や JUCE の `AbstractFifo` によるロックフリーな通信を推奨する。

### 4.2 UI・描画の設計
- **相対スケーリングの維持**: ピクセル単位の絶対座標をハードコードせず、常に `resized()` で計算される `circleRadius` や領域の比率に基づいて各サイズ・フォント・線の太さを決定する。
- **クラッシュ耐性**: 配列やマップの参照（例: `pitchLine_colours.find(dist)`）では、万が一範囲外のキーが渡された場合でも範囲外アクセス・未定義動作にならないようガードを入れる。

### 4.3 ファイルの追加・削除
- `Source/` 配下に新しい `.cpp` / `.h` を追加した場合は、Xcode プロジェクトだけでなく、**必ず `HarmonyVisualizer.jucer` にもファイルエントリを追加** する（Projucer で再保存した際に Xcode プロジェクトから消えてしまうのを防ぐため）。

---

## 5. 開発の進め方

コードをいきなり書き始めず、次の手順で進める。

### 5.1 Issue を確認・作成する
1. 作業開始前に開いている Issue を確認する（`gh issue list --state open`）。
2. 着手する Issue の内容・背景・完了条件を読む（`gh issue view <番号>`）。
3. 対応する Issue がない作業は、**必ず先に Issue を起票** し、作業内容・目的を明確にしてから着手する。起票前にユーザーへ確認を取る。

### 5.2 ブランチを切る
- `main` ブランチに直接コミットしない。
  ```sh
  git switch main
  git pull
  git switch -c <種類>/<短い説明>
  ```
- ブランチ名のプレフィックス:
  - `feat/`: 新機能追加（例: コードネーム自動判定、カラーテーマ切り替え）
  - `fix/`: バグ修正（例: クラッシュ防止、描画ズレ修正）
  - `refactor/`: リファクタリング（例: ロックフリー化、データ構造最適化）
  - `docs/`: ドキュメント作成・更新（例: README、AGENTS.md）
  - `chore/`: ビルド設定・保守

### 5.3 コミットとプルリクエスト
- **1 コミット 1 関心**: リファクタリングと挙動変更を同一コミットに混在させない。
- **コミットメッセージ**: 日本語で「何を・なぜ変更したか」を明瞭に書く。
- **PR の作成**: 本文には「目的」「変更点」「確認方法」を明記し、`Closes #<番号>` で関連 Issue と紐付ける。

---

## 6. AI エージェント向けの追加規則

- **言語**: 回答、コミットメッセージ、Issue、PR はすべて **日本語** で記述する（コード内識別子・コメント英語指定箇所を除く）。
- **確認が必須の操作**: 次の破壊的・不可逆な操作は **実行前に必ずユーザーに確認** する。
  - `git push --force` や `git reset --hard` などの履歴改変
  - ファイル・ディレクトリの削除
  - リポジトリの公開範囲変更
- **作業前の状態確認**: 作業開始時に `git status` を確認し、未コミットの変更がある場合は扱いをユーザーに確認する。
- **一時ファイルのクリーンアップ**: PR 作成前や作業完了時に、不要なビルド一時ファイルやログが残っていないか確認し整理する。
