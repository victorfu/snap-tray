#include <QtTest/QtTest>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>

#include "ScreenCanvas.h"
#include "ScreenCanvasSession.h"
#include "qml/QmlFloatingSubToolbar.h"
#include "qml/QmlFloatingToolbar.h"

class TestScreenCanvasCopyExport : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testScreenSnapshotHidesSurfacesDuringCapture();
};

void TestScreenCanvasCopyExport::initTestCase()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("No screens available for ScreenCanvas copy export tests.");
    }
}

void TestScreenCanvasCopyExport::testScreenSnapshotHidesSurfacesDuringCapture()
{
    ScreenCanvasSession session;
    session.m_qmlToolbar.reset();
    session.m_qmlSubToolbar.reset();
    session.m_isOpen = true;
    session.m_bgMode = CanvasBackgroundMode::Screen;

    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen != nullptr);

    ScreenCanvas surface(&session);
    surface.initializeForScreenSurface(
        screen,
        ScreenCanvasSession::virtualDesktopGeometry(QGuiApplication::screens()));
    surface.setSharedToolManager(session.m_toolManager);

    session.m_surfaces = {&surface};
    session.m_activeSurface = &surface;
    session.m_activationScreen = screen;

    surface.show();
    QCoreApplication::processEvents();
    QVERIFY(surface.isVisible());

    bool snapshotSawHiddenSurface = false;
    bool snapshotUsedExpectedScreen = false;
    session.m_screenSnapshotProvider = [&](QScreen* targetScreen) {
        snapshotUsedExpectedScreen = (targetScreen == screen);
        snapshotSawHiddenSurface = !surface.isVisible();

        QPixmap pixmap(targetScreen->geometry().size());
        pixmap.fill(Qt::black);
        pixmap.setDevicePixelRatio(targetScreen->devicePixelRatio());
        return pixmap;
    };

    const QPixmap snapshot = session.buildCopyExportBasePixmap(screen);

    QVERIFY(!snapshot.isNull());
    QVERIFY(snapshotUsedExpectedScreen);
    QVERIFY(snapshotSawHiddenSurface);
    QVERIFY(surface.isVisible());

    session.m_surfaces.clear();
    session.m_activeSurface = nullptr;
    session.m_activationScreen = nullptr;
    session.m_isOpen = false;
}

QTEST_MAIN(TestScreenCanvasCopyExport)
#include "tst_CopyExport.moc"
