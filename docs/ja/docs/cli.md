---
last_modified_at: 2026-03-24
layout: docs
title: CLI
seo_title: "SnapTray CLI：スクリーンショット、ピン留め、設定の自動化"
description: キャプチャ、ピン留め、設定ワークフローをスクリプト化する公式自動化インターフェース。
permalink: /ja/docs/cli/
lang: ja
route_key: docs_cli
doc_group: advanced
doc_order: 1
---

CLI は SnapTray の公式自動化インターフェースです。ローカルキャプチャワークフローのスクリプト化、IPC を通じた実行中アプリの操作、シェルや CI ジョブからの設定管理に使用します。

## CLI ヘルパーのインストール

### macOS

設定 > 一般 を開き、CLI インストールアクションを使って `/usr/local/bin/snaptray` を作成します。管理者権限が必要です。

### Windows

設定 > 一般 を開き、CLI インストールアクションを使って SnapTray の実行ファイルディレクトリを現在のユーザーの `PATH` に追加します。インストールまたはアンインストール後は新しいターミナルを開いてください。

### Linux beta

設定 > 一般 を開き、CLI インストールアクションを使って AppImage の `~/.local/bin/snaptray` ラッパーを作成します。`~/.local/bin` が `PATH` に含まれていることを確認し、インストールまたはアンインストール後は新しいターミナルを開いてください。

### パッケージ化された Windows ビルド

一部のインストーラーではすでに `snaptray` が `PATH` または App Execution Alias で利用可能になっている場合がありますが、アプリ内のインストールと削除フローが現在のアプリケーションコードで説明されている標準動作です。

## コマンド一覧

| コマンド | 説明 | メインの SnapTray インスタンスが必要 |
|---|---|---|
| `full` | カーソル下の画面全体をキャプチャ、または `-n` で指定した画面をキャプチャ | 不要 |
| `screen` | 特定の画面をキャプチャ、または `--list` で画面一覧を表示 | 不要 |
| `region` | 選択した画面から `-r x,y,width,height` で範囲をキャプチャ | 不要 |
| `gui` | 範囲キャプチャ GUI を開く | 必要 |
| `canvas` | スクリーンキャンバスモードを切り替え | 必要 |
| `pin` | 画像ファイルまたはクリップボード画像をピン留め | 必要 |
| `config` | 設定の一覧表示、取得、設定、リセット；オプションなしで設定を開く | 一部 |

## コマンド例

```bash
# ヘルプとバージョン
snaptray --help
snaptray --version
snaptray full --help

# ローカルキャプチャコマンド
snaptray full                         # カーソル下の画面をキャプチャ
snaptray full -c                      # フル画面キャプチャをクリップボードにコピー
snaptray full -d 1000 -o shot.png     # 1秒後に保存
snaptray full -n 1 -o screen1.png     # 画面 1 をファイルにキャプチャ
snaptray full --raw > shot.png        # PNG バイトを標準出力に書き出し
snaptray screen --list                # 利用可能な画面の一覧表示
snaptray screen 0 -c                  # 画面 0 をキャプチャ（位置指定構文）
snaptray screen -n 1 -o screen1.png   # 画面 1 をキャプチャ（オプション構文）
snaptray screen 1 -o screen1.png      # 画面 1 をファイルにキャプチャ
snaptray region -r 0,0,800,600 -c     # 画面 0 の範囲をクリップボードにキャプチャ
snaptray region -n 1 -r 100,100,400,300 -o region.png
snaptray region -r 100,100,400,300 -o region.png

# IPC コマンド
snaptray gui                          # 範囲セレクターを開く
snaptray gui -d 2000                  # 2秒後に開く
snaptray canvas                       # スクリーンキャンバスを切り替え
snaptray pin -f image.png             # 画像ファイルをピン留め
snaptray pin -c --center              # クリップボード画像を中央にピン留め
snaptray pin -f image.png -x 200 -y 120
snaptray config                       # 設定ダイアログを開く

# ローカル設定コマンド
snaptray config --list
snaptray config --get hotkey
snaptray config --set files/filenamePrefix SnapTray
snaptray config --reset
```

## 動作に関する注意事項

- キャプチャコマンド（`full`、`screen`、`region`）はデフォルトで PNG として保存します。
- `--clipboard` は保存の代わりにコピーします。`--raw` は PNG バイトを標準出力に書き出します。
- `--output` は `--path` より優先されます。どちらも指定しない場合、SnapTray は設定されたスクリーンショットディレクトリにファイル名を生成します。
- `screen` は `snaptray screen 1` と `snaptray screen -n 1` の両方をサポートします。
- `region` は `-r/--region` が必須で、選択した画面を基準とした論理ピクセルを使用し、矩形はその画面内に収まる必要があります。
- `pin` は `--file` または `--clipboard` のいずれか一方が必須です。`--file` は読み取り可能な画像である必要があります。カスタム配置は `-x` と `-y` の両方が指定された場合のみ適用されます。指定がない場合、ピンは中央に配置されます。
- `config --set` は単一の位置指定値を受け付けます。`config --reset` は設定ストア全体をクリアします。

## リターンコード

| コード | 意味 |
|---|---|
| `0` | 成功 |
| `1` | 一般エラー |
| `2` | 無効な引数 |
| `3` | ファイルエラー |
| `4` | インスタンスエラー（メインアプリが実行されていない） |

## 関連ドキュメント

- [はじめに](/ja/docs/getting-started/)
- [トラブルシューティング](/ja/docs/troubleshooting/)
