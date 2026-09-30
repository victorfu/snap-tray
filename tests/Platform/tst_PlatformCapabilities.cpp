#include <QtTest/QtTest>

#include "platform/PlatformCapabilities.h"
#include "platform/CaptureExclusionPolicy.h"

class tst_PlatformCapabilities : public QObject
{
    Q_OBJECT

private slots:
    void linuxX11BetaCapabilities();
    void linuxWaylandIsUnsupportedRuntime();
    void unsupportedRuntimeMessageIsEmptyOnlyWhenSupported();
    void macAndWindowsKeepRecordingAndOcrSupport();
    void displayServerDetectionUsesSessionAndQtPlatform();
    void linuxSessionBackendMatrix_data();
    void linuxSessionBackendMatrix();
    void windowsCaptureExclusionVersionGate_data();
    void windowsCaptureExclusionVersionGate();
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
    QVERIFY(caps.supportsWindowDetection);
    QVERIFY(!caps.supportsClickThrough);
    QVERIFY(!caps.supportsLiveCapture);
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
    QVERIFY(macCaps.supportsClickThrough);
    QVERIFY(macCaps.supportsLiveCapture);
    QVERIFY(macCaps.supportsInAppUpdates);

    const auto winCaps = SnapTray::capabilitiesForPlatform(
        SnapTray::PlatformKind::Windows,
        SnapTray::DisplayServerKind::Unknown, QOperatingSystemVersion::Windows10_2004);
    QVERIFY(winCaps.isRuntimeSupported);
    QVERIFY(winCaps.supportsRecording);
    QVERIFY(winCaps.supportsOCR);
    QVERIFY(winCaps.supportsWindowDetection);
    QVERIFY(winCaps.supportsClickThrough);
    QVERIFY(winCaps.supportsLiveCapture);
    QVERIFY(winCaps.supportsInAppUpdates);
}

void tst_PlatformCapabilities::displayServerDetectionUsesSessionAndQtPlatform()
{
    QCOMPARE(SnapTray::displayServerKindFromSessionType(QStringLiteral("x11"), QString()),
             SnapTray::DisplayServerKind::Other);
    QCOMPARE(SnapTray::displayServerKindFromSessionType(QStringLiteral("wayland"), QString()),
             SnapTray::DisplayServerKind::Wayland);
    QCOMPARE(SnapTray::displayServerKindFromSessionType(QString(), QStringLiteral("xcb")),
             SnapTray::DisplayServerKind::X11);
    QCOMPARE(SnapTray::displayServerKindFromSessionType(QString(), QStringLiteral("wayland")),
             SnapTray::DisplayServerKind::Wayland);
    QCOMPARE(SnapTray::displayServerKindFromSessionType(QString(), QStringLiteral("offscreen")),
             SnapTray::DisplayServerKind::Offscreen);
}

void tst_PlatformCapabilities::linuxSessionBackendMatrix_data()
{
    using Kind = SnapTray::DisplayServerKind;
    QTest::addColumn<QString>("session");
    QTest::addColumn<QString>("backend");
    QTest::addColumn<bool>("waylandEnvironment");
    QTest::addColumn<Kind>("expectedKind");
    const auto row = [](const char* name, const char* session, const char* backend,
                        Kind expected, bool waylandEnvironment = false) {
        QTest::newRow(name) << QString::fromLatin1(session) << QString::fromLatin1(backend)
                           << waylandEnvironment << expected;
    };

    row("x11-xcb", "x11", "xcb", Kind::X11);
    row("xvfb-no-session", "", "xcb", Kind::X11);
    row("startx-tty", "tty", "xcb", Kind::X11);
    row("unspecified-xcb", "unspecified", "xcb", Kind::X11);
    row("case-whitespace", " X11 ", " XCB ", Kind::X11);
    row("empty-session-whitespace", " \t", "xcb", Kind::X11);
    row("unknown-session", "mir", "xcb", Kind::Other);

    row("xwayland-session", "wayland", "xcb", Kind::Wayland);
    row("xwayland-environment", "", "xcb", Kind::Wayland, true);
    row("xwayland-tty", "tty", "xcb", Kind::Wayland, true);
    row("conflicting-wayland-environment", "x11", "xcb", Kind::Wayland, true);
    row("x11-wayland-backend", "x11", "wayland", Kind::Wayland);
    row("x11-wayland-egl", "x11", "wayland-egl", Kind::Wayland);
    row("no-session-wayland", "", "wayland", Kind::Wayland);
    row("no-session-wayland-egl", "", "wayland-egl", Kind::Wayland);
    row("wayland-no-backend", "wayland", "", Kind::Wayland);

    row("x11-offscreen", "x11", "offscreen", Kind::Offscreen);
    row("wayland-offscreen", "wayland", "offscreen", Kind::Offscreen, true);
    row("no-session-offscreen", "", "offscreen", Kind::Offscreen);
    row("tty-offscreen", "tty", "offscreen", Kind::Offscreen);
    row("x11-minimal", "x11", "minimal", Kind::Offscreen);
    row("wayland-minimal", "wayland", "minimal", Kind::Offscreen, true);
    row("no-session-minimal", "", "minimal", Kind::Offscreen);
    row("tty-minimal", "tty", "minimal", Kind::Offscreen);

    row("x11-no-backend", "x11", "", Kind::Other);
    row("tty-no-backend", "tty", "", Kind::Other);
    row("x11-eglfs", "x11", "eglfs", Kind::Other);
    row("no-session-eglfs", "", "eglfs", Kind::Other);
    row("no-session-no-backend", "", "", Kind::Unknown);
}

void tst_PlatformCapabilities::linuxSessionBackendMatrix()
{
    QFETCH(QString, session);
    QFETCH(QString, backend);
    QFETCH(bool, waylandEnvironment);
    QFETCH(SnapTray::DisplayServerKind, expectedKind);
    const bool supported = expectedKind == SnapTray::DisplayServerKind::X11;
    const auto kind = SnapTray::displayServerKindFromSessionType(session, backend, waylandEnvironment);
    QCOMPARE(kind, expectedKind);
    const auto caps = SnapTray::capabilitiesForPlatform(SnapTray::PlatformKind::Linux, kind);
    QCOMPARE(caps.isRuntimeSupported, supported);
    QCOMPARE(caps.supportsGlobalHotkeys, supported);
    QCOMPARE(caps.supportsWindowDetection, supported);
    QVERIFY(!caps.supportsRecording);
    QVERIFY(!caps.supportsOCR);
    QCOMPARE(caps.unsupportedRuntimeMessage.isEmpty(), supported);
}

void tst_PlatformCapabilities::windowsCaptureExclusionVersionGate_data()
{
    QTest::addColumn<int>("major");
    QTest::addColumn<int>("build");
    QTest::addColumn<bool>("supported");
    QTest::newRow("windows7") << 6 << 7601 << false;
    QTest::newRow("1809") << 10 << 17763 << false;
    QTest::newRow("1909") << 10 << 18363 << false;
    QTest::newRow("before-2004") << 10 << 19040 << false;
    QTest::newRow("2004") << 10 << 19041 << true;
    QTest::newRow("22H2") << 10 << 19045 << true;
    QTest::newRow("windows11") << 10 << 22000 << true;
    QTest::newRow("unknown-build") << 10 << -1 << false;
}

void tst_PlatformCapabilities::windowsCaptureExclusionVersionGate()
{
    QFETCH(int, major);
    QFETCH(int, build);
    QFETCH(bool, supported);
    const QOperatingSystemVersion version(QOperatingSystemVersion::Windows, major, 0, build);
    QCOMPARE(SnapTray::windowsCaptureAffinity(true, version), supported ? quint32(0x11) : quint32(0));
    QCOMPARE(SnapTray::windowsCaptureAffinity(false, version), quint32(0));
    QCOMPARE(SnapTray::requiresVisibleRecordingControls(version), !supported);
    const auto caps = SnapTray::capabilitiesForPlatform(
        SnapTray::PlatformKind::Windows, SnapTray::DisplayServerKind::Unknown, version);
    QCOMPARE(caps.supportsLiveCapture, supported);
    QCOMPARE(caps.liveCaptureUnavailableReason.isEmpty(), supported);
    QVERIFY(caps.isRuntimeSupported);
    QVERIFY(caps.supportsRecording);
    QVERIFY(!SnapTray::requiresVisibleRecordingControls(QOperatingSystemVersion(QOperatingSystemVersion::MacOS, 14, 0, 0)));
}

QTEST_MAIN(tst_PlatformCapabilities)
#include "tst_PlatformCapabilities.moc"
