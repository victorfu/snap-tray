#include <QtTest>
#include <QFileInfo>
#include <QFile>
#include <QUuid>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include "platform/LinuxClipboardOwner.h"

class TestLinuxClipboardOwner : public QObject
{
    Q_OBJECT
private slots:
    void rejectsInvalidInput()
    {
        QVERIFY(!SnapTray::copyImageToLinuxClipboard({}, QStringLiteral("missing")));
        QImage image(2, 2, QImage::Format_RGB32);
        image.fill(Qt::red);
        QVERIFY(!SnapTray::copyImageToLinuxClipboard(image, QStringLiteral("/nonexistent/snaptray"), 100));
        QVERIFY(SnapTray::isLinuxClipboardOwnerRequest({"app", SnapTray::kClipboardOwnerArgument}));
        QVERIFY(!SnapTray::isLinuxClipboardOwnerRequest({"app", "full"}));
        QCOMPARE(SnapTray::runLinuxClipboardOwner({"app", SnapTray::kClipboardOwnerArgument}), 1);
#ifdef Q_OS_LINUX
        // Successful process creation alone is not a clipboard acknowledgement.
        QVERIFY(!SnapTray::copyImageToLinuxClipboard(image, QStringLiteral("/bin/true"), 100));
#endif
    }
    void survivesCLIExitAndReleasesOwnership()
    {
#ifndef Q_OS_LINUX
        QSKIP("Requires Linux X11");
#else
        if (qEnvironmentVariable("SNAPTRAY_TEST_X11_CLIPBOARD") != "1") {
            QSKIP("Run on an isolated Xvfb display with SNAPTRAY_TEST_X11_CLIPBOARD=1");
        }
        QTemporaryDir dir;
        const QString firstExit = dir.filePath("red-owner-exit");
        const QString secondExit = dir.filePath("blue-owner-exit");
        const QString probe = QString::fromUtf8(CLIPBOARD_PROBE_PATH);
        const auto copy = [&](const QString& color, const QString& marker) {
            QProcess process;
            auto env = QProcessEnvironment::systemEnvironment();
            env.remove("APPIMAGE");
            env.insert("SNAPTRAY_CLIPBOARD_PROBE_EXIT_FILE", marker);
            process.setProcessEnvironment(env);
            process.start(probe, {"--copy", color});
            return process.waitForFinished(15000) && process.exitCode() == 0;
        };
        const auto read = [&] {
            QProcess process;
            process.start(probe, {"--read"});
            if (!process.waitForFinished(5000) || process.exitCode() != 0) return QByteArray();
            return process.readAllStandardOutput().trimmed();
        };
        QVERIFY(copy("red", firstExit));
        QTest::qWait(1500);
        QCOMPARE(read(), QByteArray("#ff0000"));
        QVERIFY(!QFileInfo::exists(firstExit));
        QVERIFY(copy("blue", secondExit));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(firstExit), 3000);
        QCOMPARE(read(), QByteArray("#0000ff"));
        QVERIFY(!QFileInfo::exists(secondExit));
        // A clipboard manager can take a copy and become the new owner.
        QProcess manager;
        manager.start(probe, {"--manager"});
        QVERIFY(manager.waitForReadyRead(5000));
        QCOMPARE(manager.readAllStandardOutput().trimmed(), QByteArray("owned"));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(secondExit), 3000);
        QCOMPARE(read(), QByteArray("#0000ff"));
        manager.terminate();
        QVERIFY(manager.waitForFinished(5000));
#endif
    }
    void usesAppImageLauncher()
    {
#ifndef Q_OS_LINUX
        QSKIP("Requires Linux X11");
#else
        if (qEnvironmentVariable("SNAPTRAY_TEST_X11_CLIPBOARD") != "1") {
            QSKIP("Requires an isolated Xvfb display");
        }
        QTemporaryDir dir;
        const QString launcher = dir.filePath("Launcher 'quoted'.AppImage");
        const QString launched = dir.filePath("launched");
        const QString ownerExited = dir.filePath("owner-exited");
        QFile script(launcher);
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("#!/bin/sh\nprintf invoked > \"$SNAPTRAY_PROBE_LAUNCHED\"\nexec \"$SNAPTRAY_PROBE_EXECUTABLE\" \"$@\"\n");
        script.close();
        QVERIFY(script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QString probe = QString::fromUtf8(CLIPBOARD_PROBE_PATH);
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("APPIMAGE", launcher);
        env.insert("SNAPTRAY_PROBE_EXECUTABLE", probe);
        env.insert("SNAPTRAY_PROBE_LAUNCHED", launched);
        env.insert("SNAPTRAY_CLIPBOARD_PROBE_EXIT_FILE", ownerExited);
        QProcess copy;
        copy.setProcessEnvironment(env);
        copy.start(probe, {"--copy", "green"});
        QVERIFY(copy.waitForFinished(15000));
        QCOMPARE(copy.exitCode(), 0);
        QVERIFY(QFileInfo::exists(launched));
        QVERIFY(!QFileInfo::exists(ownerExited));
        QProcess manager;
        manager.start(probe, {"--manager"});
        QVERIFY(manager.waitForReadyRead(5000));
        QCOMPARE(manager.readAllStandardOutput().trimmed(), QByteArray("owned"));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(ownerExited), 3000);
        manager.terminate();
        QVERIFY(manager.waitForFinished(5000));
#endif
    }
};
QTEST_GUILESS_MAIN(TestLinuxClipboardOwner)
#include "tst_LinuxClipboardOwner.moc"
