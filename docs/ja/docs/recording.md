---
last_modified_at: 2026-03-26
layout: docs
title: 録画
seo_title: "SnapTray 画面録画：macOS・Windows 向け MP4、GIF、WebP キャプチャ"
description: "macOS/Windows のみ：フル画面ソースを MP4、GIF、WebP で録画・エクスポート。"
permalink: /ja/docs/recording/
lang: ja
route_key: docs_recording
doc_group: workflow
doc_order: 2
---

録画は macOS と Windows でのみ利用可能です。Linux beta には録画機能は含まれておらず、
録画 UI と CLI ヘルプ項目は非表示になっています。

## 録画の開始方法

- トレイメニュー：画面を録画
- CLI：`snaptray record start [--screen N]`

## 録画のライフサイクル

1. マルチディスプレイ環境では、録画する画面を選択します。
2. 選択した画面で録画がすぐに開始されます。
3. フローティングコントロールバーで録画時間を確認し、録画を停止します。
4. 停止をクリックしてエクスポートします。

## 出力フォーマット

| フォーマット | 典型的な用途 |
|---|---|
| MP4（H.264） | チュートリアル、長い録画 |
| GIF | 短いループデモ |
| WebP | 軽量なアニメーションスニペット |

## 音声オプション

現在のプラットフォームとソースでサポートされている場合、MP4 録画で音声キャプチャが利用可能です：

- マイク
- システム音声
- ミックスキャプチャ

GIF と WebP のエクスポートは無音です。

## 品質の調整

設定 > 録画 を開いて、フレームレート、品質、カウントダウン、プレビュー動作を調整します。
