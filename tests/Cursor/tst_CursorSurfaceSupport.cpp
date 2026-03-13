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
    void testWindowSurfaceAuthoritySync();
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

QTEST_MAIN(TestCursorSurfaceSupport)
#include "tst_CursorSurfaceSupport.moc"
