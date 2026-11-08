# MeLoom

**MeLoom** は、MIDI 入力された和音・単音を五度圏（Circle of Fifths）上に配置し、リアルタイムに幾何学的な図形として可視化するオーディオプラグイン／スタンドアロンアプリケーションです。

従来のコードネーム表示に依存せず、ピッチクラス間のインターバル距離や和音の幾何学的対称性を直感的に把握できます。

---

## 主な特徴 (Features)

- **五度圏へのリアルタイムプロット**: MIDI 入力されたノートのピッチクラスを即座に五度圏上にマッピング
- **音程に応じた色彩表現**: 構成音間の五度圏上の距離（1〜6）に応じて、HSV カラーマップに基づいた線分を描画
- **最低音（ベース/ルート）の強調表示**: 同時入力されたノートのうち、最低音のマーカーをハイライト表示
- **レスポンシブ UI**: ウィンドウのリサイズに追従し、円の半径・フォントサイズ・線の太さが自動スケーリング
- **マルチフォーマット対応**: macOS 向けの **Standalone アプリ** および **VST3 プラグイン** に対応

---

## 動作要件 (Requirements)

- **OS**: macOS
- **対応フォーマット**:
  - Standalone アプリケーション
  - VST3 プラグイン（Reaper、Logic Pro、AudioPluginHost などの VST3 対応 DAW）

---

## ビルド方法 (Build)

macOS の Xcode コマンドラインツール（`xcodebuild`）を使用してビルドします。

### 1. スタンドアロンアプリのビルドと起動

```sh
# Debug ビルド
xcodebuild -project Builds/MacOSX/HarmonyVisualizer.xcodeproj \
  -scheme "HarmonyVisualizer - Standalone Plugin" \
  -configuration Debug build

# アプリの起動
open "Builds/MacOSX/build/Debug/MeLoom(Debug).app"
```

### 2. VST3 プラグインのビルド

```sh
# VST3 プラグインのビルド
xcodebuild -project Builds/MacOSX/HarmonyVisualizer.xcodeproj \
  -scheme "HarmonyVisualizer - VST3" \
  -configuration Debug build
```

生成された VST3 プラグインをお使いの DAW または `AudioPluginHost` に読み込ませて使用します。

---

## ドキュメント (Documentation)

- **[docs/MUSIC_THEORY.md](docs/MUSIC_THEORY.md)**: 五度圏幾何学モデル、ボイスリーディング引力・コスト関数、および 3D 演出の技術仕様書
- **[AGENTS.md](AGENTS.md)**: 開発フロー、Issue / ブランチ規約、リアルタイム・オーディオセーフティ設計原則

---

## 作成者 (Author)

- **yuda**
  - Website: [yuda.world](https://yuda.world)
  - GitHub: [@yudasita](https://github.com/yudasita)
