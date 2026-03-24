#include <QtTest/QtTest>

#include <QApplication>
#include <QQuickView>

#include "platform/QtQuickBackendPolicy.h"

class tst_QtQuickBackendWin : public QObject
{
    Q_OBJECT

private slots:
    void testSoftwarePolicyUsesSoftwareRenderer();
};

void tst_QtQuickBackendWin::testSoftwarePolicyUsesSoftwareRenderer()
{
#ifndef Q_OS_WIN
    QSKIP("Windows-only Qt Quick backend integration test.");
#else
    QQuickView view;
    view.resize(16, 16);
    view.show();
    QTest::qWait(50);

    QCOMPARE(QQuickWindow::sceneGraphBackend(), QStringLiteral("software"));
    QVERIFY(view.rendererInterface() != nullptr);
    QCOMPARE(view.rendererInterface()->graphicsApi(), QSGRendererInterface::Software);
#endif
}

int main(int argc, char** argv)
{
#ifdef Q_OS_WIN
    SnapTray::applyQtQuickGraphicsBackendPolicy(SnapTray::QtQuickGraphicsBackendPolicy::Software);
#endif

    QApplication app(argc, argv);
    tst_QtQuickBackendWin testCase;
    return QTest::qExec(&testCase, argc, argv);
}

#include "tst_QtQuickBackendWin.moc"
