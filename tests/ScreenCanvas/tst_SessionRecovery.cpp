#include <QtTest/QtTest>

#include <QGuiApplication>
#include <QKeyEvent>
#include <QPaintEvent>

#include "ScreenCanvas.h"
#include "ScreenCanvasSession.h"
#include "qml/QmlFloatingSubToolbar.h"
#include "qml/QmlFloatingToolbar.h"
#include "tools/IToolHandler.h"
#include "tools/ToolManager.h"

namespace {

class TrackingScreenCanvas : public ScreenCanvas
{
public:
    using ScreenCanvas::ScreenCanvas;

    QRect lastPaintBounds;

protected:
    void paintEvent(QPaintEvent* event) override
    {
        lastPaintBounds = event ? event->region().boundingRect() : QRect();
        ScreenCanvas::paintEvent(event);
    }
};

class FakeScreenCanvasInteractionEscapeHandler : public IToolHandler
{
public:
    ToolId toolId() const override
    {
        return ToolId::Text;
    }

    bool handleEscape(ToolContext* ctx) override
    {
        Q_UNUSED(ctx);
        m_active = false;
        return true;
    }

    QRect interactionBounds(const ToolContext* ctx) const override
    {
        Q_UNUSED(ctx);
        return m_active ? QRect(20, 30, 40, 50) : QRect();
    }

    AnnotationInteractionKind activeInteractionKind(const ToolContext* ctx) const override
    {
        Q_UNUSED(ctx);
        return m_active
            ? AnnotationInteractionKind::SelectedTransform
            : AnnotationInteractionKind::None;
    }

private:
    bool m_active = true;
};

} // namespace

class TestScreenCanvasSessionRecovery : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testApplicationActiveRestoresAllSurfaces();
    void testEscapeWithHandledInteractionDoesNotForceFullSurfaceRepaint();
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

void TestScreenCanvasSessionRecovery::testEscapeWithHandledInteractionDoesNotForceFullSurfaceRepaint()
{
    ScreenCanvasSession session;
    session.m_qmlToolbar.reset();
    session.m_qmlSubToolbar.reset();
    session.m_isOpen = true;

    session.m_toolManager->registerHandler(std::make_unique<FakeScreenCanvasInteractionEscapeHandler>());
    session.m_toolManager->setCurrentTool(ToolId::Pencil);

    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);
    const QRect firstDesktopGeometry = screen->geometry();
    const QRect secondDesktopGeometry = screen->geometry().translated(-screen->geometry().width(), 0);
    session.m_desktopGeometry = firstDesktopGeometry.united(secondDesktopGeometry);

    TrackingScreenCanvas firstSurface(&session);
    TrackingScreenCanvas secondSurface(&session);
    firstSurface.setSharedToolManager(session.m_toolManager);
    secondSurface.setSharedToolManager(session.m_toolManager);
    firstSurface.initializeForScreenSurface(screen, firstDesktopGeometry);
    secondSurface.initializeForScreenSurface(screen, secondDesktopGeometry);

    session.m_surfaces = {&firstSurface, &secondSurface};
    session.m_activeSurface = &firstSurface;

    firstSurface.show();
    secondSurface.show();
    QCoreApplication::processEvents();
    firstSurface.lastPaintBounds = QRect();
    secondSurface.lastPaintBounds = QRect();

    QKeyEvent event(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    session.handleSurfaceKeyPress(&firstSurface, &event);
    QCoreApplication::processEvents();

    QVERIFY(firstSurface.lastPaintBounds.isValid());
    QVERIFY(!secondSurface.lastPaintBounds.isValid());

    session.m_surfaces.clear();
    session.m_activeSurface = nullptr;
    session.m_isOpen = false;
}

QTEST_MAIN(TestScreenCanvasSessionRecovery)
#include "tst_SessionRecovery.moc"
