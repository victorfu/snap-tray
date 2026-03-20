#include <QtTest/QtTest>

#include <QGuiApplication>

#include "PinWindow.h"
#include "annotations/PolylineAnnotation.h"
#include "cursor/CursorAuthority.h"
#include "cursor/CursorManager.h"
#include "pinwindow/RegionLayoutManager.h"
#include "tools/ToolManager.h"

namespace {
QPixmap createTestPixmap(int width = 160, int height = 120)
{
    QPixmap pixmap(width, height);
    pixmap.fill(Qt::red);
    return pixmap;
}
}  // namespace

class TestPinWindowStyleSync : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testUsesAuthorityModeByDefault();
    void testNonAnnotationEdgeHoverUsesCorrectResizeCursor();
    void testOverlayRestoreReturnsArrowToolCursor();
    void testPolylineReleaseRecomputesHoverCursor();
    void testPopupRestoreReturnsMosaicCursor();
    void testRegionLayoutMoveCursorUsesAuthority();
};

void TestPinWindowStyleSync::initTestCase()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("No screens available for PinWindow tests in this environment.");
    }
}

void TestPinWindowStyleSync::testUsesAuthorityModeByDefault()
{
    PinWindow window(createTestPixmap(), QPoint(0, 0));
    QCOMPARE(CursorAuthority::instance().modeForWidget(&window), CursorSurfaceMode::Authority);
}

void TestPinWindowStyleSync::testNonAnnotationEdgeHoverUsesCorrectResizeCursor()
{
    PinWindow window(createTestPixmap(240, 160), QPoint(0, 0));

    const QPoint leftPos(0, window.height() / 2);
    QMouseEvent leftMove(QEvent::MouseMove, leftPos, window.mapToGlobal(leftPos),
                         Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &leftMove);
    QCOMPARE(window.cursor().shape(), Qt::SizeHorCursor);

    const QPoint topPos(window.width() / 2, 0);
    QMouseEvent topMove(QEvent::MouseMove, topPos, window.mapToGlobal(topPos),
                        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &topMove);
    QCOMPARE(window.cursor().shape(), Qt::SizeVerCursor);

    const QPoint cornerPos(0, 0);
    QMouseEvent cornerMove(QEvent::MouseMove, cornerPos, window.mapToGlobal(cornerPos),
                           Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &cornerMove);
    QCOMPARE(window.cursor().shape(), Qt::SizeFDiagCursor);
}

void TestPinWindowStyleSync::testOverlayRestoreReturnsArrowToolCursor()
{
    PinWindow window(createTestPixmap(240, 160), QPoint(0, 0));
    auto& authority = CursorAuthority::instance();
    auto& cursorManager = CursorManager::instance();

    if (!window.m_toolManager) {
        window.initializeAnnotationComponents();
    }

    window.enterAnnotationMode();
    window.m_currentToolId = ToolId::Arrow;
    window.m_toolManager->setCurrentTool(ToolId::Arrow);

    const QPoint bodyPos(70, 40);
    window.restoreAnnotationCursorAt(bodyPos);
    QCOMPARE(window.cursor().shape(), Qt::CrossCursor);

    authority.submitWidgetRequest(
        &window, QStringLiteral("floating.overlay.toolbar"), CursorRequestSource::Overlay,
        CursorStyleSpec::fromShape(Qt::ArrowCursor));
    cursorManager.reapplyCursorForWidget(&window);
    QCOMPARE(window.cursor().shape(), Qt::ArrowCursor);

    authority.clearWidgetRequest(&window, QStringLiteral("floating.overlay.toolbar"));
    window.restoreAnnotationCursorAt(bodyPos);
    QCOMPARE(window.cursor().shape(), Qt::CrossCursor);
}

void TestPinWindowStyleSync::testPolylineReleaseRecomputesHoverCursor()
{
    PinWindow window(createTestPixmap(240, 160), QPoint(0, 0));

    if (!window.m_toolManager) {
        window.initializeAnnotationComponents();
    }

    window.enterAnnotationMode();
    window.m_currentToolId = ToolId::Arrow;
    window.m_toolManager->setCurrentTool(ToolId::Arrow);

    auto polyline = std::make_unique<PolylineAnnotation>(
        QVector<QPoint>{QPoint(40, 40), QPoint(100, 40), QPoint(120, 80)},
        Qt::green, 3);
    window.m_annotationLayer->addItem(std::move(polyline));
    window.m_annotationLayer->setSelectedIndex(0);

    const QPoint bodyPos(70, 40);
    QVERIFY(window.handlePolylineAnnotationPress(bodyPos));
    QVERIFY(window.m_isPolylineDragging);

    QVERIFY(window.handlePolylineAnnotationRelease(bodyPos));
    QCOMPARE(window.cursor().shape(), Qt::SizeAllCursor);
}

void TestPinWindowStyleSync::testPopupRestoreReturnsMosaicCursor()
{
    PinWindow window(createTestPixmap(), QPoint(0, 0));
    auto& authority = CursorAuthority::instance();
    auto& cursorManager = CursorManager::instance();

    if (!window.m_toolManager) {
        window.initializeAnnotationComponents();
    }

    window.m_toolManager->setCurrentTool(ToolId::Mosaic);
    window.m_toolManager->setWidth(20);
    cursorManager.updateToolCursorForWidget(&window);
    cursorManager.reapplyCursorForWidget(&window);

    const QCursor toolCursor = window.cursor();
    QVERIFY(!toolCursor.pixmap().isNull());

    authority.submitWidgetRequest(
        &window, QStringLiteral("floating.popup"), CursorRequestSource::Popup,
        CursorStyleSpec::fromShape(Qt::ArrowCursor));
    cursorManager.reapplyCursorForWidget(&window);
    QCOMPARE(window.cursor().shape(), Qt::ArrowCursor);

    authority.clearWidgetRequest(&window, QStringLiteral("floating.popup"));
    cursorManager.reapplyCursorForWidget(&window);

    QCOMPARE(window.cursor().pixmap().cacheKey(), toolCursor.pixmap().cacheKey());
    QCOMPARE(window.cursor().hotSpot(), toolCursor.hotSpot());
}

void TestPinWindowStyleSync::testRegionLayoutMoveCursorUsesAuthority()
{
    PinWindow window(createTestPixmap(240, 160), QPoint(0, 0));
    auto& authority = CursorAuthority::instance();

    LayoutRegion region;
    region.rect = QRect(20, 20, 80, 60);
    region.originalRect = region.rect;
    region.image = QImage(region.rect.size(), QImage::Format_ARGB32_Premultiplied);
    region.image.fill(Qt::blue);
    region.index = 1;
    region.color = Qt::green;

    window.setMultiRegionData({region});
    window.enterRegionLayoutMode();
    QVERIFY(window.isRegionLayoutMode());

    const QPoint localPos = region.rect.center();
    QMouseEvent moveEvent(QEvent::MouseMove, localPos, window.mapToGlobal(localPos),
                          Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &moveEvent);

    QCOMPARE(authority.resolvedSourceForWidget(&window), CursorRequestSource::LayoutMode);
    QCOMPARE(authority.resolvedStyleForWidget(&window).styleId, CursorStyleId::Move);
    QCOMPARE(window.cursor().shape(), Qt::SizeAllCursor);
}

QTEST_MAIN(TestPinWindowStyleSync)
#include "tst_StyleSync.moc"
