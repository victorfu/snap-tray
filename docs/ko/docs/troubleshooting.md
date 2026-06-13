---
last_modified_at: 2026-06-13
layout: docs
title: 문제 해결
seo_title: "SnapTray 문제 해결: 권한, 단축키, 녹화 문제 해결"
description: 권한, 시작, Qt 배포, 인코딩, 단축키 문제와 macOS·Windows 녹화 문제를 해결합니다.
permalink: /ko/docs/troubleshooting/
lang: ko
route_key: docs_troubleshooting
doc_group: advanced
doc_order: 2
---

## 캡처가 시작되지 않음

### macOS

- `시스템 설정 > 개인 정보 보호 및 보안 > 화면 녹화`에서 화면 녹화 권한 확인
- `시스템 설정 > 개인 정보 보호 및 보안 > 접근성`에서 접근성 권한 확인
- 둘 중 하나의 권한을 변경한 후 SnapTray 재시작

### Windows

- 캡처가 실패하거나 영역 선택기가 나타나지 않으면 GPU 드라이버 업데이트
- 로컬 개발 빌드에 필요한 런타임 종속성이 배포되었는지 확인
- 오디오를 녹화하는 경우에만 마이크 권한 확인

## 트레이 아이콘이 나타나지 않음

- SnapTray 재실행
- OS 보안 프롬프트가 앱을 차단하지 않았는지 확인
- 로컬 개발 빌드를 실행 중이라면 플랫폼 빌드 스크립트를 다시 실행하고 종속성 확인

앱이 여전히 나타나지 않으면 다음으로 다시 빌드하세요.

- macOS/Linux beta: `./scripts/build.sh`
- Windows: `scripts\build.bat`

## macOS에서 Gatekeeper가 앱 실행을 차단함

공식 서명 및 공증된 릴리스는 Gatekeeper 경고 없이 실행되어야 합니다.

DMG를 수동으로 확인하려면:

```bash
spctl -a -vv -t open "dist/SnapTray-<version>-macOS.dmg"
```

로컬 임시 또는 개발 빌드에 한해 격리 속성을 제거할 수 있습니다.

```bash
xattr -cr /Applications/SnapTray.app
```

## Windows 앱에서 DLL 누락 또는 Qt 플러그인 오류 표시

`Qt6Core.dll을 찾을 수 없습니다` 또는 `Qt 플랫폼 플러그인을 초기화할 수 없습니다`와 같은 메시지가 나타나면 `windeployqt`로 Qt 종속성을 배포하세요.

```batch
C:\Qt\6.10.1\msvc2022_64\bin\windeployqt.exe build\bin\SnapTray-Debug.exe
```

`CMAKE_PREFIX_PATH`에 전달한 것과 동일한 Qt 설치 경로를 사용하세요.

## 단축키가 응답하지 않음

- 설정 > 단축키를 열고 동작이 여전히 바인딩되어 있는지 확인
- 동작을 다시 바인딩하고 즉시 테스트
- 다른 전역 단축키 유틸리티와의 충돌 확인
- Windows 11에서는 `Print Screen`을 SnapTray에 바인딩하기 전에 `설정 > 접근성 > 키보드 > 화면 캡처를 열기 위해 Print Screen 키 사용`을 끄세요
- 가장 자주 사용하는 동작은 단순한 단일 키 조합으로 유지하세요

### Linux beta: Wayland에서 앱 종료

Ubuntu 22.04 beta는 X11 세션만 지원합니다. 로그인 화면에서
SnapTray를 실행하기 전에 X11 세션을 선택하세요.

### Linux beta: 단축키가 등록되지 않음

전역 단축키는 X11 세션이 필요하며 데스크톱 환경 단축키와 충돌할 수 있습니다.
설정 > 단축키를 열어 실패한 단축키를 확인하고 다른 키 시퀀스를 할당하세요.

## 녹화 문제 (macOS 및 Windows 전용)

Linux beta는 녹화를 포함하지 않습니다. Linux beta 시작, 단축키 또는
Wayland 문제는 위의 Linux beta 참고 사항을 사용하세요.

- 프레임이 드롭되면 프레임 레이트 낮추기
- 긴 녹화에는 MP4 선호
- 선택한 오디오 소스와 장치 재확인
- macOS에서 시스템 오디오 캡처에는 macOS 13+ 또는 BlackHole과 같은 가상 오디오 장치가 필요합니다

## 빌드 또는 로컬 실행 오류

개발 환경에서는 저장소 스크립트로 전체 툴체인을 검증하세요.

- macOS/Linux beta: `./scripts/build.sh` 후 `./scripts/run-tests.sh`
- Windows: `scripts\build.bat` 후 `scripts\run-tests.bat`

패키징 또는 서명 문제를 디버깅 중이라면 [릴리스 및 패키징](/developer/release-packaging/)에서 계속하세요.
