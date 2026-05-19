# Ubuntu 22.04 X11 AppImage Beta Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build Ubuntu 22.04 X11 beta support with AppImage packaging while hiding recording and OCR on Linux.

**Architecture:** Add a central platform capability model and wire Linux-specific build, runtime, UI, CLI, and packaging behavior through it. Linux uses Qt/X11 capture and QHotkey for beta functionality, while OCR, recording, native window detection, and in-app updates are disabled or hidden through shared capability gates.

**Tech Stack:** C++17, Qt 6.10.1, CMake, Ninja, Qt Test, GitHub Actions, linuxdeploy AppImage tooling.

---

## Scope Check

This plan covers one feature slice: Ubuntu 22.04 X11 beta support. It touches platform, UI, CLI, packaging, CI, and docs, but all tasks serve one release target and are ordered so the project reaches a buildable/testable state before UI hiding and packaging work.

## File Structure

- Create `include/platform/PlatformCapabilities.h`: pure platform capability types and helper declarations.
- Create `src/platform/PlatformCapabilities.cpp`: pure capability decisions and Linux display-server detection.
- Modify `include/PlatformFeatures.h`: expose capabilities from the existing platform boundary.
- Modify `src/platform/PlatformFeatures_mac.mm`, `src/platform/PlatformFeatures_win.cpp`: initialize and return capabilities.
- Create `src/platform/PlatformFeatures_linux.cpp`: Linux platform feature implementation.
- Create `src/platform/WindowLevel_linux.cpp`: Linux no-op/native-safe window-level hooks.
- Create `src/OCRManager_linux.cpp`: Linux OCR unavailable implementation.
- Create `src/WindowDetector_linux.cpp`: Linux window detector disabled implementation.
- Create `src/AutoLaunchManager_linux.cpp`: XDG autostart implementation.
- Create `src/update/InstallSourceDetector_linux.cpp`: Linux install source detection.
- Create `src/update/UpdateServiceFactory_linux.cpp`: Linux update service factory returning unsupported behavior.
- Modify `CMakeLists.txt`: Linux source sets, QML `.mm` handling, install metadata.
- Modify `.github/workflows/ci.yml`: Ubuntu 22.04 build/test job.
- Modify `.github/workflows/release.yml`: AppImage release job.
- Modify `scripts/build.sh`, `scripts/run-tests.sh`: Linux-aware configure/test output.
- Create `packaging/linux/package-appimage.sh`: AppImage packaging script.
- Create `packaging/linux/SnapTray.desktop`: Linux desktop metadata.
- Modify `include/cli/CLICommand.h`, `src/cli/CLIHandler.cpp`, `src/cli/commands/RecordCommand.cpp`: capability-gated CLI help and unsupported direct record calls.
- Modify `include/qml/SettingsBackend.h`, `src/qml/SettingsBackend.cpp`: expose recording/OCR support and hide unsupported setting groups.
- Modify `src/qml/settings/SettingsSidebar.qml`, `src/qml/settings/SettingsWindow.qml`, `src/qml/settings/FilesSettings.qml`, `src/qml/settings/WatermarkSettings.qml`: Linux UI hiding.
- Modify `src/tools/ToolRegistry.cpp`: hide OCR tools on Linux through capabilities.
- Modify `src/MainApplication.cpp`, `include/MainApplication.h`: hide recording tray action and ignore direct record actions on unsupported platforms.
- Add tests under `tests/Platform`, `tests/CLI`, and update existing Settings/App/Tools/Update tests.
- Update docs under `docs/developer/`, `docs/docs/`, `docs/_data/`, and localized zh-TW developer docs.

## Task 1: Platform Capability Model

**Files:**
- Create: `include/platform/PlatformCapabilities.h`
- Create: `src/platform/PlatformCapabilities.cpp`
- Modify: `include/PlatformFeatures.h`
- Modify: `src/platform/PlatformFeatures_mac.mm`
- Modify: `src/platform/PlatformFeatures_win.cpp`
- Test: `tests/Platform/tst_PlatformCapabilities.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing capability tests**

Create `tests/Platform/tst_PlatformCapabilities.cpp`:

```cpp
#include <QtTest/QtTest>

#include "platform/PlatformCapabilities.h"

class tst_PlatformCapabilities : public QObject
{
    Q_OBJECT

private slots:
    void linuxX11BetaCapabilities();
    void linuxWaylandIsUnsupportedRuntime();
    void macAndWindowsKeepRecordingAndOcrSupport();
    void displayServerDetectionUsesSessionAndQtPlatform();
};

void tst_PlatformCapabilities::linuxX11BetaCapabilities()
{
    const auto caps = SnapTray::capabilitiesForPlatform(
        SnapTray::PlatformKind::Linux,
        SnapTray::DisplayServerKind::X11);

    QVERIFY(caps.isRuntimeSupported);
    QVERIFY(caps.supportsGlobalHotkeys);
    QVERIFY(!caps.supportsRecording);
    QVERIFY(!caps.supportsOCR);
    QVERIFY(!caps.supportsWindowDetection);
    QVERIFY(!caps.supportsInAppUpdates);
    QCOMPARE(caps.displayServer, SnapTray::DisplayServerKind::X11);
    QVERIFY(caps.unsupportedRuntimeMessage.isEmpty());
}

void tst_PlatformCapabilities::linuxWaylandIsUnsupportedRuntime()
{
    const auto caps = SnapTray::capabilitiesForPlatform(
        SnapTray::PlatformKind::Linux,
        SnapTray::DisplayServerKind::Wayland);

    QVERIFY(!caps.isRuntimeSupported);
    QVERIFY(caps.unsupportedRuntimeMessage.contains(QStringLiteral("X11")));
    QVERIFY(caps.unsupportedRuntimeMessage.contains(QStringLiteral("Ubuntu 22.04")));
}

void tst_PlatformCapabilities::macAndWindowsKeepRecordingAndOcrSupport()
{
    const auto macCaps = SnapTray::capabilitiesForPlatform(
        SnapTray::PlatformKind::MacOS,
        SnapTray::DisplayServerKind::Unknown);
    QVERIFY(macCaps.isRuntimeSupported);
    QVERIFY(macCaps.supportsRecording);
    QVERIFY(macCaps.supportsOCR);
    QVERIFY(macCaps.supportsWindowDetection);
    QVERIFY(macCaps.supportsInAppUpdates);

    const auto winCaps = SnapTray::capabilitiesForPlatform(
        SnapTray::PlatformKind::Windows,
        SnapTray::DisplayServerKind::Unknown);
    QVERIFY(winCaps.isRuntimeSupported);
    QVERIFY(winCaps.supportsRecording);
    QVERIFY(winCaps.supportsOCR);
    QVERIFY(winCaps.supportsWindowDetection);
    QVERIFY(winCaps.supportsInAppUpdates);
}

void tst_PlatformCapabilities::displayServerDetectionUsesSessionAndQtPlatform()
{
    QCOMPARE(SnapTray::displayServerKindFromSessionType(QStringLiteral("x11"), QString()),
             SnapTray::DisplayServerKind::X11);
    QCOMPARE(SnapTray::displayServerKindFromSessionType(QStringLiteral("wayland"), QString()),
             SnapTray::DisplayServerKind::Wayland);
    QCOMPARE(SnapTray::displayServerKindFromSessionType(QString(), QStringLiteral("xcb")),
             SnapTray::DisplayServerKind::X11);
    QCOMPARE(SnapTray::displayServerKindFromSessionType(QString(), QStringLiteral("wayland")),
             SnapTray::DisplayServerKind::Wayland);
    QCOMPARE(SnapTray::displayServerKindFromSessionType(QString(), QStringLiteral("offscreen")),
             SnapTray::DisplayServerKind::Offscreen);
}

QTEST_MAIN(tst_PlatformCapabilities)
#include "tst_PlatformCapabilities.moc"
```

- [ ] **Step 2: Register the test target**

Add near the other platform/QML policy tests in `tests/CMakeLists.txt`:

```cmake
add_executable(Platform_Capabilities Platform/tst_PlatformCapabilities.cpp)
target_link_libraries(Platform_Capabilities PRIVATE snaptray_core Qt6::Test)
add_test(NAME Platform_Capabilities COMMAND Platform_Capabilities)
set_tests_properties(Platform_Capabilities PROPERTIES TIMEOUT 60 LABELS "unit")
```

- [ ] **Step 3: Run the failing test**

Run:

```bash
./scripts/build.sh
cmake --build build --target Platform_Capabilities
ctest --test-dir build -R Platform_Capabilities --output-on-failure
```

Expected: configure or compile fails because `platform/PlatformCapabilities.h` does not exist.

- [ ] **Step 4: Add the capability API**

Create `include/platform/PlatformCapabilities.h`:

```cpp
#pragma once

#include <QString>

namespace SnapTray {

enum class PlatformKind {
    MacOS,
    Windows,
    Linux,
    Other
};

enum class DisplayServerKind {
    Unknown,
    X11,
    Wayland,
    Offscreen,
    Other
};

struct PlatformCapabilities {
    bool supportsRecording = false;
    bool supportsOCR = false;
    bool supportsGlobalHotkeys = false;
    bool supportsWindowDetection = false;
    bool supportsInAppUpdates = false;
    bool isRuntimeSupported = false;
    DisplayServerKind displayServer = DisplayServerKind::Unknown;
    QString unsupportedRuntimeMessage;
};

PlatformKind currentPlatformKind();
DisplayServerKind displayServerKindFromSessionType(const QString& sessionType,
                                                   const QString& qtPlatformName);
DisplayServerKind currentDisplayServerKind();
PlatformCapabilities capabilitiesForPlatform(PlatformKind platform,
                                             DisplayServerKind displayServer);
PlatformCapabilities currentPlatformCapabilities();

} // namespace SnapTray
```

Create `src/platform/PlatformCapabilities.cpp`:

```cpp
#include "platform/PlatformCapabilities.h"

#include <QByteArray>
#include <QGuiApplication>

namespace SnapTray {

PlatformKind currentPlatformKind()
{
#if defined(Q_OS_MACOS)
    return PlatformKind::MacOS;
#elif defined(Q_OS_WIN)
    return PlatformKind::Windows;
#elif defined(Q_OS_LINUX)
    return PlatformKind::Linux;
#else
    return PlatformKind::Other;
#endif
}

DisplayServerKind displayServerKindFromSessionType(const QString& sessionType,
                                                   const QString& qtPlatformName)
{
    const QString normalizedSession = sessionType.trimmed().toLower();
    const QString normalizedQtPlatform = qtPlatformName.trimmed().toLower();

    if (normalizedSession == QStringLiteral("x11") ||
        normalizedQtPlatform == QStringLiteral("xcb")) {
        return DisplayServerKind::X11;
    }

    if (normalizedSession == QStringLiteral("wayland") ||
        normalizedQtPlatform == QStringLiteral("wayland")) {
        return DisplayServerKind::Wayland;
    }

    if (normalizedQtPlatform == QStringLiteral("offscreen") ||
        normalizedQtPlatform == QStringLiteral("minimal")) {
        return DisplayServerKind::Offscreen;
    }

    if (normalizedSession.isEmpty() && normalizedQtPlatform.isEmpty()) {
        return DisplayServerKind::Unknown;
    }

    return DisplayServerKind::Other;
}

DisplayServerKind currentDisplayServerKind()
{
#if defined(Q_OS_LINUX)
    QString qtPlatformName;
    if (QGuiApplication::instance()) {
        qtPlatformName = QGuiApplication::platformName();
    }

    auto detected = displayServerKindFromSessionType(
        QString::fromLocal8Bit(qgetenv("XDG_SESSION_TYPE")),
        qtPlatformName);
    if (detected != DisplayServerKind::Unknown) {
        return detected;
    }

    if (!qEnvironmentVariableIsEmpty("DISPLAY")) {
        return DisplayServerKind::X11;
    }
    if (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
        return DisplayServerKind::Wayland;
    }
#endif
    return DisplayServerKind::Unknown;
}

PlatformCapabilities capabilitiesForPlatform(PlatformKind platform,
                                             DisplayServerKind displayServer)
{
    PlatformCapabilities caps;
    caps.displayServer = displayServer;

    switch (platform) {
    case PlatformKind::MacOS:
    case PlatformKind::Windows:
        caps.supportsRecording = true;
        caps.supportsOCR = true;
        caps.supportsGlobalHotkeys = true;
        caps.supportsWindowDetection = true;
        caps.supportsInAppUpdates = true;
        caps.isRuntimeSupported = true;
        return caps;
    case PlatformKind::Linux:
        caps.supportsRecording = false;
        caps.supportsOCR = false;
        caps.supportsGlobalHotkeys = displayServer == DisplayServerKind::X11;
        caps.supportsWindowDetection = false;
        caps.supportsInAppUpdates = false;
        caps.isRuntimeSupported = displayServer == DisplayServerKind::X11;
        if (!caps.isRuntimeSupported) {
            caps.unsupportedRuntimeMessage = QStringLiteral(
                "SnapTray Ubuntu 22.04 beta supports X11 sessions only.");
        }
        return caps;
    case PlatformKind::Other:
        caps.unsupportedRuntimeMessage = QStringLiteral(
            "SnapTray does not support this platform.");
        return caps;
    }

    caps.unsupportedRuntimeMessage = QStringLiteral(
        "SnapTray does not support this platform.");
    return caps;
}

PlatformCapabilities currentPlatformCapabilities()
{
    return capabilitiesForPlatform(currentPlatformKind(), currentDisplayServerKind());
}

} // namespace SnapTray
```

- [ ] **Step 5: Expose capabilities through `PlatformFeatures`**

In `include/PlatformFeatures.h`, add the include and methods/member:

```cpp
#include "platform/PlatformCapabilities.h"
```

Add to the public section:

```cpp
    const SnapTray::PlatformCapabilities& capabilities() const;
```

Add to the private section:

```cpp
    SnapTray::PlatformCapabilities m_capabilities;
```

In both `src/platform/PlatformFeatures_mac.mm` and `src/platform/PlatformFeatures_win.cpp`, update the constructor initializer list:

```cpp
PlatformFeatures::PlatformFeatures()
    : m_capabilities(SnapTray::currentPlatformCapabilities())
    , m_ocrAvailable(m_capabilities.supportsOCR && OCRManager::isAvailable())
    , m_windowDetectionAvailable(m_capabilities.supportsWindowDetection)
{
}
```

Add the method in both files:

```cpp
const SnapTray::PlatformCapabilities& PlatformFeatures::capabilities() const
{
    return m_capabilities;
}
```

- [ ] **Step 6: Run the capability test**

Run:

```bash
./scripts/build.sh
cmake --build build --target Platform_Capabilities
ctest --test-dir build -R Platform_Capabilities --output-on-failure
```

Expected: `100% tests passed` for `Platform_Capabilities`.

- [ ] **Step 7: Commit**

```bash
git add include/platform/PlatformCapabilities.h src/platform/PlatformCapabilities.cpp include/PlatformFeatures.h src/platform/PlatformFeatures_mac.mm src/platform/PlatformFeatures_win.cpp tests/Platform/tst_PlatformCapabilities.cpp tests/CMakeLists.txt
git commit -m "feat(platform): add capability model"
```

## Task 2: Linux Platform Implementations And Link Stubs

**Files:**
- Create: `src/platform/PlatformFeatures_linux.cpp`
- Create: `src/platform/WindowLevel_linux.cpp`
- Create: `src/OCRManager_linux.cpp`
- Create: `src/WindowDetector_linux.cpp`
- Create: `src/AutoLaunchManager_linux.cpp`
- Create: `src/update/InstallSourceDetector_linux.cpp`
- Create: `src/update/UpdateServiceFactory_linux.cpp`
- Modify: `include/update/InstallSourceDetector.h`
- Modify: `src/update/InstallSourceDetector.cpp`
- Test: `tests/Update/tst_InstallSourceDetector.cpp`
- Modify: `tests/Update/tst_UpdateCoordinator.cpp`

- [ ] **Step 1: Write failing update/source tests**

In `tests/Update/tst_InstallSourceDetector.cpp`, add private slots:

```cpp
    void testGetSourceDisplayName_AppImage();
    void testAppImageIsNotStoreInstallWhenForced();
```

Add implementations before `QTEST_MAIN`:

```cpp
void tst_InstallSourceDetector::testGetSourceDisplayName_AppImage()
{
    QCOMPARE(InstallSourceDetector::getSourceDisplayName(InstallSource::AppImage),
             QStringLiteral("AppImage"));
}

void tst_InstallSourceDetector::testAppImageIsNotStoreInstallWhenForced()
{
    InstallSourceDetector::setDetectedSourceForTests(InstallSource::AppImage);
    QVERIFY(!InstallSourceDetector::isStoreInstall());
    QVERIFY(InstallSourceDetector::getStoreName().isEmpty());
    InstallSourceDetector::clearDetectedSourceForTests();
}
```

In `tests/Update/tst_UpdateCoordinator.cpp`, add a private slot:

```cpp
    void testSelectServiceKind_AppImage_UsesExternalManaged();
```

Add implementation after the Homebrew test:

```cpp
void tst_UpdateCoordinator::testSelectServiceKind_AppImage_UsesExternalManaged()
{
    QCOMPARE(UpdateCoordinator::selectServiceKind(UpdatePlatform::Other,
                                                  InstallSource::AppImage),
             UpdateServiceKind::ExternalManaged);
}
```

- [ ] **Step 2: Run the failing tests**

Run:

```bash
./scripts/build.sh
cmake --build build --target Update_InstallSourceDetector Update_UpdateCoordinator
ctest --test-dir build -R "Update_(InstallSourceDetector|UpdateCoordinator)" --output-on-failure
```

Expected: compile fails because `InstallSource::AppImage` is not defined.

- [ ] **Step 3: Add AppImage install source**

In `include/update/InstallSourceDetector.h`, add `AppImage` before `Development`:

```cpp
    AppImage,         // Linux AppImage
```

In `src/update/InstallSourceDetector.cpp`, add to `getSourceDisplayName`:

```cpp
    case InstallSource::AppImage:
        return QStringLiteral("AppImage");
```

In `updateManagementMessageForSource` in `src/update/UpdateCoordinator.cpp`, add:

```cpp
    case InstallSource::AppImage:
        return QCoreApplication::translate(
            "UpdateCoordinator",
            "Updates for this Linux beta are provided as AppImage downloads.");
```

In `UpdateCoordinator::selectServiceKind`, add `InstallSource::AppImage` to the external-managed group:

```cpp
    case InstallSource::AppImage:
```

- [ ] **Step 4: Add Linux platform files**

Create `src/platform/PlatformFeatures_linux.cpp`:

```cpp
#include "PlatformFeatures.h"
#include "OCRManager.h"
#include "WindowDetector.h"

#include <QBuffer>
#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStandardPaths>
#include <QTextStream>

namespace {

QString cliScriptPath()
{
    const QString binDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
        + QStringLiteral("/.local/bin");
    return QDir(binDir).filePath(QStringLiteral("snaptray"));
}

QString shellQuote(const QString& value)
{
    QString escaped = value;
    escaped.replace('\'', QStringLiteral("'\"'\"'"));
    return QStringLiteral("'%1'").arg(escaped);
}

} // namespace

PlatformFeatures& PlatformFeatures::instance()
{
    static PlatformFeatures instance;
    return instance;
}

PlatformFeatures::PlatformFeatures()
    : m_capabilities(SnapTray::currentPlatformCapabilities())
    , m_ocrAvailable(false)
    , m_windowDetectionAvailable(false)
{
}

PlatformFeatures::~PlatformFeatures() = default;

const SnapTray::PlatformCapabilities& PlatformFeatures::capabilities() const
{
    return m_capabilities;
}

bool PlatformFeatures::isOCRAvailable() const
{
    return false;
}

OCRManager* PlatformFeatures::createOCRManager(QObject*) const
{
    return nullptr;
}

WindowDetector* PlatformFeatures::createWindowDetector(QObject*) const
{
    return nullptr;
}

QIcon PlatformFeatures::createTrayIcon() const
{
    const int size = 32;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    QPainterPath bgPath;
    bgPath.addRoundedRect(0, 0, size, size, size / 2, size / 2);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#6366F1")));
    painter.drawPath(bgPath);

    QPainterPath lightningPath;
    lightningPath.moveTo(19, 3);
    lightningPath.lineTo(8, 17);
    lightningPath.lineTo(15, 17);
    lightningPath.lineTo(13, 29);
    lightningPath.lineTo(24, 14);
    lightningPath.lineTo(17, 14);
    lightningPath.closeSubpath();

    painter.setPen(QPen(QColor(QStringLiteral("#FEF3C7")), 1));
    painter.setBrush(QColor(QStringLiteral("#FBBF24")));
    painter.drawPath(lightningPath);

    return QIcon(pixmap);
}

bool PlatformFeatures::copyImageToClipboardPersistently(const QImage& image) const
{
    return copyImageToClipboardForGui(image);
}

bool PlatformFeatures::copyImageToClipboardForGui(const QImage& image) const
{
    if (image.isNull()) {
        return false;
    }

    if (QClipboard* clipboard = QGuiApplication::clipboard()) {
        auto* mimeData = new QMimeData();
        QByteArray pngData;
        QBuffer buffer(&pngData);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        mimeData->setData(QStringLiteral("image/png"), pngData);
        mimeData->setImageData(image);
        clipboard->setMimeData(mimeData);
        return true;
    }
    return false;
}

QString PlatformFeatures::getAppExecutablePath() const
{
    return QCoreApplication::applicationFilePath();
}

bool PlatformFeatures::isCLIInstalled() const
{
    QFile file(cliScriptPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    return QString::fromUtf8(file.readAll()).contains(getAppExecutablePath());
}

bool PlatformFeatures::installCLI() const
{
    QFileInfo scriptInfo(cliScriptPath());
    QDir().mkpath(scriptInfo.absolutePath());

    QFile file(scriptInfo.absoluteFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }

    QTextStream stream(&file);
    stream << "#!/bin/sh\n";
    stream << "exec " << shellQuote(getAppExecutablePath()) << " \"$@\"\n";
    file.close();

    return QFile::setPermissions(
        scriptInfo.absoluteFilePath(),
        QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
            QFile::ReadGroup | QFile::ExeGroup |
            QFile::ReadOther | QFile::ExeOther);
}

bool PlatformFeatures::uninstallCLI() const
{
    return !QFileInfo::exists(cliScriptPath()) || QFile::remove(cliScriptPath());
}
```

Create `src/platform/WindowLevel_linux.cpp`:

```cpp
#include "WindowLevel.h"

void raiseWindowAboveMenuBar(QWidget *widget) { Q_UNUSED(widget); }
void setWindowClickThrough(QWidget *widget, bool enabled) { Q_UNUSED(widget); Q_UNUSED(enabled); }
void setWindowFloatingWithoutFocus(QWidget *widget) { Q_UNUSED(widget); }
void setWindowExcludedFromCapture(QWidget *widget, bool excluded) { Q_UNUSED(widget); Q_UNUSED(excluded); }
void setWindowExcludedFromCapture(QWindow *window, bool excluded) { Q_UNUSED(window); Q_UNUSED(excluded); }
void setWindowVisibleOnAllWorkspaces(QWidget *widget, bool enabled) { Q_UNUSED(widget); Q_UNUSED(enabled); }
void preventWindowHideOnDeactivate(QWidget *widget) { Q_UNUSED(widget); }
void forceNativeCursor(const QCursor&, QWidget *widget) { Q_UNUSED(widget); }
void raiseWindowAboveOverlays(QWidget *widget) { Q_UNUSED(widget); }
void raiseTransientWindowAboveParent(QWindow *window, QWidget *parentWidget) { Q_UNUSED(window); Q_UNUSED(parentWidget); }
void reinforceFramelessToolWindow(QWindow *window) { Q_UNUSED(window); }
void hideNativeWindowTitleBarIcon(QWindow *window) { Q_UNUSED(window); }
void hideNativeWindowTitleBarIcon(QWidget *widget) { Q_UNUSED(widget); }
```

Create `src/OCRManager_linux.cpp`:

```cpp
#include "OCRManager.h"

OCRManager::OCRManager(QObject* parent)
    : QObject(parent)
{
}

OCRManager::~OCRManager() = default;

QList<OCRLanguageInfo> OCRManager::availableLanguages()
{
    return {};
}

OCRLanguageQueryResult OCRManager::queryAvailableLanguages()
{
    return {};
}

void OCRManager::setRecognitionLanguages(const QStringList& languageCodes)
{
    m_languages = languageCodes;
}

QStringList OCRManager::recognitionLanguages() const
{
    return m_languages;
}

void OCRManager::recognizeText(const QPixmap&, const OCRCallback& callback)
{
    if (callback) {
        callback(OCRResult{});
    }
}

bool OCRManager::isAvailable()
{
    return false;
}

void OCRManager::beginShutdown()
{
    m_shuttingDown = true;
}
```

Create `src/WindowDetector_linux.cpp`:

```cpp
#include "WindowDetector.h"

#include <QScreen>

WindowDetector::WindowDetector(QObject* parent)
    : QObject(parent)
    , m_enabled(false)
{
}

WindowDetector::~WindowDetector() = default;

bool WindowDetector::hasAccessibilityPermission(bool)
{
    return false;
}

void WindowDetector::setScreen(QScreen* screen)
{
    m_currentScreen = screen;
}

void WindowDetector::setEnabled(bool enabled)
{
    m_enabled = false;
    Q_UNUSED(enabled);
}

bool WindowDetector::isEnabled() const
{
    return false;
}

void WindowDetector::refreshWindowList(QueryMode)
{
    m_cacheReady = true;
    m_refreshComplete.store(true);
    emit windowListReady();
}

void WindowDetector::refreshWindowListAsync(QueryMode queryMode)
{
    refreshWindowList(queryMode);
}

bool WindowDetector::isRefreshComplete() const
{
    return true;
}

bool WindowDetector::isWindowCacheReady(QueryMode) const
{
    return true;
}

std::optional<DetectedElement> WindowDetector::detectWindowAt(const QPoint&, QueryMode) const
{
    return std::nullopt;
}

DetectionFlags WindowDetector::detectionFlags() const
{
    return DetectionFlag::None;
}

std::optional<DetectedElement> WindowDetector::detectChildElementAt(
    const QPoint&,
    const DetectedElement&,
    size_t) const
{
    return std::nullopt;
}

void WindowDetector::enumerateWindows()
{
}

void WindowDetector::enumerateWindowsInternal(std::vector<DetectedElement>& cache,
                                              qreal,
                                              DetectionFlags)
{
    cache.clear();
}
```

Create `src/AutoLaunchManager_linux.cpp`:

```cpp
#include "AutoLaunchManager.h"

#include "settings/AutoLaunchSettingsManager.h"
#include "settings/AutoLaunchSyncPolicy.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

namespace {

QString desktopFilePath()
{
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return QDir(configDir).filePath(QStringLiteral("autostart/SnapTray.desktop"));
}

QString currentExecutablePath()
{
    return QCoreApplication::applicationFilePath();
}

bool desktopEntryExists()
{
    return QFileInfo::exists(desktopFilePath());
}

SnapTray::AutoLaunchSyncState startupEntryState()
{
    return desktopEntryExists()
        ? SnapTray::AutoLaunchSyncState::EnabledCurrentCanonical
        : SnapTray::AutoLaunchSyncState::Disabled;
}

} // namespace

bool AutoLaunchManager::isEnabled()
{
    return desktopEntryExists();
}

bool AutoLaunchManager::setEnabled(bool enabled)
{
    const QString path = desktopFilePath();
    if (!enabled) {
        return !QFileInfo::exists(path) || QFile::remove(path);
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }

    QTextStream out(&file);
    out << "[Desktop Entry]\n";
    out << "Type=Application\n";
    out << "Name=SnapTray\n";
    out << "Exec=" << currentExecutablePath() << " --minimized\n";
    out << "Icon=snaptray\n";
    out << "Terminal=false\n";
    out << "Categories=Utility;Graphics;\n";
    return true;
}

bool AutoLaunchManager::syncWithPreference()
{
    auto& settingsManager = SnapTray::AutoLaunchSettingsManager::instance();
    const std::optional<bool> preferredEnabled = settingsManager.loadPreferredEnabled();
    const SnapTray::AutoLaunchSyncPlan plan =
        SnapTray::buildAutoLaunchStartupSyncPlan(preferredEnabled, startupEntryState());

    if (plan.shouldApplyChange && !setEnabled(plan.targetEnabled)) {
        return isEnabled();
    }

    return isEnabled();
}
```

Create `src/update/InstallSourceDetector_linux.cpp`:

```cpp
#include "update/InstallSourceDetector.h"

#include <QCoreApplication>

InstallSource InstallSourceDetector::detect()
{
    if (s_detected) {
        return s_cachedSource;
    }

    if (isDevelopmentBuild()) {
        s_cachedSource = InstallSource::Development;
    } else if (qEnvironmentVariableIsSet("APPIMAGE")) {
        s_cachedSource = InstallSource::AppImage;
    } else {
        s_cachedSource = InstallSource::DirectDownload;
    }

    s_detected = true;
    return s_cachedSource;
}
```

Create `src/update/UpdateServiceFactory_linux.cpp`:

```cpp
#include "update/UpdateServiceFactory.h"

std::unique_ptr<IUpdateService> createPlatformUpdateService(UpdateServiceKind,
                                                            InstallSource)
{
    return nullptr;
}
```

- [ ] **Step 5: Run update tests**

Run:

```bash
./scripts/build.sh
cmake --build build --target Update_InstallSourceDetector Update_UpdateCoordinator
ctest --test-dir build -R "Update_(InstallSourceDetector|UpdateCoordinator)" --output-on-failure
```

Expected: both tests pass on the current platform.

- [ ] **Step 6: Commit**

```bash
git add include/update/InstallSourceDetector.h src/update/InstallSourceDetector.cpp src/update/UpdateCoordinator.cpp src/platform/PlatformFeatures_linux.cpp src/platform/WindowLevel_linux.cpp src/OCRManager_linux.cpp src/WindowDetector_linux.cpp src/AutoLaunchManager_linux.cpp src/update/InstallSourceDetector_linux.cpp src/update/UpdateServiceFactory_linux.cpp tests/Update/tst_InstallSourceDetector.cpp tests/Update/tst_UpdateCoordinator.cpp
git commit -m "feat(linux): add platform stubs"
```

## Task 3: CMake Linux Source Set

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Configure on Linux and capture the expected failure**

On Ubuntu 22.04 X11 or CI container:

```bash
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR"
```

Expected before this task: configure or build fails due missing Linux platform source definitions or Objective-C++ `.mm` handling.

- [ ] **Step 2: Add Linux platform sources**

In the `snaptray_core` source list in `CMakeLists.txt`, add the common capability source near the other cross-cutting utilities:

```cmake
    src/platform/PlatformCapabilities.cpp
```

Add Linux platform sources next to the Windows/macOS source groups:

```cmake
    $<$<PLATFORM_ID:Linux>:
        src/platform/WindowLevel_linux.cpp
        src/platform/PlatformFeatures_linux.cpp
        src/WindowDetector_linux.cpp
        src/OCRManager_linux.cpp
        src/AutoLaunchManager_linux.cpp
    >
```

In the `snaptray_ui` source list near update and platform-specific update sources, add:

```cmake
    $<$<PLATFORM_ID:Linux>:
        src/update/InstallSourceDetector_linux.cpp
        src/update/UpdateServiceFactory_linux.cpp
    >
```

- [ ] **Step 3: Make QML native bridge sources compile on Linux**

Above `qt_add_qml_module(snaptray_qml ...)`, define the source variable:

```cmake
set(SNAPTRAY_QML_NATIVE_SOURCES
    src/qml/QmlOverlayManager.mm
    src/qml/QmlRecordingBoundary.mm
    src/qml/QmlRecordingControlBar.mm
    src/qml/QmlCountdownOverlay.mm
    src/qml/RecordingPreviewBackend.mm
    src/qml/QmlWindowedToolbar.mm
    src/qml/QmlFloatingToolbar.mm
    src/qml/QmlFloatingSubToolbar.mm
    src/qml/QmlDialog.mm
    src/qml/QmlOverlayPanel.mm
)

if(NOT APPLE)
    set_source_files_properties(${SNAPTRAY_QML_NATIVE_SOURCES} PROPERTIES LANGUAGE CXX)
endif()
```

Then replace the repeated `.mm` entries inside `qt_add_qml_module(... SOURCES ...)` with:

```cmake
        ${SNAPTRAY_QML_NATIVE_SOURCES}
```

Keep every corresponding header entry unchanged.

- [ ] **Step 4: Keep native-only tests guarded and common tests enabled**

Verify `tests/CMakeLists.txt` already guards Windows and macOS native tests with `if(WIN32)` or `if(APPLE)`. Keep common recording manager tests enabled on Linux; the Linux encoder/audio stubs added in Task 2 must make them link and exercise unsupported-platform behavior instead of excluding them.

- [ ] **Step 5: Build on Ubuntu 22.04**

Run:

```bash
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR"
cmake --build build-linux --target SnapTray Platform_Capabilities --parallel
```

Expected: `SnapTray` and `Platform_Capabilities` build without Objective-C++ compiler errors.

- [ ] **Step 6: Build on macOS/Windows**

Run on the current platform:

```bash
./scripts/build.sh
```

Expected: the existing platform still builds.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt tests/CMakeLists.txt
git commit -m "build: add linux source set"
```

## Task 4: CLI Capability Gating

**Files:**
- Modify: `include/cli/CLICommand.h`
- Modify: `src/cli/CLIHandler.cpp`
- Modify: `src/cli/commands/RecordCommand.cpp`
- Test: `tests/CLI/tst_CLIHandlerFeatureGating.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing CLI tests**

Create `tests/CLI/tst_CLIHandlerFeatureGating.cpp`:

```cpp
#include <QtTest/QtTest>

#include "cli/CLIHandler.h"
#include "cli/CLIResult.h"

class tst_CLIHandlerFeatureGating : public QObject
{
    Q_OBJECT

private slots:
    void helpHidesRecordWhenRecordingUnsupported();
    void directRecordInvocationFailsWhenRecordingUnsupported();
};

void tst_CLIHandlerFeatureGating::helpHidesRecordWhenRecordingUnsupported()
{
    SnapTray::CLI::CLIHandler handler;
    const QString help = handler.getHelpText();

#if defined(Q_OS_LINUX)
    QVERIFY(!help.contains(QStringLiteral("record")));
    QVERIFY(!help.contains(QStringLiteral("Record screen")));
#else
    QVERIFY(help.contains(QStringLiteral("record")));
#endif
}

void tst_CLIHandlerFeatureGating::directRecordInvocationFailsWhenRecordingUnsupported()
{
    SnapTray::CLI::CLIHandler handler;
    const auto result = handler.process({QStringLiteral("snaptray"), QStringLiteral("record")});

#if defined(Q_OS_LINUX)
    QVERIFY(!result.isSuccess());
    QCOMPARE(result.code, SnapTray::CLI::CLIResult::Code::RecordingError);
    QVERIFY(result.message.contains(QStringLiteral("Linux beta")));
    QVERIFY(result.message.contains(QStringLiteral("recording")));
#else
    QVERIFY(!result.isSuccess());
    QCOMPARE(result.code, SnapTray::CLI::CLIResult::Code::InstanceError);
#endif
}

QTEST_MAIN(tst_CLIHandlerFeatureGating)
#include "tst_CLIHandlerFeatureGating.moc"
```

Add to `tests/CMakeLists.txt` near CLI tests:

```cmake
add_executable(CLI_FeatureGating CLI/tst_CLIHandlerFeatureGating.cpp)
target_link_libraries(CLI_FeatureGating PRIVATE snaptray_platform Qt6::Test)
add_test(NAME CLI_FeatureGating COMMAND CLI_FeatureGating)
set_tests_properties(CLI_FeatureGating PROPERTIES TIMEOUT 60 LABELS "unit")
```

- [ ] **Step 2: Run the failing CLI tests**

Run:

```bash
./scripts/build.sh
cmake --build build --target CLI_FeatureGating
ctest --test-dir build -R CLI_FeatureGating --output-on-failure
```

Expected on Linux: help still lists `record` and direct invocation tries IPC. On macOS/Windows this test should already pass after registration.

- [ ] **Step 3: Add command support gating**

In `include/cli/CLICommand.h`, add a virtual method to `CLICommand`:

```cpp
    virtual bool isSupported() const { return true; }
    virtual QString unsupportedMessage() const { return QString(); }
```

In `src/cli/commands/RecordCommand.cpp`, include capabilities:

```cpp
#include "platform/PlatformCapabilities.h"
```

Add methods to `RecordCommand` declaration in `include/cli/commands/RecordCommand.h`:

```cpp
    bool isSupported() const override;
    QString unsupportedMessage() const override;
```

Add implementations:

```cpp
bool RecordCommand::isSupported() const
{
    return SnapTray::currentPlatformCapabilities().supportsRecording;
}

QString RecordCommand::unsupportedMessage() const
{
    return QStringLiteral("SnapTray Linux beta does not support recording.");
}
```

In `src/cli/CLIHandler.cpp`, update help listing loop:

```cpp
    for (const QString& name : names) {
        const auto& cmd = m_commands.at(name);
        if (!cmd->isSupported()) {
            continue;
        }
        out << QString("  %1  %2\n").arg(name, -10).arg(cmd->description());
    }
```

In `CLIHandler::process`, after finding the command:

```cpp
    if (!command->isSupported()) {
        return CLIResult::error(CLIResult::Code::RecordingError,
                                command->unsupportedMessage());
    }
```

- [ ] **Step 4: Preserve metadata CLI without X11**

Before creating `QApplication` in `src/main.cpp`, keep `--help` and `--version` as metadata commands that do not require a supported display server. This enables AppImage smoke checks:

```cpp
    if (arguments.size() >= 2 &&
        (arguments.at(1) == QStringLiteral("--help") ||
         arguments.at(1) == QStringLiteral("-h") ||
         arguments.at(1) == QStringLiteral("--version") ||
         arguments.at(1) == QStringLiteral("-v"))) {
        QCoreApplication app(argc, argv);
        SnapTray::CLI::CLIHandler cliHandler;
        auto result = cliHandler.process(arguments);
        QTextStream out(stdout);
        QTextStream err(stderr);
        if (result.isSuccess()) {
            out << result.message << "\n";
        } else {
            err << "Error: " << result.message << "\n";
        }
        return static_cast<int>(result.code);
    }
```

- [ ] **Step 5: Run CLI tests**

Run:

```bash
./scripts/build.sh
cmake --build build --target CLI_FeatureGating
ctest --test-dir build -R CLI_FeatureGating --output-on-failure
```

Expected: tests pass.

- [ ] **Step 6: Commit**

```bash
git add include/cli/CLICommand.h include/cli/commands/RecordCommand.h src/cli/CLIHandler.cpp src/cli/commands/RecordCommand.cpp src/main.cpp tests/CLI/tst_CLIHandlerFeatureGating.cpp tests/CMakeLists.txt
git commit -m "feat(cli): gate unsupported linux recording"
```

## Task 5: Hide Recording And OCR In Settings

**Files:**
- Modify: `include/qml/SettingsBackend.h`
- Modify: `src/qml/SettingsBackend.cpp`
- Modify: `src/qml/settings/SettingsSidebar.qml`
- Modify: `src/qml/settings/SettingsWindow.qml`
- Modify: `src/qml/settings/FilesSettings.qml`
- Modify: `src/qml/settings/WatermarkSettings.qml`
- Test: `tests/Settings/tst_SettingsBackend.cpp`
- Test: `tests/Qml/tst_SettingsWindowFeatureGating.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing settings backend tests**

In `tests/Settings/tst_SettingsBackend.cpp`, add private slots:

```cpp
    void testFeatureSupportPropertiesFollowPlatformCapabilities();
    void testHotkeyCategoriesHideRecordingWhenUnsupported();
```

Add implementations before `QTEST_MAIN`:

```cpp
void tst_SettingsBackend::testFeatureSupportPropertiesFollowPlatformCapabilities()
{
    SettingsBackend backend;

#if defined(Q_OS_LINUX)
    QVERIFY(!backend.recordingSupported());
    QVERIFY(!backend.ocrSettingsVisible());
#else
    QVERIFY(backend.recordingSupported());
    QVERIFY(backend.ocrSettingsVisible());
#endif
}

void tst_SettingsBackend::testHotkeyCategoriesHideRecordingWhenUnsupported()
{
    SettingsBackend backend;
    const QVariantList categories = backend.hotkeyCategories();

    bool sawRecording = false;
    for (const QVariant& item : categories) {
        const auto map = item.toMap();
        if (map.value(QStringLiteral("category")).toInt() ==
            static_cast<int>(SnapTray::HotkeyCategory::Recording)) {
            sawRecording = true;
        }
    }

#if defined(Q_OS_LINUX)
    QVERIFY(!sawRecording);
#else
    QVERIFY(sawRecording);
#endif
}
```

- [ ] **Step 2: Add QML navigation test**

Create `tests/Qml/tst_SettingsWindowFeatureGating.cpp`:

```cpp
#include <QtTest/QtTest>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>

#include <memory>

#include "qml/SettingsBackend.h"

class tst_SettingsWindowFeatureGating : public QObject
{
    Q_OBJECT

private slots:
    void sidebarModelHidesUnsupportedPages();
};

void tst_SettingsWindowFeatureGating::sidebarModelHidesUnsupportedPages()
{
    QQmlEngine engine;
    SnapTray::SettingsBackend backend;
    engine.rootContext()->setContextProperty(QStringLiteral("settingsBackend"), &backend);

    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/SnapTrayQml/settings/SettingsSidebar.qml")));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root != nullptr, qPrintable(component.errorString()));

    const QVariant pages = root->property("pages");
    QVERIFY(pages.isValid());
    const QVariantList pageList = pages.toList();

    QStringList keys;
    for (const QVariant& item : pageList) {
        keys.append(item.toMap().value(QStringLiteral("key")).toString());
    }

#if defined(Q_OS_LINUX)
    QVERIFY(!keys.contains(QStringLiteral("ocr")));
    QVERIFY(!keys.contains(QStringLiteral("recording")));
#else
    QVERIFY(keys.contains(QStringLiteral("ocr")));
    QVERIFY(keys.contains(QStringLiteral("recording")));
#endif
}

QTEST_MAIN(tst_SettingsWindowFeatureGating)
#include "tst_SettingsWindowFeatureGating.moc"
```

Add to `tests/CMakeLists.txt`:

```cmake
add_executable(Qml_SettingsWindowFeatureGating Qml/tst_SettingsWindowFeatureGating.cpp)
target_link_libraries(Qml_SettingsWindowFeatureGating PRIVATE snaptray_ui snaptray_qmlplugin Qt6::Qml Qt6::Quick Qt6::Test)
add_test(NAME Qml_SettingsWindowFeatureGating COMMAND Qml_SettingsWindowFeatureGating)
set_tests_properties(Qml_SettingsWindowFeatureGating PROPERTIES TIMEOUT 60 LABELS "unit")
```

- [ ] **Step 3: Run failing tests**

Run:

```bash
./scripts/build.sh
cmake --build build --target Settings_SettingsBackend Qml_SettingsWindowFeatureGating
ctest --test-dir build -R "Settings_SettingsBackend|Qml_SettingsWindowFeatureGating" --output-on-failure
```

Expected: compile fails because `recordingSupported` does not exist and QML `pages` property does not exist.

- [ ] **Step 4: Add support properties**

In `include/qml/SettingsBackend.h`, add:

```cpp
    Q_PROPERTY(bool recordingSupported READ recordingSupported CONSTANT)
    Q_PROPERTY(bool ocrSettingsVisible READ ocrSettingsVisible CONSTANT)
```

Add public getter:

```cpp
    bool recordingSupported() const;
    bool ocrSettingsVisible() const;
```

In `src/qml/SettingsBackend.cpp`, add:

```cpp
bool SettingsBackend::recordingSupported() const
{
    return PlatformFeatures::instance().capabilities().supportsRecording;
}

bool SettingsBackend::ocrSettingsVisible() const
{
    return PlatformFeatures::instance().capabilities().supportsOCR;
}
```

Update `SettingsBackend::ocrSupported()`:

```cpp
bool SettingsBackend::ocrSupported() const
{
    return PlatformFeatures::instance().capabilities().supportsOCR
        && PlatformFeatures::instance().isOCRAvailable();
}
```

Update `SettingsBackend::hotkeyCategories()`:

```cpp
    for (auto cat : categories) {
        if (cat == HotkeyCategory::Recording && !recordingSupported()) {
            continue;
        }
        QVariantMap entry;
        entry[QStringLiteral("category")] = static_cast<int>(cat);
        entry[QStringLiteral("name")] = getCategoryDisplayName(cat);
        result.append(entry);
    }
```

- [ ] **Step 5: Make settings navigation data-driven**

In `src/qml/settings/SettingsSidebar.qml`, add:

```qml
    property var pages: buildPages()
    readonly property string currentKey: pages.length > 0 && currentIndex >= 0 && currentIndex < pages.length
        ? pages[currentIndex].key
        : "general"

    function buildPages() {
        var result = [
            { key: "general", label: qsTr("General") },
            { key: "hotkeys", label: qsTr("Hotkeys") },
            { key: "advanced", label: qsTr("Advanced") },
            { key: "watermark", label: qsTr("Watermark") }
        ]
        if (settingsBackend.ocrSettingsVisible) {
            result.push({ key: "ocr", label: qsTr("OCR") })
        }
        if (settingsBackend.recordingSupported) {
            result.push({ key: "recording", label: qsTr("Recording") })
        }
        result.push({ key: "files", label: qsTr("Files") })
        result.push({ key: "updates", label: qsTr("Updates") })
        result.push({ key: "about", label: qsTr("About") })
        return result
    }
```

Replace the current array model with:

```qml
        model: root.pages
```

In the delegate, replace `required property string modelData` with:

```qml
            required property var modelData
            readonly property string label: modelData.label
```

Replace `text: navItem.modelData` and accessibility name references with `navItem.label`.

In `src/qml/settings/SettingsWindow.qml`, add:

```qml
    function stackIndexForKey(key) {
        var keys = ["general", "hotkeys", "advanced", "watermark", "ocr", "recording", "files", "updates", "about"]
        for (var i = 0; i < keys.length; i++) {
            if (keys[i] === key) return i
        }
        return 0
    }
```

Update `StackLayout.currentIndex`:

```qml
            currentIndex: root.stackIndexForKey(sidebar.currentKey)
```

Update each loader `active` condition to use `sidebar.currentKey`. Example:

```qml
                active: sidebar.currentKey === "general"
                    || status === Loader.Ready
                    || status === Loader.Loading
```

Set unsupported page loaders inactive:

```qml
                active: settingsBackend.ocrSettingsVisible
                    && (sidebar.currentKey === "ocr"
                    || status === Loader.Ready
                    || status === Loader.Loading)
```

Use the same pattern for `recording`.

- [ ] **Step 6: Hide recording-specific file/watermark rows**

In `src/qml/settings/FilesSettings.qml`, set these controls visible only when recording is supported:

```qml
        SettingsPathPicker {
            label: qsTr("Recordings")
            visible: settingsBackend.recordingSupported
            path: settingsBackend.recordingPath
            onBrowseClicked: settingsBackend.browseRecordingPath()
        }
```

```qml
        SettingsToggle {
            label: qsTr("Auto-save recordings")
            visible: settingsBackend.recordingSupported
            checked: settingsBackend.autoSaveRecordings
            onToggled: function(checked) { settingsBackend.autoSaveRecordings = checked }
        }
```

In `src/qml/settings/WatermarkSettings.qml`, hide the recording toggle:

```qml
            visible: settingsBackend.recordingSupported
```

- [ ] **Step 7: Run settings tests**

Run:

```bash
./scripts/build.sh
cmake --build build --target Settings_SettingsBackend Qml_SettingsWindowFeatureGating
ctest --test-dir build -R "Settings_SettingsBackend|Qml_SettingsWindowFeatureGating" --output-on-failure
```

Expected: tests pass.

- [ ] **Step 8: Commit**

```bash
git add include/qml/SettingsBackend.h src/qml/SettingsBackend.cpp src/qml/settings/SettingsSidebar.qml src/qml/settings/SettingsWindow.qml src/qml/settings/FilesSettings.qml src/qml/settings/WatermarkSettings.qml tests/Settings/tst_SettingsBackend.cpp tests/Qml/tst_SettingsWindowFeatureGating.cpp tests/CMakeLists.txt
git commit -m "feat(settings): hide unsupported linux pages"
```

## Task 6: Hide OCR Tool Actions On Linux

**Files:**
- Modify: `src/tools/ToolRegistry.cpp`
- Test: `tests/Tools/tst_ToolRegistry.cpp`

- [ ] **Step 1: Write failing tool tests**

In `tests/Tools/tst_ToolRegistry.cpp`, add private slot:

```cpp
    void testGetToolsForToolbar_HidesOcrWhenUnsupported();
```

Update existing toolbar assertions that currently require OCR. In
`testGetToolsForToolbar_RegionSelector`, replace the OCR assertion with:

```cpp
#if defined(Q_OS_LINUX)
    QVERIFY(!tools.contains(ToolId::OCR));
#else
    QVERIFY(tools.contains(ToolId::OCR));
#endif
```

In `testGetToolsForToolbar_PinWindow`, replace the OCR assertion with the same
conditional block.

Add implementation after PinWindow toolbar test:

```cpp
void TestToolRegistry::testGetToolsForToolbar_HidesOcrWhenUnsupported()
{
    const QVector<ToolId> regionTools = registry().getToolsForToolbar(ToolbarType::RegionSelector);
    const QVector<ToolId> pinTools = registry().getToolsForToolbar(ToolbarType::PinWindow);

#if defined(Q_OS_LINUX)
    QVERIFY(!regionTools.contains(ToolId::OCR));
    QVERIFY(!pinTools.contains(ToolId::OCR));
#else
    QVERIFY(regionTools.contains(ToolId::OCR));
    QVERIFY(pinTools.contains(ToolId::OCR));
#endif
}
```

- [ ] **Step 2: Run failing test**

Run:

```bash
./scripts/build.sh
cmake --build build --target Tools_ToolRegistry
ctest --test-dir build -R Tools_ToolRegistry --output-on-failure
```

Expected on Linux: OCR is still present.

- [ ] **Step 3: Filter OCR by capabilities**

In `src/tools/ToolRegistry.cpp`, include capabilities:

```cpp
#include "platform/PlatformCapabilities.h"
```

Add helper near the top:

```cpp
namespace {
bool isToolSupportedOnCurrentPlatform(ToolId id)
{
    const auto caps = SnapTray::currentPlatformCapabilities();
    if (id == ToolId::OCR) {
        return caps.supportsOCR;
    }
    return true;
}
}
```

At the end of `ToolRegistry::getToolsForToolbar`, before return:

```cpp
    tools.erase(std::remove_if(tools.begin(), tools.end(), [](ToolId id) {
        return !isToolSupportedOnCurrentPlatform(id);
    }), tools.end());
```

Add `<algorithm>` include if needed:

```cpp
#include <algorithm>
```

- [ ] **Step 4: Run tool tests**

Run:

```bash
./scripts/build.sh
cmake --build build --target Tools_ToolRegistry
ctest --test-dir build -R Tools_ToolRegistry --output-on-failure
```

Expected: tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/tools/ToolRegistry.cpp tests/Tools/tst_ToolRegistry.cpp
git commit -m "feat(tools): hide ocr on unsupported platforms"
```

## Task 7: Hide Recording Tray Entry And Runtime Paths

**Files:**
- Modify: `src/MainApplication.cpp`
- Modify: `include/MainApplication.h`
- Test: `tests/App/tst_MainApplicationTrayMenu.cpp`

- [ ] **Step 1: Write failing tray tests**

In `tests/App/tst_MainApplicationTrayMenu.cpp`, add private slot:

```cpp
    void initialize_hidesRecordingActionWhenUnsupported();
```

Add implementation after update action tests:

```cpp
void tst_MainApplicationTrayMenu::initialize_hidesRecordingActionWhenUnsupported()
{
    installFakeUpdateService(InstallSource::DirectDownload, false);

    MainApplication application;
    application.initialize();

#if defined(Q_OS_LINUX)
    QVERIFY(application.m_fullScreenRecordingAction == nullptr);
    const QList<QAction*> actions = application.m_trayMenu->actions();
    for (QAction* action : actions) {
        if (action) {
            QVERIFY(action->text() != MainApplication::tr("Record Screen"));
        }
    }
#else
    QVERIFY(application.m_fullScreenRecordingAction != nullptr);
#endif
}
```

Update existing record command tests for Linux. In
`handleCLICommand_recordToggle_closesScreenPicker`, wrap the final assertions:

```cpp
#if defined(Q_OS_LINUX)
    QVERIFY(application.m_screenPickerDialog != nullptr);
#else
    QVERIFY(application.m_screenPickerDialog.isNull());
#endif
    QCOMPARE(application.m_recordingManager->state(), RecordingManager::State::Idle);
```

Keep `handleCLICommand_recordStart_doesNothingWhilePickerOpen` unchanged because
Linux also leaves the picker open.

- [ ] **Step 2: Run failing tray test**

Run:

```bash
./scripts/build.sh
cmake --build build --target App_MainApplicationTrayMenu
ctest --test-dir build -R App_MainApplicationTrayMenu --output-on-failure
```

Expected on Linux: record action still exists.

- [ ] **Step 3: Gate recording action creation**

In `src/MainApplication.cpp`, include capabilities through `PlatformFeatures` already present.

Replace unconditional recording action creation with:

```cpp
    if (PlatformFeatures::instance().capabilities().supportsRecording) {
        m_fullScreenRecordingAction = m_trayMenu->addAction(tr("Record Screen"));
        connect(m_fullScreenRecordingAction, &QAction::triggered, this, &MainApplication::onFullScreenRecording);
    }
```

In `MainApplication::onFullScreenRecording`, add first:

```cpp
    if (!PlatformFeatures::instance().capabilities().supportsRecording) {
        return;
    }
```

In `MainApplication::handleCLICommand`, inside the `record` branch before reading action:

```cpp
        if (!PlatformFeatures::instance().capabilities().supportsRecording) {
            qWarning() << "Ignoring record command on unsupported platform";
            return;
        }
```

In `MainApplication::onHotkeyAction`, gate `RecordFullScreen`:

```cpp
    case HotkeyAction::RecordFullScreen:
        if (PlatformFeatures::instance().capabilities().supportsRecording) {
            onFullScreenRecording();
        }
        break;
```

In `updateRecordingActionText()` and `updateTrayMenuHotkeyText()`, keep null guards:

```cpp
    if (!m_fullScreenRecordingAction) {
        return;
    }
```

- [ ] **Step 4: Run tray tests**

Run:

```bash
./scripts/build.sh
cmake --build build --target App_MainApplicationTrayMenu
ctest --test-dir build -R App_MainApplicationTrayMenu --output-on-failure
```

Expected: tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/MainApplication.cpp include/MainApplication.h tests/App/tst_MainApplicationTrayMenu.cpp
git commit -m "feat(app): hide recording action on linux"
```

## Task 8: Linux Runtime Guard

**Files:**
- Modify: `src/main.cpp`
- Test: `tests/Platform/tst_PlatformCapabilities.cpp`

- [ ] **Step 1: Add runtime guard test coverage**

In `tests/Platform/tst_PlatformCapabilities.cpp`, add private slot:

```cpp
    void unsupportedRuntimeMessageIsEmptyOnlyWhenSupported();
```

Add implementation:

```cpp
void tst_PlatformCapabilities::unsupportedRuntimeMessageIsEmptyOnlyWhenSupported()
{
    const auto linuxX11 = SnapTray::capabilitiesForPlatform(
        SnapTray::PlatformKind::Linux,
        SnapTray::DisplayServerKind::X11);
    QVERIFY(linuxX11.isRuntimeSupported);
    QVERIFY(linuxX11.unsupportedRuntimeMessage.isEmpty());

    const auto linuxOffscreen = SnapTray::capabilitiesForPlatform(
        SnapTray::PlatformKind::Linux,
        SnapTray::DisplayServerKind::Offscreen);
    QVERIFY(!linuxOffscreen.isRuntimeSupported);
    QVERIFY(!linuxOffscreen.unsupportedRuntimeMessage.isEmpty());
}
```

- [ ] **Step 2: Add GUI/command runtime guard**

In `src/main.cpp`, include:

```cpp
#include "platform/PlatformCapabilities.h"
```

Add helper near file top:

```cpp
namespace {
bool shouldBypassRuntimeGuardForMetadataCommand(const QStringList& arguments)
{
    return arguments.size() >= 2 &&
        (arguments.at(1) == QStringLiteral("--help") ||
         arguments.at(1) == QStringLiteral("-h") ||
         arguments.at(1) == QStringLiteral("--version") ||
         arguments.at(1) == QStringLiteral("-v"));
}

int reportUnsupportedRuntime(const QString& message)
{
    QTextStream err(stderr);
    err << "Error: " << message << "\n";
    return 1;
}
}
```

After constructing `QApplication` in CLI mode and before `CLIHandler cliHandler;`, add:

```cpp
        const auto capabilities = SnapTray::currentPlatformCapabilities();
        if (!capabilities.isRuntimeSupported &&
            !shouldBypassRuntimeGuardForMetadataCommand(arguments)) {
            return reportUnsupportedRuntime(capabilities.unsupportedRuntimeMessage);
        }
```

After constructing GUI `QApplication` and setting app metadata, add:

```cpp
    const auto capabilities = SnapTray::currentPlatformCapabilities();
    if (!capabilities.isRuntimeSupported) {
        return reportUnsupportedRuntime(capabilities.unsupportedRuntimeMessage);
    }
```

- [ ] **Step 3: Verify metadata command still works without display**

Run on Linux CI or local Linux shell:

```bash
QT_QPA_PLATFORM=offscreen ./build-linux/bin/SnapTray-Debug --version
```

Expected:

```text
SnapTray version <current-version>
```

Run:

```bash
QT_QPA_PLATFORM=offscreen ./build-linux/bin/SnapTray-Debug region
```

Expected: exits non-zero with `SnapTray Ubuntu 22.04 beta supports X11 sessions only.`

- [ ] **Step 4: Run tests**

Run:

```bash
./scripts/build.sh
cmake --build build --target Platform_Capabilities
ctest --test-dir build -R Platform_Capabilities --output-on-failure
```

Expected: tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp tests/Platform/tst_PlatformCapabilities.cpp
git commit -m "feat(linux): enforce x11 beta runtime"
```

## Task 9: Linux Build And CI

**Files:**
- Modify: `scripts/build.sh`
- Modify: `scripts/run-tests.sh`
- Modify: `.github/workflows/ci.yml`
- Modify: `docs/developer/build-from-source.md`
- Modify: `docs/zh-tw/developer/build-from-source.md`

- [ ] **Step 1: Update scripts for Linux**

In `scripts/build.sh`, inside `configure_build_dir`, add Linux generator/prefix handling after macOS block:

```bash
    if [[ "$OSTYPE" == linux* ]]; then
        desired_generator="Ninja"
        buildsystem_file="$build_dir/build.ninja"
        cmake_args=(-G "$desired_generator" "${cmake_args[@]}")
        if [ -n "${QT_ROOT_DIR:-}" ]; then
            cmake_args+=("-DCMAKE_PREFIX_PATH=${QT_ROOT_DIR}")
        fi
    fi
```

Update final output:

```bash
if [[ "$OSTYPE" == darwin* ]]; then
    echo "Build complete: $BUILD_DIR/bin/SnapTray-Debug.app"
else
    echo "Build complete: $BUILD_DIR/bin/SnapTray-Debug"
fi
```

In `scripts/run-tests.sh`, mirror the same Linux CMake prefix behavior and use:

```bash
if [[ "$OSTYPE" == linux* ]] && [ -z "${DISPLAY:-}" ] && command -v xvfb-run >/dev/null 2>&1; then
    xvfb-run -a ctest --test-dir "$BUILD_DIR" --output-on-failure
else
    ctest --test-dir "$BUILD_DIR" --output-on-failure
fi
```

- [ ] **Step 2: Add Ubuntu 22.04 CI job**

In `.github/workflows/ci.yml`, add a job:

```yaml
  build-linux:
    runs-on: ubuntu-22.04
    steps:
      - name: Checkout repository
        uses: actions/checkout@v4

      - name: Install system dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y \
            build-essential cmake ninja-build ccache git patchelf file libfuse2 \
            xvfb dbus-x11 \
            libgl1-mesa-dev libxkbcommon-x11-0 libxcb-cursor0 \
            libxcb-keysyms1-dev libxcb-xinerama0-dev libxcb-render-util0-dev \
            libxcb-image0-dev libxcb-icccm4-dev libxcb-randr0-dev \
            libxcb-shape0-dev libxcb-xfixes0-dev libxcb-sync-dev \
            libxrender-dev libxi-dev libxtst-dev

      - name: Install Qt
        uses: jurplel/install-qt-action@v4
        with:
          version: '6.10.1'
          host: 'linux'
          target: 'desktop'
          arch: 'linux_gcc_64'
          cache: true

      - name: Configure CMake
        run: |
          cmake -S . -B build -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR" \
            -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
            -DCMAKE_C_COMPILER_LAUNCHER=ccache

      - name: Build
        run: cmake --build build --config Release --parallel

      - name: Run Tests
        run: xvfb-run -a ctest --test-dir build --output-on-failure -C Release
```

The `install-qt-action` README documents `host: linux`, `target: desktop`, `arch: linux_gcc_64`, and that Qt 6+ Linux builds require Ubuntu 20.04 or later.

- [ ] **Step 3: Run local Linux script verification**

On Ubuntu 22.04:

```bash
QT_ROOT_DIR="$HOME/Qt/6.10.1/gcc_64" ./scripts/build.sh
QT_ROOT_DIR="$HOME/Qt/6.10.1/gcc_64" ./scripts/run-tests.sh
```

Expected: build and tests complete.

- [ ] **Step 4: Update build docs**

In `docs/developer/build-from-source.md`, update supported platforms:

```markdown
- Ubuntu 22.04 X11 beta
```

Add Linux prerequisites:

```markdown
### Linux

- Ubuntu 22.04 X11 session
- Qt 6.10.1 with Widgets, Gui, Svg, Concurrent, Network, Quick, QuickControls2, and QuickWidgets
- CMake 3.16+
- Ninja
- Git
- X11 development libraries required by Qt and QHotkey
```

Add commands:

```bash
./scripts/build.sh
./scripts/run-tests.sh
```

In `docs/zh-tw/developer/build-from-source.md`, add the matching Traditional Chinese summary:

```markdown
### Linux

Ubuntu 22.04 X11 beta 使用與 macOS 相同的 shell scripts：

```bash
./scripts/build.sh
./scripts/run-tests.sh
```
```

- [ ] **Step 5: Commit**

```bash
git add scripts/build.sh scripts/run-tests.sh .github/workflows/ci.yml docs/developer/build-from-source.md docs/zh-tw/developer/build-from-source.md
git commit -m "ci: add ubuntu x11 build"
```

## Task 10: AppImage Packaging

**Files:**
- Create: `packaging/linux/package-appimage.sh`
- Create: `packaging/linux/SnapTray.desktop`
- Modify: `.github/workflows/release.yml`
- Modify: `docs/developer/release-packaging.md`
- Modify: `docs/zh-tw/developer/release-packaging.md`

- [ ] **Step 1: Create desktop metadata**

Create `packaging/linux/SnapTray.desktop`:

```ini
[Desktop Entry]
Type=Application
Name=SnapTray
Comment=Screenshot and annotation tool
Exec=SnapTray
Icon=snaptray
Terminal=false
Categories=Utility;Graphics;
StartupNotify=false
```

- [ ] **Step 2: Create AppImage package script**

Create `packaging/linux/package-appimage.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_DIR/release-linux"
APPDIR="$PROJECT_DIR/build/AppDir"
DIST_DIR="$PROJECT_DIR/dist"
TOOLS_DIR="$PROJECT_DIR/build/appimage-tools"

VERSION="$(grep "project(SnapTray VERSION" "$PROJECT_DIR/CMakeLists.txt" | sed -E 's/.*VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/')"

mkdir -p "$DIST_DIR" "$TOOLS_DIR"
rm -rf "$APPDIR"

cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  ${QT_ROOT_DIR:+-DCMAKE_PREFIX_PATH="$QT_ROOT_DIR"}
cmake --build "$BUILD_DIR" --target SnapTray --parallel

mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/256x256/apps"
cp "$BUILD_DIR/bin/SnapTray" "$APPDIR/usr/bin/SnapTray"
cp "$SCRIPT_DIR/SnapTray.desktop" "$APPDIR/usr/share/applications/SnapTray.desktop"
cp "$PROJECT_DIR/resources/icons/snaptray.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/snaptray.png"

LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_QT="$TOOLS_DIR/linuxdeploy-plugin-qt-x86_64.AppImage"

if [ ! -x "$LINUXDEPLOY" ]; then
  curl -L \
    -o "$LINUXDEPLOY" \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
  chmod +x "$LINUXDEPLOY"
fi

if [ ! -x "$LINUXDEPLOY_QT" ]; then
  curl -L \
    -o "$LINUXDEPLOY_QT" \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
  chmod +x "$LINUXDEPLOY_QT"
fi

export QML_SOURCES_PATHS="$PROJECT_DIR/src/qml"
export EXTRA_QT_PLUGINS="imageformats;platforms;platformthemes;xcbglintegrations"
export OUTPUT="SnapTray-$VERSION-x86_64.AppImage"

cd "$PROJECT_DIR"
"$LINUXDEPLOY" \
  --appdir "$APPDIR" \
  --desktop-file "$APPDIR/usr/share/applications/SnapTray.desktop" \
  --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/snaptray.png" \
  --executable "$APPDIR/usr/bin/SnapTray" \
  --plugin qt \
  --output appimage

mv "$PROJECT_DIR/$OUTPUT" "$DIST_DIR/$OUTPUT"

QT_QPA_PLATFORM=offscreen "$DIST_DIR/$OUTPUT" --version >/tmp/snaptray-appimage-version.txt
grep -q "SnapTray version" /tmp/snaptray-appimage-version.txt

echo "AppImage complete: $DIST_DIR/$OUTPUT"
```

Make executable:

```bash
chmod +x packaging/linux/package-appimage.sh
```

The AppImage docs state that linuxdeploy uses `--plugin qt` for Qt bundling and `--output appimage` for AppImage output.

- [ ] **Step 3: Run package script on Ubuntu 22.04**

Run:

```bash
QT_ROOT_DIR="$HOME/Qt/6.10.1/gcc_64" ./packaging/linux/package-appimage.sh
```

Expected:

```text
AppImage complete: /home/.../snap-tray/dist/SnapTray-<version>-x86_64.AppImage
```

- [ ] **Step 4: Add release workflow job**

In `.github/workflows/release.yml`, add a `build-linux-appimage` job after Windows/macOS build jobs:

```yaml
  build-linux-appimage:
    needs: prepare
    runs-on: ubuntu-22.04
    steps:
      - name: Checkout repository
        uses: actions/checkout@v4

      - name: Install system dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y \
            build-essential cmake ninja-build ccache git curl patchelf file libfuse2 \
            libgl1-mesa-dev libxkbcommon-x11-0 libxcb-cursor0 \
            libxcb-keysyms1-dev libxcb-xinerama0-dev libxcb-render-util0-dev \
            libxcb-image0-dev libxcb-icccm4-dev libxcb-randr0-dev \
            libxcb-shape0-dev libxcb-xfixes0-dev libxcb-sync-dev \
            libxrender-dev libxi-dev libxtst-dev

      - name: Install Qt
        uses: jurplel/install-qt-action@v4
        with:
          version: '6.10.1'
          host: 'linux'
          target: 'desktop'
          arch: 'linux_gcc_64'
          cache: true

      - name: Build AppImage
        run: ./packaging/linux/package-appimage.sh

      - name: Upload Linux artifact
        uses: actions/upload-artifact@v4
        with:
          name: linux-appimage
          path: dist/SnapTray-${{ needs.prepare.outputs.version }}-x86_64.AppImage
```

Update the existing `release` job dependencies:

```yaml
  release:
    needs: [prepare, build-macos, build-windows, build-linux-appimage]
```

Update the existing GitHub Release upload `files` list:

```yaml
          files: |
            artifacts/macos-dmg/*.dmg
            artifacts/windows-nsis/*.exe
            artifacts/linux-appimage/*.AppImage
```

- [ ] **Step 5: Update packaging docs**

In `docs/developer/release-packaging.md`, add:

```markdown
### Linux

```bash
./packaging/linux/package-appimage.sh
# Output: dist/SnapTray-<version>-x86_64.AppImage
```

The Linux beta artifact targets Ubuntu 22.04 X11. It does not include in-app
updates; users download a newer AppImage for upgrades.
```

In `docs/zh-tw/developer/release-packaging.md`, add:

```markdown
### Linux

```bash
./packaging/linux/package-appimage.sh
# Output: dist/SnapTray-<version>-x86_64.AppImage
```

Linux beta 目標是 Ubuntu 22.04 X11。此版本沒有內建更新，升級方式是下載新的 AppImage。
```

- [ ] **Step 6: Commit**

```bash
git add packaging/linux/package-appimage.sh packaging/linux/SnapTray.desktop .github/workflows/release.yml docs/developer/release-packaging.md docs/zh-tw/developer/release-packaging.md
git commit -m "build(linux): package appimage"
```

## Task 11: User And Architecture Documentation

**Files:**
- Modify: `docs/developer/architecture.md`
- Modify: `docs/zh-tw/developer/architecture.md`
- Modify: `docs/docs/troubleshooting.md`
- Modify: `docs/zh-tw/docs/troubleshooting.md`
- Modify: `docs/docs/getting-started.md`
- Modify: `docs/zh-tw/docs/getting-started.md`
- Modify: `docs/_data/i18n/en.yml`
- Modify: `docs/_data/i18n/zh-tw.yml`
- Modify: `README.md`
- Modify: `README.zh-TW.md`

- [ ] **Step 1: Update architecture platform map**

In `docs/developer/architecture.md`, update Tech stack packaging targets:

```markdown
- Packaging targets: macOS DMG, Windows NSIS, Windows MSIX, Linux AppImage beta
```

Add Linux column to the platform table:

```markdown
| Feature | Windows | macOS | Linux beta |
|---|---|---|---|
| Screen capture | `DXGICaptureEngine_win.cpp` | `SCKCaptureEngine_mac.mm` | `QtCaptureEngine.cpp` on X11 |
| Video encoding | `MediaFoundationEncoder.cpp` | `AVFoundationEncoder.mm` | Not supported |
| Audio capture | `WASAPIAudioCaptureEngine_win.cpp` | `CoreAudioCaptureEngine_mac.mm` | Not supported |
| Video playback | `MediaFoundationPlayer_win.cpp` | `AVFoundationPlayer_mac.mm` | Not supported |
| Window detection | `WindowDetector_win.cpp` | `WindowDetector.mm` | Not supported |
| OCR | `OCRManager_win.cpp` | `OCRManager.mm` | Not supported |
| Window level | `WindowLevel_win.cpp` | `WindowLevel_mac.mm` | `WindowLevel_linux.cpp` |
| Platform features | `PlatformFeatures_win.cpp` | `PlatformFeatures_mac.mm` | `PlatformFeatures_linux.cpp` |
| Auto-launch | `AutoLaunchManager_win.cpp` | `AutoLaunchManager_mac.mm` | `AutoLaunchManager_linux.cpp` |
| Install source | `InstallSourceDetector_win.cpp` | `InstallSourceDetector_mac.mm` | `InstallSourceDetector_linux.cpp` |
```

In `docs/zh-tw/developer/architecture.md`, add the same Linux beta information in Traditional Chinese.

- [ ] **Step 2: Update user docs**

In `docs/docs/getting-started.md`, add a Linux beta note:

```markdown
### Linux beta

SnapTray provides an Ubuntu 22.04 X11 beta as an AppImage. Recording and OCR are
not included in the Linux beta. Wayland sessions are not supported.
```

In `docs/docs/troubleshooting.md`, add:

```markdown
### Linux beta: app exits on Wayland

The Ubuntu 22.04 beta supports X11 sessions only. On the login screen, choose an
X11 session before launching SnapTray.

### Linux beta: hotkeys do not register

Global hotkeys require an X11 session and may conflict with desktop environment
shortcuts. Open Settings > Hotkeys to see which shortcut failed and assign a
different key sequence.
```

Add matching zh-TW sections in `docs/zh-tw/docs/getting-started.md` and `docs/zh-tw/docs/troubleshooting.md`.

- [ ] **Step 3: Update README platform statements**

In `README.md`, change platform sentence to:

```markdown
SnapTray is a Qt 6 screenshot and annotation app for macOS, Windows, and Ubuntu 22.04 X11 beta.
```

Add beta limitation bullet:

```markdown
- Linux beta: Ubuntu 22.04 X11 AppImage; recording and OCR are not shown.
```

In `README.zh-TW.md`, add the corresponding Traditional Chinese text:

```markdown
Linux beta：Ubuntu 22.04 X11 AppImage；不顯示錄影與 OCR。
```

- [ ] **Step 4: Update site strings**

In `docs/_data/i18n/en.yml` and `docs/_data/i18n/zh-tw.yml`, update download/platform copy that says only macOS/Windows to include Linux beta. Keep the wording explicit:

```yaml
site:
  tagline: "Free Screenshot & Annotation Tool for macOS, Windows, and Linux beta"

hero:
  title: Fast Screenshot Tool for Mac, Windows, and Linux beta
  pills:
    - macOS 14+
    - Windows 10+
    - Ubuntu 22.04 X11 beta
    - Qt 6.10.1

downloads:
  linux:
    label: Linux beta
    title: AppImage
    requirements: Ubuntu 22.04 X11 session
    button: Download AppImage
    empty: Linux AppImage beta coming soon
  req_cards:
    # Append this item after the existing macOS and Windows cards.
    - title: Linux beta
      items:
        - Ubuntu 22.04 X11 session
        - Recording and OCR are not included
        - Wayland sessions are not supported
```

For `docs/_data/i18n/zh-tw.yml`, use the same keys and Traditional Chinese values:

```yaml
site:
  tagline: "免費截圖與標註工具｜macOS、Windows 與 Linux beta"

hero:
  title: 快速截圖工具｜Mac、Windows 與 Linux beta
  pills:
    - macOS 14+
    - Windows 10+
    - Ubuntu 22.04 X11 beta
    - Qt 6.10.1

downloads:
  linux:
    label: Linux beta
    title: AppImage
    requirements: Ubuntu 22.04 X11 session
    button: 下載 AppImage
    empty: Linux AppImage beta 即將推出
  req_cards:
    # Append this item after the existing macOS and Windows cards.
    - title: Linux beta
      items:
        - Ubuntu 22.04 X11 session
        - 不包含錄影與 OCR
        - 不支援 Wayland session
```

- [ ] **Step 5: Verify docs build inputs**

Run:

```bash
bundle exec jekyll build --source docs --destination docs/_site
```

Expected: Jekyll build completes. If Ruby dependencies are unavailable locally, run:

```bash
cd docs && bundle install && bundle exec jekyll build
```

- [ ] **Step 6: Commit**

```bash
git add docs/developer/architecture.md docs/zh-tw/developer/architecture.md docs/docs/troubleshooting.md docs/zh-tw/docs/troubleshooting.md docs/docs/getting-started.md docs/zh-tw/docs/getting-started.md docs/_data/i18n/en.yml docs/_data/i18n/zh-tw.yml README.md README.zh-TW.md
git commit -m "docs: document linux beta support"
```

## Task 12: Full Verification

**Files:**
- No planned file changes.

- [ ] **Step 1: Run current platform verification**

Run on the current development platform:

```bash
./scripts/build.sh
./scripts/run-tests.sh
```

Expected: build succeeds and all tests pass.

- [ ] **Step 2: Run Ubuntu 22.04 verification**

Run on Ubuntu 22.04 with Qt 6.10.1:

```bash
QT_ROOT_DIR="$HOME/Qt/6.10.1/gcc_64" ./scripts/build.sh
QT_ROOT_DIR="$HOME/Qt/6.10.1/gcc_64" ./scripts/run-tests.sh
QT_ROOT_DIR="$HOME/Qt/6.10.1/gcc_64" ./packaging/linux/package-appimage.sh
```

Expected:

```text
Build complete: .../build/bin/SnapTray-Debug
100% tests passed
AppImage complete: .../dist/SnapTray-<version>-x86_64.AppImage
```

- [ ] **Step 3: Run Linux manual QA on X11**

On Ubuntu 22.04 X11:

```bash
chmod +x dist/SnapTray-<version>-x86_64.AppImage
./dist/SnapTray-<version>-x86_64.AppImage
```

Verify:

- Tray icon appears.
- Tray menu does not show `Record Screen`.
- Settings sidebar does not show `OCR` or `Recording`.
- Files settings do not show recording folder or auto-save recordings.
- Region Capture opens and captures a manually selected region.
- Annotation tools work and OCR button is absent.
- Copy and Save work.
- Pin from capture and Pin from Image work.
- Screen Canvas opens and exits.
- Default region capture hotkey triggers capture.
- Hotkey setting changes persist and status is visible.

- [ ] **Step 4: Run Linux Wayland rejection check**

On Ubuntu 22.04 Wayland:

```bash
./dist/SnapTray-<version>-x86_64.AppImage region
```

Expected: non-zero exit and message containing:

```text
SnapTray Ubuntu 22.04 beta supports X11 sessions only.
```

- [ ] **Step 5: Final status commit**

If manual QA required small fixes, commit them with focused messages. If no file changes remain:

```bash
git status --short
```

Expected: no uncommitted changes.
