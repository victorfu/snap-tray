#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QTemporaryDir>

#include "AutoLaunchManager.h"

class tst_AutoLaunchLinux : public QObject
{
    Q_OBJECT
private slots:
    void launchesLiteralPath_data();
    void launchesLiteralPath();
};

void tst_AutoLaunchLinux::launchesLiteralPath_data()
{
    QTest::addColumn<QString>("relativePath");
    QTest::newRow("plain") << QStringLiteral("SnapTray");
    QTest::newRow("file-code") << QStringLiteral("snap%foo");
    QTest::newRow("numeric-code") << QStringLiteral("snap%25");
    QTest::newRow("trailing-percent") << QStringLiteral("snap%");
    QTest::newRow("double-percent") << QStringLiteral("snap%%f");
    QTest::newRow("space") << QStringLiteral("a directory/snap %foo");
    QTest::newRow("quoted-characters") << QStringLiteral("a%25 directory/snap \\\"`$%f");
    QTest::newRow("unicode") << QString::fromUtf8("截圖 %25/SnapTray");
    QTest::newRow("shell-syntax") << QStringLiteral("snap%f;$(printf PWNED)&'");
    QTest::newRow("control-characters") << QStringLiteral("snap%25\nwith\ttabs\rand-newlines");
    QTest::newRow("equals") << QStringLiteral("snap=%foo");
}

void tst_AutoLaunchLinux::launchesLiteralPath()
{
    QFETCH(QString, relativePath);
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString executable = temp.filePath(relativePath);
    QVERIFY(QDir().mkpath(QFileInfo(executable).absolutePath()));
    QFile program(executable);
    QVERIFY(program.open(QIODevice::WriteOnly));
    program.write("#!/bin/sh\nprintf '%s\\n' \"$0\" \"$@\" > \"$SNAPTRAY_AUTOLAUNCH_TEST_OUTPUT\"\n");
    program.close();
    QVERIFY(program.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));

    const QString output = temp.filePath("launched.txt");
    qputenv("SNAPTRAY_AUTOLAUNCH_TEST_OUTPUT", output.toUtf8());
    qputenv("XDG_CONFIG_HOME", temp.filePath("config").toUtf8());
    qputenv("APPIMAGE", executable.toUtf8());
    QVERIFY(AutoLaunchManager::setEnabled(true));
    QVERIFY(AutoLaunchManager::isEnabled());
    const QString desktopPath = temp.filePath("config/autostart/SnapTray.desktop");
    QFile desktop(desktopPath);
    QVERIFY(desktop.open(QIODevice::ReadOnly));
    const QByteArray canonical = desktop.readAll();
    desktop.close();

    // Use the desktop environment's actual parser, including its executable
    // availability check before field-code expansion. No real app is launched.
    QLibrary gio(QStringLiteral("libgio-2.0.so.0"));
    QLibrary gobject(QStringLiteral("libgobject-2.0.so.0"));
    auto load = reinterpret_cast<void* (*)(const char*)>(
        gio.resolve("g_desktop_app_info_new_from_filename"));
    auto launch = reinterpret_cast<int (*)(void*, void*, void*, void**)>(
        gio.resolve("g_app_info_launch"));
    auto unref = reinterpret_cast<void (*)(void*)>(gobject.resolve("g_object_unref"));
    QVERIFY(load && launch && unref);
    void* info = load(QFile::encodeName(desktopPath).constData());
    QVERIFY2(info, canonical.constData());
    const bool launched = launch(info, nullptr, nullptr, nullptr);
    unref(info);
    QVERIFY2(launched, canonical.constData());
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(output), 5000);
    QFile result(output);
    QVERIFY(result.open(QIODevice::ReadOnly));
    const QByteArray expectedOutput = executable.toUtf8() + "\n--minimized\n";
    QTRY_COMPARE_WITH_TIMEOUT(result.size(), expectedOutput.size(), 5000);
    QCOMPARE(result.readAll(), expectedOutput);

    // Existing unescaped entries must be normalized on startup, including
    // installations with no saved start-on-login preference.
    QVERIFY(desktop.open(QIODevice::WriteOnly | QIODevice::Truncate));
    desktop.write("[Desktop Entry]\nType=Application\nName=SnapTray\nExec=\""
                  + executable.toUtf8() + "\" --minimized\n");
    desktop.close();
    QVERIFY(AutoLaunchManager::syncWithPreference());
    QVERIFY(desktop.open(QIODevice::ReadOnly));
    QCOMPARE(desktop.readAll(), canonical);
    desktop.close();
    // A canonical entry must remain untouched on subsequent startups.
    const QByteArray annotated = canonical + "# Keep this marker\n";
    QVERIFY(desktop.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(desktop.write(annotated), qint64(annotated.size()));
    desktop.close();
    QVERIFY(AutoLaunchManager::syncWithPreference());
    QVERIFY(desktop.open(QIODevice::ReadOnly));
    QCOMPARE(desktop.readAll(), annotated);
    desktop.close();
    QVERIFY(AutoLaunchManager::setEnabled(false));
    QVERIFY(!QFileInfo::exists(desktopPath));
}

QTEST_GUILESS_MAIN(tst_AutoLaunchLinux)
#include "tst_AutoLaunchLinux.moc"
