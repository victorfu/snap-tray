<p align="center">
  <img src="resources/icons/snaptray.png" alt="SnapTray logo" width="144" />
</p>

<h1 align="center">SnapTray</h1>

<p align="center">
  <a href="README.md">English</a> | <a href="README.zh-TW.md">繁體中文</a> | <a href="README.ja.md">日本語</a> | <strong>한국어</strong> | <a href="README.th.md">ไทย</a>
</p>

---

<p align="center">
  데스크톱을 벗어나지 않고 캡처, 주석, 고정까지. 녹화는 macOS/Windows에서 사용할 수 있습니다.
</p>

<p align="center">
  macOS 14+ · Windows 10+ · Ubuntu 22.04 X11 beta
</p>

<p align="center">
  <a href="https://github.com/victorfu/snap-tray/releases">다운로드</a> ·
  <a href="docs/ko/docs/index.md">문서</a> ·
  <a href="docs/ko/docs/tutorials/index.md">튜토리얼</a>
</p>

SnapTray는 macOS, Windows, Ubuntu 22.04 X11 beta를 지원하는 Qt 6 스크린샷 및 주석 앱입니다. 빠른 데스크톱 워크플로를 위해 만들어졌습니다. 영역을 캡처하고, 즉시 설명하고, 참조 화면을 계속 띄워 두세요. 녹화와 OCR은 macOS/Windows 전용이며 Linux beta에서는 숨겨지고 포함되지 않습니다.

## SnapTray를 선택하는 이유

- 확대경 선택, 창 감지, 다중 영역 캡처, 커서 포함, 색상 추출로 빠르게 캡처
- 화살표, 마커, 도형, 텍스트, 모자이크, 단계 배지, 이모지, QR 코드 스캔, 자동 블러로 즉시 주석 추가. OCR은 macOS/Windows 전용
- 스크린샷을 다른 창 위에 고정해 작업하는 동안에도 참조 화면을 계속 볼 수 있습니다
- macOS/Windows에서는 트레이 메뉴나 녹화 단축키로 전체 화면을 녹화합니다. 단일 디스플레이에서는 바로 시작하고, 다중 디스플레이에서는 화면을 선택합니다
- 전역 단축키, 트레이 메뉴, CLI에서 반복 작업 흐름을 실행합니다
- Linux beta: Ubuntu 22.04 X11 AppImage. 녹화와 OCR은 표시되지 않습니다.

## 실제 업무를 위한 설계

### 캡처와 마크업을 한 번에

`F2`를 누르고 영역을 드래그한 다음, 같은 도구 모음에서 복사, 저장, 고정, 블러를 실행하세요. macOS/Windows에서는 OCR도 함께 제공됩니다.

> **참고:** 이미지 공유(공유 가능한 URL로 업로드)는 현재 비활성화되어 있습니다. 공유 버튼은 캡처 및 핀 창 도구 모음에서 숨겨져 있어 UI에서 실행할 수 없습니다. 공유 관련 코드는 의도적으로 남겨 두었으므로, 이후 빌드에서 다시 구현하지 않고도 기능을 재활성화할 수 있습니다.

### 데스크톱에 직접 그리기

`Ctrl+F2 / Cmd+F2`로 스크린 캔버스를 열어 데모, 안내, 프레젠테이션, 실시간 설명에 활용하세요.

### 참조 화면을 필요한 자리에

고정한 이미지는 다른 앱 위에 유지되며 확대/축소, 불투명도, 회전, 뒤집기, 병합/레이아웃 제어, 인라인 주석을 지원합니다.

### 중요한 순간을 녹화

macOS/Windows에서는 트레이 메뉴나 녹화 단축키로 시작하고, 필요하면 화면을 선택한 뒤 플로팅 컨트롤 바에서 시간을 확인하고 녹화를 중지합니다.

## 더 알아보기

- 릴리스: [GitHub Releases](https://github.com/victorfu/snap-tray/releases)
- 사용자 문서: [문서 홈](docs/ko/docs/index.md)
- 튜토리얼: [튜토리얼 허브](docs/ko/docs/tutorials/index.md)
- CLI: [CLI 레퍼런스](docs/ko/docs/cli.md)
- 문제 해결: [문제 해결](docs/ko/docs/troubleshooting.md)

## 개발자용

SnapTray를 소스에서 빌드하거나 코드베이스에 참여하려면 [개발자 문서](docs/developer/index.md)(영문)에서 시작하세요.

빠른 시작 지점:

```bash
# macOS/Linux beta
./scripts/build.sh
./scripts/run-tests.sh
```

```batch
REM Windows
scripts\build.bat
scripts\run-tests.bat
```

추가 개발자 레퍼런스(영문):

- [소스에서 빌드](docs/developer/build-from-source.md)
- [릴리스 및 패키징](docs/developer/release-packaging.md)
- [아키텍처 개요](docs/developer/architecture.md)

## 라이선스

MIT License
