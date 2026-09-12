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
             SnapTray::DisplayServerKind::Other);
    QCOMPARE(SnapTray::displayServerKindFromSessionType(QString(), QStringLiteral("wayland")),
             SnapTray::DisplayServerKind::Wayland);
    QCOMPARE(SnapTray::displayServerKindFromSessionType(QString(), QStringLiteral("offscreen")),
             SnapTray::DisplayServerKind::Offscreen);
}

void tst_PlatformCapabilities::linuxSessionBackendMatrix_data()
{
    QTest::addColumn<QString>("session");
    QTest::addColumn<QString>("backend");
    for (const QString& session : {QStringLiteral("x11"), QStringLiteral("wayland"),
                                   QStringLiteral("tty"), QString()}) {
        for (const QString& backend : {QStringLiteral("xcb"), QStringLiteral("wayland"),
                                       QStringLiteral("wayland-egl"), QStringLiteral("offscreen"),
                                       QStringLiteral("minimal"), QString()}) {
            QTest::addRow("%s-%s", qPrintable(session), qPrintable(backend)) << session << backend;
        }
    }
    QTest::newRow("case-whitespace") << QStringLiteral(" X11 ") << QStringLiteral(" XCB ");
}

void tst_PlatformCapabilities::linuxSessionBackendMatrix()
{
    QFETCH(QString, session);
    QFETCH(QString, backend);
    const bool supported = session.trimmed().compare("x11", Qt::CaseInsensitive) == 0
        && backend.trimmed().compare("xcb", Qt::CaseInsensitive) == 0;
    const auto kind = SnapTray::displayServerKindFromSessionType(session, backend);
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
