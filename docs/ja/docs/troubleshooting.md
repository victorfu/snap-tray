---
last_modified_at: 2026-05-18
layout: docs
title: トラブルシューティング
seo_title: "SnapTray トラブルシューティング：権限、ホットキー、録画の問題を解決"
description: 権限、起動、Qt デプロイメント、エンコード、ホットキーの問題、macOS・Windows の録画問題を解決。
permalink: /ja/docs/troubleshooting/
lang: ja
route_key: docs_troubleshooting
doc_group: advanced
doc_order: 2
---

## キャプチャが開始されない

### macOS

- `システム設定 > プライバシーとセキュリティ > 画面収録` で画面収録の許可を確認
- `システム設定 > プライバシーとセキュリティ > アクセシビリティ` でアクセシビリティの許可を確認
- いずれかの許可を変更した後は SnapTray を再起動

### Windows

- キャプチャが失敗するか範囲セレクターが表示されない場合は GPU ドライバーを更新
- ローカル開発ビルドで必要なランタイム依存関係がデプロイされていることを確認
- 音声を録音している場合のみマイクの許可を確認

## トレイアイコンが表示されない

- SnapTray を再起動
- アプリが OS のセキュリティプロンプトによってブロックされていないことを確認
- ローカル開発ビルドを実行している場合は、プラットフォームビルドスクリプトを再実行して依存関係を確認

それでもアプリが表示されない場合は、以下でリビルドしてください：

- macOS/Linux beta：`./scripts/build.sh`
- Windows：`scripts\build.bat`

## macOS で Gatekeeper がアプリの起動をブロックする

公式の署名・公証済みリリースは Gatekeeper の警告なしに起動するはずです。

DMG を手動で確認するには：

```bash
spctl -a -vv -t open "dist/SnapTray-<version>-macOS.dmg"
```

ローカルのアドホックまたは開発ビルドのみの場合、隔離属性をクリアできます：

```bash
xattr -cr /Applications/SnapTray.app
```

## Windows で DLL の不足または Qt プラグインエラーが表示される

`Qt6Core.dll was not found` または `no Qt platform plugin could be initialized` などのメッセージが表示される場合は、`windeployqt` で Qt 依存関係をデプロイしてください：

```batch
C:\Qt\6.10.1\msvc2022_64\bin\windeployqt.exe build\bin\SnapTray-Debug.exe
```

`CMAKE_PREFIX_PATH` に渡したものと同じ Qt インストールパスを使用してください。

## ホットキーが反応しない

- 設定 > ホットキー を開いてアクションがまだバインドされていることを確認
- アクションを再バインドしてすぐにテスト
- 別のグローバルホットキーユーティリティとの競合を確認
- Windows 11 では、`Print Screen` を SnapTray にバインドする前に `設定 > アクセシビリティ > キーボード > Print Screen キーを使用して画面キャプチャを開く` を無効にしてください
- 頻繁に使用するアクションはシンプルな単一コンボショートカットに保ちます

### Linux beta：Wayland でアプリが終了する

Ubuntu 22.04 beta は X11 セッションのみをサポートしています。ログイン画面で
SnapTray を起動する前に X11 セッションを選択してください。

### Linux beta：ホットキーが登録されない

グローバルホットキーには X11 セッションが必要で、デスクトップ環境の
ショートカットと競合する場合があります。設定 > ホットキー を開いて失敗した
ショートカットを確認し、別のキーシーケンスを割り当ててください。

## 録画の問題（macOS と Windows のみ）

Linux beta には録画が含まれていません。Linux beta の起動、ホットキー、
Wayland の問題については、上記の Linux beta の注意事項を参照してください。

- フレームが落ちる場合はフレームレートを下げる
- 長い録画には MP4 を優先
- 選択した音声ソースとデバイスを再確認
- macOS では、システム音声キャプチャには macOS 13+ または BlackHole などの仮想オーディオデバイスが必要

## ビルドまたはローカル起動エラー

開発環境の場合は、リポジトリスクリプトでツールチェーン全体を確認してください：

- macOS/Linux beta：`./scripts/build.sh` 次に `./scripts/run-tests.sh`
- Windows：`scripts\build.bat` 次に `scripts\run-tests.bat`

パッケージングや署名の問題をデバッグしている場合は [リリース & パッケージング](/developer/release-packaging/) に進んでください。
