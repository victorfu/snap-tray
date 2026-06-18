---
last_modified_at: 2026-06-13
layout: docs
title: 자주 묻는 질문
seo_title: "SnapTray FAQ: 개인 정보 보호, 형식, OCR, 자동화 도움말"
description: 개인 정보 보호, 형식, OCR, 단축키, 자동화, 플랫폼별 제한에 관한 일반적인 질문.
permalink: /ko/docs/faq/
lang: ko
route_key: docs_faq
doc_group: advanced
doc_order: 3
faq_schema:
  - question: "SnapTray가 캡처 이미지를 업로드하나요?"
    answer: "자동 업로드는 없습니다. 캡처, 주석, 고정은 로컬에 유지됩니다. 녹화는 macOS/Windows 전용이며 역시 로컬에 유지됩니다."
  - question: "어떤 출력 형식을 사용해야 하나요?"
    answer: "macOS/Windows에서는 긴 동영상과 튜토리얼에 MP4, 짧은 루프나 소형 데모에 GIF, 가벼운 애니메이션 클립에 WebP를 사용하세요. Linux beta에서는 녹화 형식을 사용할 수 없습니다."
  - question: "OCR이 내 시스템에서 사용 불가능한 이유는?"
    answer: "OCR은 플랫폼 지원과 설치된 언어 팩이 있는 macOS/Windows에서 사용 가능합니다. Linux beta에는 포함되지 않으며 숨겨져 있습니다."
  - question: "기본 단축키를 사용자 지정할 수 있나요?"
    answer: "예. 설정 > 단축키를 열고 카테고리별로 편집하세요."
  - question: "스크립트에서 SnapTray를 사용할 수 있나요?"
    answer: "예. CLI가 로컬 캡처와 IPC 기반 제어를 위한 공식 자동화 인터페이스입니다."
  - question: "현재 플랫폼 제한 사항은 무엇인가요?"
    answer: "다중 모니터 및 혼합 DPI 캡처는 더 많은 실제 기기 테스트가 필요합니다. macOS/Windows에서 화면 녹화는 네이티브 API를 사용할 수 없을 때 Qt 캡처 엔진으로 폴백할 수 있습니다. macOS에서 시스템 오디오 캡처에는 macOS 13+ 또는 BlackHole과 같은 가상 오디오 장치가 필요합니다. Linux beta는 Ubuntu 22.04 X11 AppImage이며, 녹화와 OCR은 포함되지 않고 Wayland는 지원되지 않습니다."
  - question: "버그는 어디에 보고하나요?"
    answer: "GitHub에 이슈를 열고 OS 버전, SnapTray 버전, 재현 단계, 관련 스크린샷이나 녹화를 포함하세요."
---

## SnapTray가 캡처 이미지를 업로드하나요?

자동 업로드는 없습니다. 캡처, 주석, 고정은 로컬에 유지됩니다. 녹화는 macOS/Windows 전용이며 역시 로컬에 유지됩니다. 직접 다운로드 빌드에서 SnapTray는 플랫폼 네이티브 업데이터(macOS는 Sparkle, Windows는 WinSparkle)를 통해 업데이트를 확인하며, 이는 설정에서 비활성화할 수 있습니다.

## 어떤 출력 형식을 사용해야 하나요?

녹화와 애니메이션 내보내기는 macOS/Windows 전용입니다. 해당 플랫폼에서:

- 긴 동영상과 튜토리얼에는 MP4
- 짧은 루프나 소형 데모에는 GIF
- 가벼운 애니메이션 클립에는 WebP

## OCR이 내 시스템에서 사용 불가능한 이유는?

OCR은 플랫폼 지원과 설치된 언어 팩이 있는 macOS/Windows에서 사용 가능합니다. Linux beta에는 포함되지 않으며 숨겨져 있습니다.

## 기본 단축키를 사용자 지정할 수 있나요?

예. 설정 > 단축키를 열고 카테고리별로 편집하세요.

## 스크립트에서 SnapTray를 사용할 수 있나요?

예. CLI가 로컬 캡처와 IPC 기반 제어를 위한 공식 자동화 인터페이스입니다. 전체 명령 레퍼런스는 [CLI](/ko/docs/cli/)를 참조하세요.

## 현재 플랫폼 제한 사항은 무엇인가요?

- 다중 모니터 및 혼합 DPI 캡처는 더 많은 실제 기기 테스트가 필요합니다
- macOS/Windows에서 화면 녹화는 네이티브 API를 사용할 수 없을 때 Qt 캡처 엔진으로 폴백할 수 있으며, 이 경우 속도가 느릴 수 있습니다
- macOS에서 시스템 오디오 캡처에는 macOS 13+ 또는 BlackHole과 같은 가상 오디오 장치가 필요합니다
- Linux beta는 Ubuntu 22.04 X11 AppImage이며, 녹화와 OCR은 포함되지 않고 Wayland 세션은 지원되지 않습니다

## 버그는 어디에 보고하나요?

GitHub에 이슈를 열고 다음을 포함하세요.

- OS 버전
- SnapTray 버전
- 재현 단계
- 관련 스크린샷이나 녹화
