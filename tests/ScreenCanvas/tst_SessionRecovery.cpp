#include <QtTest/QtTest>

#include <QGuiApplication>

#include "ScreenCanvas.h"
#include "ScreenCanvasSession.h"
#include "qml/QmlFloatingSubToolbar.h"
#include "qml/QmlFloatingToolbar.h"

class TestScreenCanvasSessionRecovery : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testApplicationActiveRestoresAllSurfaces();
};

void TestScreenCanvasSessionRecovery::initTestCase()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("No screens available for ScreenCanvas recovery tests in this environment.");
    }
}

void TestScreenCanvasSessionRecovery::testApplicationActiveRestoresAllSurfaces()
{
    ScreenCanvasSession session;
    session.m_qmlToolbar.reset();
    session.m_qmlSubToolbar.reset();
    session.m_isOpen = true;

    ScreenCanvas firstSurface(&session);
    ScreenCanvas secondSurface(&session);
    firstSurface.setSharedToolManager(session.m_toolManager);
    secondSurface.setSharedToolManager(session.m_toolManager);

    session.m_surfaces = {&firstSurface, &secondSurface};
    session.m_activeSurface = nullptr;

    firstSurface.show();
    secondSurface.show();
    QCoreApplication::processEvents();

    firstSurface.hide();
    secondSurface.hide();
    QVERIFY(!firstSurface.isVisible());
    QVERIFY(!secondSurface.isVisible());

    session.handleApplicationStateChanged(Qt::ApplicationInactive);
    QVERIFY(!firstSurface.isVisible());
    QVERIFY(!secondSurface.isVisible());

    session.handleApplicationStateChanged(Qt::ApplicationActive);
    QCoreApplication::processEvents();

    QVERIFY(firstSurface.isVisible());
    QVERIFY(secondSurface.isVisible());
    QCOMPARE(session.m_activeSurface.data(), &firstSurface);

    session.m_surfaces.clear();
    session.m_activeSurface = nullptr;
    session.m_isOpen = false;
}

QTEST_MAIN(TestScreenCanvasSessionRecovery)
#include "tst_SessionRecovery.moc"
