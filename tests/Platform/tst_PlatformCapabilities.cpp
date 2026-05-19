#include <QtTest/QtTest>

#include "platform/PlatformCapabilities.h"

class tst_PlatformCapabilities : public QObject
{
    Q_OBJECT

private slots:
    void linuxX11BetaCapabilities();
    void linuxWaylandIsUnsupportedRuntime();
    void unsupportedRuntimeMessageIsEmptyOnlyWhenSupported();
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
