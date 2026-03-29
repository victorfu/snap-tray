#include <QtTest/QtTest>

#include <QGuiApplication>
#include <QWindow>

#include "cursor/CursorSurfaceSupport.h"

class TestCursorSurfaceSupport : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testPointerRefreshClassification();
    void testTopLevelOcclusionDetection();
    void testWindowSurfaceAuthoritySync();
    void testWindowSurfaceExplicitOverride();
};

void TestCursorSurfaceSupport::initTestCase()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("No screens available for CursorSurfaceSupport tests in this environment.");
    }
}

void TestCursorSurfaceSupport::testPointerRefreshClassification()
{
    QVERIFY(CursorSurfaceSupport::isPointerRefreshEvent(QEvent::HoverMove));
    QVERIFY(CursorSurfaceSupport::isPointerRefreshEvent(QEvent::WindowActivate));
    QVERIFY(CursorSurfaceSupport::isPointerRefreshEvent(QEvent::FocusIn));
    QVERIFY(!CursorSurfaceSupport::isPointerRefreshEvent(QEvent::FocusOut));
    QVERIFY(!CursorSurfaceSupport::isPointerRefreshEvent(QEvent::WindowDeactivate));
    QVERIFY(!CursorSurfaceSupport::isPointerRefreshEvent(QEvent::Paint));
}

void TestCursorSurfaceSupport::testTopLevelOcclusionDetection()
{
    QWindow hostWindow;
    hostWindow.setGeometry(40, 40, 160, 120);
    hostWindow.show();

    QWindow overlayWindow;
    overlayWindow.setGeometry(70, 70, 80, 60);
    overlayWindow.show();

    QWindow transparentOverlay;
    transparentOverlay.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowTransparentForInput);
    transparentOverlay.setGeometry(150, 90, 30, 30);
    transparentOverlay.show();

    QCoreApplication::processEvents();

    QVERIFY(!CursorSurfaceSupport::isPointerOverOtherVisibleTopLevelWindow(
        &hostWindow, QPoint(50, 50)));
    QVERIFY(CursorSurfaceSupport::isPointerOverOtherVisibleTopLevelWindow(
        &hostWindow, QPoint(90, 90)));
    QVERIFY(!CursorSurfaceSupport::isPointerOverOtherVisibleTopLevelWindow(
        &hostWindow, QPoint(160, 100)));
    QVERIFY(!CursorSurfaceSupport::isPointerOverOtherVisibleTopLevelWindow(
        &overlayWindow, QPoint(90, 90)));

    overlayWindow.hide();
    QCoreApplication::processEvents();
    QVERIFY(!CursorSurfaceSupport::isPointerOverOtherVisibleTopLevelWindow(
        &hostWindow, QPoint(160, 100)));
}

void TestCursorSurfaceSupport::testWindowSurfaceAuthoritySync()
{
    QWindow window;
    window.setCursor(QCursor(Qt::CrossCursor));

    auto& authority = CursorAuthority::instance();
    const QString surfaceId = CursorSurfaceSupport::registerManagedSurface(
        &window, QStringLiteral("QmlSettingsWindow"));
    const QString ownerId = CursorSurfaceSupport::defaultOwnerId(QStringLiteral("QmlSettingsWindow"));

    QCOMPARE(authority.surfaceMode(surfaceId), CursorSurfaceMode::Authority);

    authority.submitRequest(
        surfaceId, QStringLiteral("popup"), CursorRequestSource::Popup,
        CursorStyleSpec::fromShape(Qt::ArrowCursor));
    CursorSurfaceSupport::syncWindowSurface(
        &window, surfaceId, ownerId, CursorRequestSource::SurfaceDefault);
    QCOMPARE(window.cursor().shape(), Qt::ArrowCursor);
    QCOMPARE(authority.resolvedSource(surfaceId), CursorRequestSource::Popup);

    authority.clearRequest(surfaceId, QStringLiteral("popup"));
    window.setCursor(QCursor(Qt::PointingHandCursor));
    CursorSurfaceSupport::syncWindowSurface(
        &window, surfaceId, ownerId, CursorRequestSource::SurfaceDefault);
    QCOMPARE(window.cursor().shape(), Qt::PointingHandCursor);
    QCOMPARE(authority.resolvedOwner(surfaceId), ownerId);

    CursorSurfaceSupport::clearWindowSurface(surfaceId, ownerId);
    authority.unregisterSurface(surfaceId);
}

void TestCursorSurfaceSupport::testWindowSurfaceExplicitOverride()
{
    QWindow window;
    window.setCursor(QCursor(Qt::ArrowCursor));

    auto& authority = CursorAuthority::instance();
    const QString surfaceId = CursorSurfaceSupport::registerManagedSurface(
        &window, QStringLiteral("QmlFloatingToolbar"));
    const QString ownerId = CursorSurfaceSupport::defaultOwnerId(QStringLiteral("QmlFloatingToolbar"));
    const CursorStyleSpec closedHandSpec = CursorStyleSpec::fromShape(Qt::ClosedHandCursor);

    CursorSurfaceSupport::syncWindowSurface(
        &window, surfaceId, ownerId, CursorRequestSource::Overlay, &closedHandSpec);
    QCOMPARE(window.cursor().shape(), Qt::ClosedHandCursor);
    QCOMPARE(authority.resolvedOwner(surfaceId), ownerId);
    QCOMPARE(authority.resolvedStyle(surfaceId).styleId, CursorStyleId::ClosedHand);

    window.setCursor(QCursor(Qt::PointingHandCursor));
    CursorSurfaceSupport::syncWindowSurface(
        &window, surfaceId, ownerId, CursorRequestSource::Overlay);
    QCOMPARE(window.cursor().shape(), Qt::PointingHandCursor);
    QCOMPARE(authority.resolvedOwner(surfaceId), ownerId);
    QCOMPARE(authority.resolvedStyle(surfaceId).styleId, CursorStyleId::PointingHand);

    CursorSurfaceSupport::clearWindowSurface(surfaceId, ownerId);
    authority.unregisterSurface(surfaceId);
}

QTEST_MAIN(TestCursorSurfaceSupport)
#include "tst_CursorSurfaceSupport.moc"
