---
last_modified_at: 2026-06-13
layout: docs
title: CLI
seo_title: "SnapTray CLI: 스크린샷, 고정 및 설정 자동화"
description: 캡처, 고정 및 설정 워크플로를 스크립팅하기 위한 공식 자동화 인터페이스.
permalink: /ko/docs/cli/
lang: ko
route_key: docs_cli
doc_group: advanced
doc_order: 1
---

CLI는 SnapTray의 공식 자동화 인터페이스입니다. 로컬 캡처 워크플로를 스크립팅하고, IPC를 통해 실행 중인 앱을 제어하며, 셸이나 CI 작업에서 설정을 관리하는 데 사용합니다.

## CLI 헬퍼 설치

### macOS

설정 > 일반을 열고 CLI 설치 동작을 사용하여 `/usr/local/bin/snaptray`를 생성합니다. 관리자 권한이 필요합니다.

### Windows

설정 > 일반을 열고 CLI 설치 동작을 사용하여 SnapTray의 실행 파일 디렉토리를 현재 사용자의 `PATH`에 추가합니다. 설치 또는 제거 후에는 새 터미널을 여세요.

### Linux beta

설정 > 일반을 열고 CLI 설치 동작을 사용하여 AppImage용 `~/.local/bin/snaptray` 래퍼를 생성합니다. `~/.local/bin`이 `PATH`에 있는지 확인한 후 설치 또는 제거 후 새 터미널을 여세요.

### 패키지된 Windows 빌드

일부 설치 프로그램은 이미 `PATH` 또는 앱 실행 별칭을 통해 `snaptray`를 노출할 수 있지만, 인앱 설치 및 제거 흐름이 현재 애플리케이션 코드에서 설명하는 표준 동작입니다.

## 명령 매트릭스

| 명령 | 설명 | 메인 SnapTray 인스턴스 필요 |
|---|---|---|
| `full` | `-n`으로 선택한 화면 또는 커서 아래 전체 화면 캡처 | 아니요 |
| `screen` | 특정 화면 캡처, 또는 `--list`로 화면 목록 | 아니요 |
| `region` | 선택된 화면에서 `-r x,y,width,height` 영역 캡처 | 아니요 |
| `gui` | 영역 캡처 GUI 열기 | 예 |
| `canvas` | 스크린 캔버스 모드 전환 | 예 |
| `pin` | 이미지 파일 또는 클립보드 이미지 고정 | 예 |
| `config` | 설정 나열, 가져오기, 설정, 초기화; 옵션 없으면 설정 열기 | 부분 |

## 예제 명령

```bash
# 도움말 및 버전
snaptray --help
snaptray --version
snaptray full --help

# 로컬 캡처 명령
snaptray full                         # 커서 아래 화면 캡처
snaptray full -c                      # 전체 화면 캡처를 클립보드에 복사
snaptray full -d 1000 -o shot.png     # 1초 지연 후 저장
snaptray full -n 1 -o screen1.png     # 화면 1을 파일로 캡처
snaptray full --raw > shot.png        # PNG 바이트를 stdout에 출력
snaptray screen --list                # 사용 가능한 화면 목록
snaptray screen 0 -c                  # 화면 0 캡처 (위치 구문)
snaptray screen -n 1 -o screen1.png   # 화면 1 캡처 (옵션 구문)
snaptray screen 1 -o screen1.png      # 화면 1을 파일로 캡처
snaptray region -r 0,0,800,600 -c     # 화면 0의 영역을 클립보드에 캡처
snaptray region -n 1 -r 100,100,400,300 -o region.png
snaptray region -r 100,100,400,300 -o region.png

# IPC 명령
snaptray gui                          # 영역 선택기 열기
snaptray gui -d 2000                  # 2초 후 열기
snaptray canvas                       # 스크린 캔버스 전환
snaptray pin -f image.png             # 이미지 파일 고정
snaptray pin -c --center              # 클립보드 이미지를 가운데에 고정
snaptray pin -f image.png -x 200 -y 120
snaptray config                       # 설정 다이얼로그 열기

# 로컬 설정 명령
snaptray config --list
snaptray config --get hotkey
snaptray config --set files/filenamePrefix SnapTray
snaptray config --reset
```

## 동작 참고 사항

- 캡처 명령 (`full`, `screen`, `region`)은 기본적으로 PNG로 저장합니다.
- `--clipboard`는 저장 대신 복사합니다. `--raw`는 PNG 바이트를 stdout에 출력합니다.
- `--output`이 `--path`보다 우선합니다. 둘 다 제공하지 않으면 SnapTray가 설정된 스크린샷 디렉토리에 파일명을 생성합니다.
- `screen`은 `snaptray screen 1`과 `snaptray screen -n 1` 두 가지 구문을 모두 지원합니다.
- `region`은 `-r/--region`이 필요하며, 선택된 화면 기준의 논리 픽셀을 사용합니다. 직사각형은 해당 화면 내에 맞아야 합니다.
- `pin`은 `--file` 또는 `--clipboard` 중 하나만 필요합니다. `--file`은 읽을 수 있는 이미지여야 합니다. 사용자 지정 배치는 `-x`와 `-y` 둘 다 제공된 경우에만 적용되고, 그렇지 않으면 가운데에 고정됩니다.
- `config --set`은 단일 위치 값을 받습니다. `config --reset`은 전체 설정 저장소를 초기화합니다.

## 반환 코드

| 코드 | 의미 |
|---|---|
| `0` | 성공 |
| `1` | 일반 오류 |
| `2` | 잘못된 인수 |
| `3` | 파일 오류 |
| `4` | 인스턴스 오류 (메인 앱이 실행 중이 아님) |

## 관련 문서

- [시작하기](/ko/docs/getting-started/)
- [문제 해결](/ko/docs/troubleshooting/)
