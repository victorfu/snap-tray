#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include "platform/MacCLIInstaller.h"

class TestMacCLIInstaller : public QObject
{
    Q_OBJECT
private slots:
    void wrapperUsesActualExecutable()
    {
        for (const QString& name : {QString("SnapTray"), QString("SnapTray-Debug")}) {
            const QString bundle = "/tmp/A 'quoted' $name `cmd` \\\".app";
            const QString executable = bundle + "/Contents/MacOS/" + name;
            const QByteArray wrapper = SnapTray::macCLIWrapper(executable, bundle);
            QVERIFY(wrapper.contains(("exec " + SnapTray::quoteShellLiteral(executable) + " \"$@\"").toUtf8()));
            QVERIFY(wrapper.contains(SnapTray::quoteShellLiteral(QDir(bundle).filePath("Contents/PlugIns")).toUtf8()));
        }
        QCOMPARE(SnapTray::quoteAppleScriptString("a\"b\\c\nd"), QString("\"a\\\"b\\\\c\\nd\""));
    }
    void missingExecutablePreservesExistingScript()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("snaptray");
        QFile old(path);
        QVERIFY(old.open(QIODevice::WriteOnly));
        old.write("working-cli");
        old.close();
        bool called = false;
        QVERIFY(!SnapTray::installMacCLIWrapper(path, dir.filePath("missing"), dir.path(),
            [&](const QString&) { called = true; return true; }));
        QVERIFY(!called);
        QVERIFY(old.open(QIODevice::ReadOnly));
        QCOMPARE(old.readAll(), QByteArray("working-cli"));
    }
    void installAndExecute_data()
    {
        QTest::addColumn<QString>("name");
        QTest::newRow("Release") << QString("SnapTray");
        QTest::newRow("Debug") << QString("SnapTray-Debug");
    }
    void installAndExecute()
    {
#ifndef Q_OS_UNIX
        QSKIP("POSIX wrapper execution requires macOS or Linux");
#else
        QFETCH(QString, name);
        QTemporaryDir dir;
        const QString bundle = dir.filePath("Space ' $literal `literal` \\\".app");
        const QString executable = bundle + "/Contents/MacOS/" + name;
        QVERIFY(QDir().mkpath(QFileInfo(executable).absolutePath()));
        QFile binary(executable);
        QVERIFY(binary.open(QIODevice::WriteOnly));
        binary.write("#!/bin/sh\nprintf '%s\\n' \"$QT_PLUGIN_PATH\" \"$@\"\n");
        binary.close();
        QVERIFY(binary.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QString script = dir.filePath("bin/snaptray");
        const auto shell = [](const QString& command) {
            QProcess process;
            process.start("/bin/sh", {"-c", command});
            return process.waitForFinished() && process.exitCode() == 0;
        };
        QVERIFY(SnapTray::installMacCLIWrapper(script, executable, bundle, shell));
        QVERIFY(SnapTray::isMacCLIWrapperInstalled(script, executable, bundle));
        QProcess process;
        process.start(script, {"argument with spaces", "'\"$()", ""});
        QVERIFY(process.waitForFinished());
        QCOMPARE(process.exitCode(), 0);
        QCOMPARE(process.readAllStandardOutput(),
            (bundle + "/Contents/PlugIns\nargument with spaces\n'\"$()\n\n").toUtf8());

        QFile before(script);
        QVERIFY(before.open(QIODevice::ReadOnly));
        const QByteArray contents = before.readAll();
        before.close();
        QVERIFY(!SnapTray::installMacCLIWrapper(script, executable, bundle,
            [](const QString&) { return false; }));
        QVERIFY(before.open(QIODevice::ReadOnly));
        QCOMPARE(before.readAll(), contents);
        before.close();
        QVERIFY(!SnapTray::installMacCLIWrapper(script, executable, bundle,
            [&](QString command) {
                // Fail after writing/chmodding the staged file, before rename.
                command.replace("/bin/mv -f", "false #");
                return shell(command);
            }));
        QVERIFY(before.open(QIODevice::ReadOnly));
        QCOMPARE(before.readAll(), contents);
        QCOMPARE(QDir(QFileInfo(script).absolutePath()).entryList({".snaptray-*"}, QDir::Files | QDir::Hidden).size(), 0);
        QVERIFY(!SnapTray::isMacCLIWrapperInstalled(script, executable + "-other", bundle));
#endif
    }
};
QTEST_GUILESS_MAIN(TestMacCLIInstaller)
#include "tst_MacCLIInstaller.moc"
