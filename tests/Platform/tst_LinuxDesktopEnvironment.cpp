#include <QtTest/QtTest>

#include "platform/LinuxDesktopEnvironment.h"

class tst_LinuxDesktopEnvironment : public QObject
{
    Q_OBJECT

private slots:
    void readsXcursorValuesFromResourceManager();
    void resolvesMissingEnvironmentFromXresources();
    void keepsExplicitEnvironmentValues();
    void ignoresInvalidXcursorSize();
};

void tst_LinuxDesktopEnvironment::readsXcursorValuesFromResourceManager()
{
    const QByteArray resources =
        "Xft.dpi:\t192\n"
        "Xcursor.size:\t48\n"
        "Xcursor.theme:\tYaru\n";

    QCOMPARE(SnapTray::xResourceValue(resources, "Xcursor.size", "Xcursor.Size"),
             QByteArray("48"));
    QCOMPARE(SnapTray::xResourceValue(resources, "Xcursor.theme", "Xcursor.Theme"),
             QByteArray("Yaru"));
}

void tst_LinuxDesktopEnvironment::resolvesMissingEnvironmentFromXresources()
{
    const QByteArray resources =
        "Xcursor.size:\t48\n"
        "Xcursor.theme:\tYaru\n";

    const auto resolved = SnapTray::resolveLinuxXcursorEnvironment(
        SnapTray::LinuxXcursorEnvironment{},
        resources,
        QByteArray("/home/victor"));

    QCOMPARE(resolved.size, QByteArray("48"));
    QCOMPARE(resolved.theme, QByteArray("Yaru"));
    QCOMPARE(resolved.path,
             QByteArray("/home/victor/.local/share/icons:/home/victor/.icons:"
                        "/usr/share/icons:/usr/share/pixmaps"));
}

void tst_LinuxDesktopEnvironment::keepsExplicitEnvironmentValues()
{
    const QByteArray resources =
        "Xcursor.size:\t48\n"
        "Xcursor.theme:\tYaru\n";
    SnapTray::LinuxXcursorEnvironment current;
    current.size = "32";
    current.theme = "Breeze";
    current.path = "/custom/cursors";

    const auto resolved = SnapTray::resolveLinuxXcursorEnvironment(
        current,
        resources,
        QByteArray("/home/victor"));

    QCOMPARE(resolved.size, QByteArray("32"));
    QCOMPARE(resolved.theme, QByteArray("Breeze"));
    QCOMPARE(resolved.path, QByteArray("/custom/cursors"));
}

void tst_LinuxDesktopEnvironment::ignoresInvalidXcursorSize()
{
    const QByteArray resources =
        "Xcursor.size:\tnot-a-number\n"
        "Xcursor.theme:\tYaru\n";

    const auto resolved = SnapTray::resolveLinuxXcursorEnvironment(
        SnapTray::LinuxXcursorEnvironment{},
        resources,
        QByteArray());

    QVERIFY(resolved.size.isEmpty());
    QCOMPARE(resolved.theme, QByteArray("Yaru"));
}

QTEST_MAIN(tst_LinuxDesktopEnvironment)
#include "tst_LinuxDesktopEnvironment.moc"
