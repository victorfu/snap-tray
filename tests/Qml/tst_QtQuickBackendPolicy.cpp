#include <QtTest/QtTest>

#include <QOperatingSystemVersion>

#include "platform/QtQuickBackendPolicy.h"

class tst_QtQuickBackendPolicy : public QObject
{
    Q_OBJECT

private slots:
    void testWindows10UsesSoftwarePolicy();
    void testWindows11UsesPlatformDefaultPolicy();
    void testNonWindowsUsePlatformDefaultPolicy();
};

void tst_QtQuickBackendPolicy::testWindows10UsesSoftwarePolicy()
{
    QCOMPARE(
        SnapTray::selectQtQuickGraphicsBackendPolicy(
            QOperatingSystemVersion(QOperatingSystemVersion::Windows, 10, 0, 19045)),
        SnapTray::QtQuickGraphicsBackendPolicy::Software);
    QCOMPARE(
        SnapTray::selectQtQuickGraphicsBackendPolicy(
            QOperatingSystemVersion(QOperatingSystemVersion::Windows, 10, 0, 17763)),
        SnapTray::QtQuickGraphicsBackendPolicy::Software);
}

void tst_QtQuickBackendPolicy::testWindows11UsesPlatformDefaultPolicy()
{
    QCOMPARE(
        SnapTray::selectQtQuickGraphicsBackendPolicy(
            QOperatingSystemVersion(QOperatingSystemVersion::Windows, 10, 0, 22000)),
        SnapTray::QtQuickGraphicsBackendPolicy::PlatformDefault);
    QCOMPARE(
        SnapTray::selectQtQuickGraphicsBackendPolicy(
            QOperatingSystemVersion(QOperatingSystemVersion::Windows, 10, 0, 26100)),
        SnapTray::QtQuickGraphicsBackendPolicy::PlatformDefault);
}

void tst_QtQuickBackendPolicy::testNonWindowsUsePlatformDefaultPolicy()
{
    QCOMPARE(
        SnapTray::selectQtQuickGraphicsBackendPolicy(
            QOperatingSystemVersion(QOperatingSystemVersion::MacOS, 15, 0, 0)),
        SnapTray::QtQuickGraphicsBackendPolicy::PlatformDefault);
    QCOMPARE(
        SnapTray::selectQtQuickGraphicsBackendPolicy(
            QOperatingSystemVersion(QOperatingSystemVersion::Unknown, 0, 0, 0)),
        SnapTray::QtQuickGraphicsBackendPolicy::PlatformDefault);
}

QTEST_MAIN(tst_QtQuickBackendPolicy)
#include "tst_QtQuickBackendPolicy.moc"
