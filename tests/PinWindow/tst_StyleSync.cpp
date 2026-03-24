#include <QtTest/QtTest>

#include <QGuiApplication>

#include "PinWindow.h"
#include "qml/QmlEmojiPickerPopup.h"
#include "annotations/PolylineAnnotation.h"
#include "cursor/CursorAuthority.h"
#include "cursor/CursorManager.h"
#include "pinwindow/RegionLayoutManager.h"
#include "tools/ToolManager.h"

namespace {
constexpr int kToolbarOutsideClickGuardMs = 350;

QPixmap createTestPixmap(int width = 160, int height = 120)
{
    QPixmap pixmap(width, height);
    pixmap.fill(Qt::red);
    return pixmap;
}

class HeadlessEmojiPickerPopup final : public SnapTray::QmlEmojiPickerPopup
{
public:
    explicit HeadlessEmojiPickerPopup(QObject* parent = nullptr)
        : QmlEmojiPickerPopup(parent)
    {
    }

    void positionAt(const QRect& anchorRect) override
    {
        const QPoint topLeft = anchorRect.isValid() ? anchorRect.topLeft() : QPoint();
        m_geometry = QRect(topLeft, QSize(1, 1));
    }

    void showAt(const QRect& anchorRect) override
    {
        positionAt(anchorRect);
        m_visible = true;
    }

    void hide() override
    {
        m_visible = false;
    }

    void close() override
    {
        m_visible = false;
    }

    bool isVisible() const override
    {
        return m_visible;
    }

    QRect geometry() const override
    {
        return m_geometry;
    }

    QWindow* window() const override
    {
        return nullptr;
    }

private:
    bool m_visible = false;
    QRect m_geometry;
};
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
    void testTemporaryToolbarHideRestoresEmojiPicker();
    void testExplicitToolbarHideClearsEmojiToolState();
    void testOutsideClickHidesEmojiPickerWithToolbar();
    void testApplicationDeactivateHidesEmojiPickerWithToolbar();
    void testClickingEmojiPopupDoesNotCloseToolbar();
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

void TestPinWindowStyleSync::testTemporaryToolbarHideRestoresEmojiPicker()
{
    PinWindow window(createTestPixmap(), QPoint(0, 0));

    if (!window.m_toolManager) {
        window.initializeAnnotationComponents();
    }

    auto* popup = new HeadlessEmojiPickerPopup(&window);
    popup->showAt(QRect(QPoint(10, 10), QSize(1, 1)));
    window.m_emojiPickerPopup = popup;

    window.showToolbar();
    QVERIFY(window.isToolbarVisible());

    window.enterAnnotationMode();
    window.m_currentToolId = ToolId::EmojiSticker;
    window.m_toolManager->setCurrentTool(ToolId::EmojiSticker);

    QVERIFY(window.m_emojiPickerPopup->isVisible());

    window.hideToolbarPreservingToolState();
    QVERIFY(!window.isToolbarVisible());
    QVERIFY(!window.m_emojiPickerPopup->isVisible());

    window.showToolbar();
    QVERIFY(window.isToolbarVisible());
    QVERIFY(window.m_emojiPickerPopup->isVisible());
    QCOMPARE(window.m_currentToolId, ToolId::EmojiSticker);
}

void TestPinWindowStyleSync::testExplicitToolbarHideClearsEmojiToolState()
{
    PinWindow window(createTestPixmap(), QPoint(0, 0));

    if (!window.m_toolManager) {
        window.initializeAnnotationComponents();
    }

    auto* popup = new HeadlessEmojiPickerPopup(&window);
    popup->showAt(QRect(QPoint(10, 10), QSize(1, 1)));
    window.m_emojiPickerPopup = popup;

    window.showToolbar();
    window.enterAnnotationMode();
    window.m_currentToolId = ToolId::EmojiSticker;
    window.m_toolManager->setCurrentTool(ToolId::EmojiSticker);

    window.hideToolbar();
    QVERIFY(!window.isToolbarVisible());
    QCOMPARE(window.m_currentToolId, ToolId::Selection);

    window.showToolbar();
    QVERIFY(window.isToolbarVisible());
    QVERIFY(!window.m_emojiPickerPopup->isVisible());
    QCOMPARE(window.m_currentToolId, ToolId::Selection);
}

void TestPinWindowStyleSync::testOutsideClickHidesEmojiPickerWithToolbar()
{
    PinWindow window(createTestPixmap(), QPoint(0, 0));

    if (!window.m_toolManager) {
        window.initializeAnnotationComponents();
    }

    auto* popup = new HeadlessEmojiPickerPopup(&window);
    window.m_emojiPickerPopup = popup;

    window.showToolbar();
    window.handleToolbarToolSelected(static_cast<int>(ToolId::EmojiSticker));

    QVERIFY(window.isToolbarVisible());
    QVERIFY(window.m_annotationMode);
    QVERIFY(window.m_emojiPickerPopup->isVisible());

    QWidget outsideTarget;
    outsideTarget.setAttribute(Qt::WA_DontShowOnScreen, true);
    outsideTarget.setGeometry(window.frameGeometry().right() + 200,
                              window.frameGeometry().bottom() + 200,
                              40, 40);
    outsideTarget.show();
    QTest::qWait(kToolbarOutsideClickGuardMs);

    const QPoint localPos(5, 5);
    const QPoint globalPos = outsideTarget.mapToGlobal(localPos);
    QMouseEvent pressEvent(QEvent::MouseButtonPress,
                           localPos,
                           globalPos,
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
    QCoreApplication::sendEvent(&outsideTarget, &pressEvent);
    QCoreApplication::processEvents();

    QTRY_VERIFY(!window.isToolbarVisible());
    QTRY_VERIFY(!window.m_emojiPickerPopup->isVisible());
}

void TestPinWindowStyleSync::testApplicationDeactivateHidesEmojiPickerWithToolbar()
{
    PinWindow window(createTestPixmap(), QPoint(0, 0));

    if (!window.m_toolManager) {
        window.initializeAnnotationComponents();
    }

    auto* popup = new HeadlessEmojiPickerPopup(&window);
    window.m_emojiPickerPopup = popup;

    window.showToolbar();
    window.handleToolbarToolSelected(static_cast<int>(ToolId::EmojiSticker));

    QVERIFY(window.isToolbarVisible());
    QVERIFY(window.m_annotationMode);
    QVERIFY(window.m_emojiPickerPopup->isVisible());

    window.handleApplicationStateChanged(Qt::ApplicationInactive);

    QVERIFY(!window.isToolbarVisible());
    QVERIFY(!window.m_emojiPickerPopup->isVisible());
    QCOMPARE(window.m_currentToolId, ToolId::EmojiSticker);
}

void TestPinWindowStyleSync::testClickingEmojiPopupDoesNotCloseToolbar()
{
    PinWindow window(createTestPixmap(), QPoint(0, 0));

    if (!window.m_toolManager) {
        window.initializeAnnotationComponents();
    }

    window.showToolbar();
    window.handleToolbarToolSelected(static_cast<int>(ToolId::EmojiSticker));
    QCoreApplication::processEvents();

    QVERIFY(window.isToolbarVisible());
    QVERIFY(window.m_emojiPickerPopup);
    QVERIFY(window.m_emojiPickerPopup->isVisible());

    QWindow* popupWindow = window.m_emojiPickerPopup->window();
    QVERIFY(popupWindow);
    QVERIFY(popupWindow->isVisible());

    const QPoint localPos = popupWindow->geometry().center() - popupWindow->geometry().topLeft();
    const QPoint globalPos = popupWindow->geometry().center();
    QMouseEvent pressEvent(QEvent::MouseButtonPress,
                           localPos,
                           globalPos,
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
    QCoreApplication::sendEvent(popupWindow, &pressEvent);
    QCoreApplication::processEvents();

    QVERIFY(window.isToolbarVisible());
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
