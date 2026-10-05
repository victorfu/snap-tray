#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <QQmlEngine>
#include <QQuickView>
#include <QQuickItem>
#include <QQmlComponent>
#include <QtQml/qqmlextensionplugin.h>
#include "update/AppImageUpdateService.h"
#include "qml/QmlDialog.h"
Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

class TestAppImageUpdateService : public QObject
{
    Q_OBJECT
    QTemporaryDir directory;
    QString helper(const QByteArray& body)
    {
        const auto path = directory.filePath("test helper");
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return {};
        file.write("#!/bin/sh\n" + body); file.close();
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return path;
    }
private slots:
    void progressAndReady()
    {
        AppImageUpdateService service;
        service.m_helper = helper("printf '%s\\n' '{\"event\":\"progress\",\"progress\":0.5}' '{\"event\":\"ready\",\"version\":\"1.2.3\"}'\n");
        service.setState("available"); service.download();
        QVERIFY(service.isBusy());
        QTRY_COMPARE(service.state(), QString("ready"));
        QCOMPARE(service.version(), QString("1.2.3"));
        QCOMPARE(service.progress(), 0.5);
        QVERIFY(!service.isBusy());
    }
    void preserveFailureReason()
    {
        AppImageUpdateService service;
        service.m_helper = helper("printf '%s\\n' '{\"event\":\"error\",\"message\":\"Bad signature\"}'; exit 1\n");
        service.setState("available"); service.download();
        QTRY_COMPARE(service.m_process.state(), QProcess::NotRunning);
        QCOMPARE(service.state(), QString("error"));
        QCOMPARE(service.message(), QString("Bad signature"));
    }
    void noSuccessWithoutTerminalEvent()
    {
        AppImageUpdateService service;
        service.m_helper = helper("exit 0\n");
        service.setState("available"); service.download();
        QTRY_COMPARE(service.state(), QString("error"));
    }
    void cancelledDownload()
    {
        AppImageUpdateService service;
        service.m_helper = helper("exec sleep 20\n");
        service.setState("available"); service.download();
        QVERIFY(service.m_process.waitForStarted());
        service.cancel();
        QTRY_COMPARE(service.state(), QString("available"));
        QVERIFY(!service.isBusy());
    }
    void closingDialogKeepsServiceAlive()
    {
        AppImageUpdateService service;
        QPointer<AppImageUpdateService> guard(&service);
        service.showDialog();
        QVERIFY(!service.parent());
        service.close();
        QTRY_VERIFY(!service.m_dialog);
        QVERIFY(guard);
    }
    void renderDialog()
    {
        AppImageUpdateService service;
        service.m_candidate = {{"version", "1.2.3"}, {"notes", "What is new\n\nScreenshot improvements and bug fixes.\n\nYour settings will be preserved."}};
        service.setState("available", "SnapTray 1.2.3 is available.");
        QQuickView view;
        view.setInitialProperties({{"viewModel", QVariant::fromValue(&service)}});
        view.setSource(QUrl("qrc:/SnapTrayQml/dialogs/AppImageUpdateDialog.qml"));
        QCOMPARE(view.status(), QQuickView::Ready);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTest::qWait(100);
        const auto snapshot = qEnvironmentVariable("SNAPTRAY_UPDATE_TEST_SCREENSHOT");
        if (!snapshot.isEmpty()) QVERIFY(view.grabWindow().save(snapshot));
    }
    void qmlLoadsAllStates()
    {
        AppImageUpdateService service;
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl("qrc:/SnapTrayQml/dialogs/AppImageUpdateDialog.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.createWithInitialProperties({{"viewModel", QVariant::fromValue(&service)}}));
        QVERIFY2(root != nullptr, qPrintable(component.errorString()));
        for (const QString state : {"checking", "available", "downloading", "verifying", "ready", "error"}) {
            service.setState(state, "Test message");
            QCoreApplication::processEvents();
            QVERIFY(root->property("height").toDouble() > 0);
        }
    }
};
QTEST_MAIN(TestAppImageUpdateService)
#include "tst_AppImageUpdateService.moc"
