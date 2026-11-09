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
    void testActivateSurfaceSkipsRedundantSynchronization();
    void testApplicationActiveRestoresAllSurfaces();
};

void TestScreenCanvasSessionRecovery::initTestCase()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("No screens available for ScreenCanvas recovery tests in this environment.");
    }
}

void TestScreenCanvasSessionRecovery::testActivateSurfaceSkipsRedundantSynchronization()
{
    ScreenCanvasSession session;
    session.m_qmlToolbar.reset();
    session.m_qmlSubToolbar.reset();

    ScreenCanvas firstSurface(&session);
    ScreenCanvas secondSurface(&session);
    firstSurface.setSharedToolManager(session.m_toolManager);
    secondSurface.setSharedToolManager(session.m_toolManager);

    // Surface creation selects the active surface before activateSurface() is
    // called. The first call must still initialize the shared tool context.
    session.m_activeSurface = &firstSurface;
    QVERIFY(session.activateSurface(&firstSurface));

    // Pencil mouse moves remain on the grabbed surface and must take the fast
    // path instead of rebinding editors, DPR, and floating UI ownership.
    QVERIFY(!session.activateSurface(&firstSurface));
    QVERIFY(!session.activateSurface(&firstSurface));

    // A real surface transition performs one synchronization, then becomes the
    // new fast-path surface. Switching back must synchronize again.
    QVERIFY(session.activateSurface(&secondSurface));
    QVERIFY(!session.activateSurface(&secondSurface));
    QVERIFY(session.activateSurface(&firstSurface));

    session.m_activeSurface = nullptr;
    session.m_activationSyncedSurface = nullptr;
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
    ScreenCanvas* activeSurface = session.m_activeSurface.data();
    QVERIFY(activeSurface == &firstSurface || activeSurface == &secondSurface);
    QVERIFY(activeSurface->isVisible());

    session.m_surfaces.clear();
    session.m_activeSurface = nullptr;
    session.m_isOpen = false;
}

QTEST_MAIN(TestScreenCanvasSessionRecovery)
#include "tst_SessionRecovery.moc"
