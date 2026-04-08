#include <QtTest/QtTest>

#include <memory>

#include <QClipboard>
#include <QGuiApplication>
#include <QScreen>
#include "RegionSelector.h"
#include "RegionSelectorTestAccess.h"
#include "annotations/ArrowAnnotation.h"
#include "cursor/CursorAuthority.h"
#include "cursor/CursorManager.h"
#include "cursor/CursorStyleCatalog.h"
#include "settings/RegionCaptureSettingsManager.h"
#include "region/RegionInputHandler.h"
#include "tools/ToolManager.h"

namespace {
const QRect kSelectionRect(40, 40, 160, 120);
const QPoint kSelectionBodyPos(180, 140);
const QPoint kArrowControlHandlePos(100, 80);
const QPoint kArrowDraggedControlPos(110, 86);

void verifyMoveCursor(const QCursor& cursor)
{
#ifdef Q_OS_MACOS
    const QCursor expected =
        CursorStyleCatalog::instance().cursorForStyle(CursorStyleSpec::fromShape(Qt::SizeAllCursor));
    QCOMPARE(cursor.shape(), Qt::BitmapCursor);
    QVERIFY(!cursor.pixmap().isNull());
    QCOMPARE(cursor.hotSpot(), expected.hotSpot());
    QCOMPARE(cursor.pixmap().deviceIndependentSize(),
             expected.pixmap().deviceIndependentSize());
#else
    QCOMPARE(cursor.shape(), Qt::SizeAllCursor);
#endif
}

bool isMoveCursor(const QCursor& cursor)
{
#ifdef Q_OS_MACOS
    const QCursor expected =
        CursorStyleCatalog::instance().cursorForStyle(CursorStyleSpec::fromShape(Qt::SizeAllCursor));
    return cursor.shape() == Qt::BitmapCursor &&
           !cursor.pixmap().isNull() &&
           cursor.hotSpot() == expected.hotSpot() &&
           cursor.pixmap().deviceIndependentSize() ==
               expected.pixmap().deviceIndependentSize();
#else
    return cursor.shape() == Qt::SizeAllCursor;
#endif
}
}  // namespace

class TestRegionSelectorStyleSync : public QObject
{
    Q_OBJECT

private:
    static void prepareSelectionTool(RegionSelector& selector);
    static void addSelectedCurvedArrow(RegionSelector& selector);
    static QRect showToolbarForTool(RegionSelector& selector, ToolId tool);

private slots:
    void testUsesAuthorityModeByDefault();
    void testSelectionBodyHoverUsesMoveCursor();
    void testSelectionBodyHoverUsesEventPosWhenLiveCursorLags();
    void testSelectionDragUsesClosedHandCursor();
    void testOverlayRequestRestoreReturnsArrowToolCursor();
    void testFloatingToolbarWindowOwnsArrowCursor();
    void testToolbarLeaveRestoresArrowToolCrossCursor();
    void testToolbarLeaveRestoresSelectionBodyMoveCursor();
    void testArrowControlReleaseRestoresSelectionBodyCursor();
    void testRestoreRegionCursorAfterArrowControlHoverReturnsSelectionBodyCursor();
    void testPopupRestoreReturnsSelectionBodyCursor();
    void testPopupRestoreReturnsMosaicCursor();
    void testInitializeForScreen_DisabledMagnifierPreventsVisibility();
    void testInitializeForScreen_BeaverStyleDisablesMagnifierMode();
    void testInitializeForScreen_BeaverStyleSkipsMagnifierPrewarm();
    void testInitializeForScreen_MagnifierStylePrewarmsCache();
    void testDisabledMagnifierIgnoresShiftAndCopyShortcuts();
    void testBeaverStyleIgnoresShiftAndCopyShortcuts();
};

void TestRegionSelectorStyleSync::prepareSelectionTool(RegionSelector& selector)
{
    selector.m_selectionManager->setSelectionRect(kSelectionRect);
    selector.m_inputState.currentTool = ToolId::Selection;
    selector.m_toolManager->setCurrentTool(ToolId::Selection);
}

void TestRegionSelectorStyleSync::addSelectedCurvedArrow(RegionSelector& selector)
{
    auto arrow = std::make_unique<ArrowAnnotation>(
        QPoint(60, 100), QPoint(140, 100), Qt::green, 3);
    arrow->setControlPoint(QPoint(100, 60));
    selector.m_annotationLayer->addItem(std::move(arrow));
    selector.m_annotationLayer->setSelectedIndex(0);
}

QRect TestRegionSelectorStyleSync::showToolbarForTool(RegionSelector& selector, ToolId tool)
{
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        return {};
    }

    selector.setGeometry(screen->geometry());
    selector.m_selectionManager->setSelectionRect(kSelectionRect);
    selector.m_inputState.currentTool = tool;
    selector.m_toolManager->setCurrentTool(tool);
    selector.syncDetachedSelectionUiDuringPaint();
    QCoreApplication::processEvents();
    return RegionSelectorTestAccess::toolbarGeometry(selector);
}

void TestRegionSelectorStyleSync::testUsesAuthorityModeByDefault()
{
    RegionSelector selector;
    QCOMPARE(CursorAuthority::instance().modeForWidget(&selector), CursorSurfaceMode::Authority);
}

void TestRegionSelectorStyleSync::testSelectionBodyHoverUsesMoveCursor()
{
    RegionSelector selector;
    auto& cursorManager = CursorManager::instance();

    prepareSelectionTool(selector);

    selector.m_inputHandler->syncHoverCursorAt(kSelectionBodyPos);
    cursorManager.reapplyCursorForWidget(&selector);

    verifyMoveCursor(selector.cursor());
}

void TestRegionSelectorStyleSync::testSelectionBodyHoverUsesEventPosWhenLiveCursorLags()
{
    RegionSelector selector;

    prepareSelectionTool(selector);
    selector.setGeometry(0, 0, 320, 240);

    const QPoint originalCursorPos = QCursor::pos();
    const QPoint laggedCursorPos = selector.mapToGlobal(QPoint(1, 1));
    QCursor::setPos(laggedCursorPos);
    if (QCursor::pos() != laggedCursorPos) {
        QSKIP("System cursor position could not be adjusted for live-cursor lag test.");
    }

    RegionSelectorTestAccess::dispatchWidgetMouseMove(selector, kSelectionBodyPos);

    verifyMoveCursor(selector.cursor());

    QCursor::setPos(originalCursorPos);
}

void TestRegionSelectorStyleSync::testSelectionDragUsesClosedHandCursor()
{
    RegionSelector selector;

    prepareSelectionTool(selector);

    QMouseEvent pressEvent(QEvent::MouseButtonPress, kSelectionBodyPos, kSelectionBodyPos,
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    selector.m_inputHandler->handleMousePress(&pressEvent);

    QCOMPARE(selector.cursor().shape(), Qt::ClosedHandCursor);

    QMouseEvent releaseEvent(QEvent::MouseButtonRelease, kSelectionBodyPos, kSelectionBodyPos,
                             Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    selector.m_inputHandler->handleMouseRelease(&releaseEvent);
}

void TestRegionSelectorStyleSync::testOverlayRequestRestoreReturnsArrowToolCursor()
{
    RegionSelector selector;
    auto& authority = CursorAuthority::instance();
    auto& cursorManager = CursorManager::instance();

    selector.m_selectionManager->setSelectionRect(kSelectionRect);
    selector.m_inputState.currentTool = ToolId::Arrow;
    selector.m_toolManager->setCurrentTool(ToolId::Arrow);
    selector.restoreRegionCursorAt(kSelectionBodyPos);

    QCOMPARE(selector.cursor().shape(), Qt::CrossCursor);

    authority.submitWidgetRequest(
        &selector, QStringLiteral("floating.overlay.test"), CursorRequestSource::Overlay,
        CursorStyleSpec::fromShape(Qt::ArrowCursor));
    cursorManager.reapplyCursorForWidget(&selector);
    QCOMPARE(selector.cursor().shape(), Qt::ArrowCursor);

    authority.clearWidgetRequest(&selector, QStringLiteral("floating.overlay.test"));
    selector.restoreRegionCursorAt(kSelectionBodyPos);
    QCOMPARE(selector.cursor().shape(), Qt::CrossCursor);
}

void TestRegionSelectorStyleSync::testFloatingToolbarWindowOwnsArrowCursor()
{
    RegionSelector selector;
    auto& authority = CursorAuthority::instance();

    const QRect toolbarGeometry = showToolbarForTool(selector, ToolId::Arrow);
    if (!toolbarGeometry.isValid() || toolbarGeometry.isEmpty()) {
        QSKIP("Floating toolbar did not produce a valid geometry in this environment.");
    }

    QWindow* toolbarWindow = RegionSelectorTestAccess::toolbarWindow(selector);
    if (!toolbarWindow) {
        QSKIP("Floating toolbar window did not initialize in this environment.");
    }

    const QPoint originalCursorPos = QCursor::pos();
    const QPoint toolbarPos = toolbarGeometry.center();
    QCursor::setPos(toolbarPos);
    if (QCursor::pos() != toolbarPos) {
        QSKIP("System cursor position could not be adjusted for toolbar hover test.");
    }

    RegionSelectorTestAccess::dispatchWindowEnter(toolbarWindow, toolbarPos);

    QCOMPARE(authority.resolvedStyleForWindow(toolbarWindow).styleId, CursorStyleId::Arrow);
    QCOMPARE(selector.cursor().shape(), Qt::CrossCursor);
    QVERIFY(authority.resolvedOwner(authority.surfaceIdForWidget(&selector)) !=
            QStringLiteral("floating.overlay.toolbar"));

    QCursor::setPos(originalCursorPos);
}

void TestRegionSelectorStyleSync::testToolbarLeaveRestoresArrowToolCrossCursor()
{
    RegionSelector selector;
    auto& cursorManager = CursorManager::instance();

    const QRect toolbarGeometry = showToolbarForTool(selector, ToolId::Arrow);
    if (!toolbarGeometry.isValid() || toolbarGeometry.isEmpty()) {
        QSKIP("Floating toolbar did not produce a valid geometry in this environment.");
    }

    QWindow* toolbarWindow = RegionSelectorTestAccess::toolbarWindow(selector);
    if (!toolbarWindow) {
        QSKIP("Floating toolbar window did not initialize in this environment.");
    }

    const QPoint originalCursorPos = QCursor::pos();
    const QPoint toolbarPos = toolbarGeometry.center();
    const QPoint selectionBodyGlobal = selector.mapToGlobal(kSelectionBodyPos);

    QCursor::setPos(toolbarPos);
    if (QCursor::pos() != toolbarPos) {
        QSKIP("System cursor position could not be adjusted for toolbar round-trip test.");
    }

    RegionSelectorTestAccess::dispatchWindowEnter(toolbarWindow, toolbarPos);

    QCursor::setPos(selectionBodyGlobal);
    if (QCursor::pos() != selectionBodyGlobal) {
        QCursor::setPos(originalCursorPos);
        QSKIP("System cursor position could not be adjusted for selector restore test.");
    }

    QEvent leaveEvent(QEvent::Leave);
    QCoreApplication::sendEvent(toolbarWindow, &leaveEvent);
    RegionSelectorTestAccess::dispatchWidgetMouseMove(selector, kSelectionBodyPos);
    cursorManager.reapplyCursorForWidget(&selector);

    QTRY_COMPARE(selector.cursor().shape(), Qt::CrossCursor);
    QCursor::setPos(originalCursorPos);
}

void TestRegionSelectorStyleSync::testToolbarLeaveRestoresSelectionBodyMoveCursor()
{
    RegionSelector selector;
    auto& cursorManager = CursorManager::instance();

    const QRect toolbarGeometry = showToolbarForTool(selector, ToolId::Selection);
    if (!toolbarGeometry.isValid() || toolbarGeometry.isEmpty()) {
        QSKIP("Floating toolbar did not produce a valid geometry in this environment.");
    }

    QWindow* toolbarWindow = RegionSelectorTestAccess::toolbarWindow(selector);
    if (!toolbarWindow) {
        QSKIP("Floating toolbar window did not initialize in this environment.");
    }

    const QPoint originalCursorPos = QCursor::pos();
    const QPoint toolbarPos = toolbarGeometry.center();
    const QPoint selectionBodyGlobal = selector.mapToGlobal(kSelectionBodyPos);

    QCursor::setPos(toolbarPos);
    if (QCursor::pos() != toolbarPos) {
        QSKIP("System cursor position could not be adjusted for selection restore test.");
    }

    RegionSelectorTestAccess::dispatchWindowEnter(toolbarWindow, toolbarPos);

    QCursor::setPos(selectionBodyGlobal);
    if (QCursor::pos() != selectionBodyGlobal) {
        QCursor::setPos(originalCursorPos);
        QSKIP("System cursor position could not be adjusted for selection restore test.");
    }

    QEvent leaveEvent(QEvent::Leave);
    QCoreApplication::sendEvent(toolbarWindow, &leaveEvent);
    RegionSelectorTestAccess::dispatchWidgetMouseMove(selector, kSelectionBodyPos);
    cursorManager.reapplyCursorForWidget(&selector);

    QTRY_VERIFY(isMoveCursor(selector.cursor()));
    QCursor::setPos(originalCursorPos);
}

void TestRegionSelectorStyleSync::testArrowControlReleaseRestoresSelectionBodyCursor()
{
    RegionSelector selector;
    auto& cursorManager = CursorManager::instance();

    prepareSelectionTool(selector);
    addSelectedCurvedArrow(selector);

    QMouseEvent pressEvent(QEvent::MouseButtonPress, kArrowControlHandlePos, kArrowControlHandlePos,
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    selector.m_inputHandler->handleMousePress(&pressEvent);
    QCOMPARE(selector.cursor().shape(), Qt::PointingHandCursor);

    QMouseEvent moveEvent(QEvent::MouseMove, kArrowDraggedControlPos, kArrowDraggedControlPos,
                          Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    selector.m_inputHandler->handleMouseMove(&moveEvent);

    QMouseEvent releaseEvent(QEvent::MouseButtonRelease, kSelectionBodyPos, kSelectionBodyPos,
                             Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    selector.m_inputHandler->handleMouseRelease(&releaseEvent);
    cursorManager.reapplyCursorForWidget(&selector);

    verifyMoveCursor(selector.cursor());
}

void TestRegionSelectorStyleSync::testRestoreRegionCursorAfterArrowControlHoverReturnsSelectionBodyCursor()
{
    RegionSelector selector;
    auto& cursorManager = CursorManager::instance();

    prepareSelectionTool(selector);
    addSelectedCurvedArrow(selector);

    selector.m_inputHandler->syncHoverCursorAt(kArrowControlHandlePos);
    cursorManager.reapplyCursorForWidget(&selector);
    QCOMPARE(selector.cursor().shape(), Qt::PointingHandCursor);

    selector.restoreRegionCursorAt(kSelectionBodyPos);
    verifyMoveCursor(selector.cursor());
}

void TestRegionSelectorStyleSync::testPopupRestoreReturnsSelectionBodyCursor()
{
    RegionSelector selector;
    auto& authority = CursorAuthority::instance();
    auto& cursorManager = CursorManager::instance();

    prepareSelectionTool(selector);
    selector.restoreRegionCursorAt(kSelectionBodyPos);

    verifyMoveCursor(selector.cursor());

    authority.submitWidgetRequest(
        &selector, QStringLiteral("floating.popup"), CursorRequestSource::Popup,
        CursorStyleSpec::fromShape(Qt::ArrowCursor));
    cursorManager.reapplyCursorForWidget(&selector);
    QCOMPARE(selector.cursor().shape(), Qt::ArrowCursor);

    authority.clearWidgetRequest(&selector, QStringLiteral("floating.popup"));
    selector.restoreRegionCursorAt(kSelectionBodyPos);
    verifyMoveCursor(selector.cursor());
}

void TestRegionSelectorStyleSync::testPopupRestoreReturnsMosaicCursor()
{
    RegionSelector selector;
    auto& authority = CursorAuthority::instance();
    auto& cursorManager = CursorManager::instance();

    selector.m_selectionManager->setSelectionRect(kSelectionRect);
    selector.m_toolManager->setCurrentTool(ToolId::Mosaic);
    selector.m_toolManager->setWidth(18);
    cursorManager.updateToolCursorForWidget(&selector);
    cursorManager.reapplyCursorForWidget(&selector);

    const QCursor toolCursor = selector.cursor();
    QVERIFY(!toolCursor.pixmap().isNull());

    authority.submitWidgetRequest(
        &selector, QStringLiteral("floating.popup"), CursorRequestSource::Popup,
        CursorStyleSpec::fromShape(Qt::ArrowCursor));
    cursorManager.reapplyCursorForWidget(&selector);
    QCOMPARE(selector.cursor().shape(), Qt::ArrowCursor);

    authority.clearWidgetRequest(&selector, QStringLiteral("floating.popup"));
    cursorManager.reapplyCursorForWidget(&selector);

    const QCursor restoredCursor = selector.cursor();
    QVERIFY(!restoredCursor.pixmap().isNull());
    QCOMPARE(restoredCursor.shape(), Qt::BitmapCursor);
    QCOMPARE(restoredCursor.hotSpot(), toolCursor.hotSpot());
    QCOMPARE(restoredCursor.pixmap().deviceIndependentSize(),
             toolCursor.pixmap().deviceIndependentSize());
}

void TestRegionSelectorStyleSync::testInitializeForScreen_DisabledMagnifierPreventsVisibility()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No screens available for RegionSelector magnifier setting test.");
    }

    auto& settings = RegionCaptureSettingsManager::instance();
    settings.setMagnifierEnabled(false);

    RegionSelector selector;
    const QSize size = screen->geometry().size().boundedTo(QSize(320, 240));
    QPixmap preCapture(size);
    preCapture.fill(Qt::red);

    selector.initializeForScreen(screen, preCapture);

    QVERIFY(!selector.shouldShowMagnifier());
    QCOMPARE(selector.m_magnifierEnabled, false);

    settings.setMagnifierEnabled(true);
}

void TestRegionSelectorStyleSync::testInitializeForScreen_BeaverStyleDisablesMagnifierMode()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No screens available for RegionSelector cursor companion setting test.");
    }

    auto& settings = RegionCaptureSettingsManager::instance();
    settings.setCursorCompanionStyle(
        RegionCaptureSettingsManager::CursorCompanionStyle::Beaver);

    RegionSelector selector;
    const QSize size = screen->geometry().size().boundedTo(QSize(320, 240));
    QPixmap preCapture(size);
    preCapture.fill(Qt::darkYellow);

    selector.initializeForScreen(screen, preCapture);

    QCOMPARE(
        selector.m_cursorCompanionStyle,
        RegionCaptureSettingsManager::CursorCompanionStyle::Beaver);
    QCOMPARE(selector.m_magnifierEnabled, false);
    QVERIFY(!selector.shouldShowMagnifier());

    settings.setCursorCompanionStyle(
        RegionCaptureSettingsManager::CursorCompanionStyle::Magnifier);
}

void TestRegionSelectorStyleSync::testInitializeForScreen_BeaverStyleSkipsMagnifierPrewarm()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No screens available for RegionSelector cursor companion setting test.");
    }

    auto& settings = RegionCaptureSettingsManager::instance();
    settings.setCursorCompanionStyle(
        RegionCaptureSettingsManager::CursorCompanionStyle::Beaver);

    RegionSelector selector;
    const QSize size = screen->geometry().size().boundedTo(QSize(320, 240));
    QPixmap preCapture(size);
    preCapture.fill(Qt::cyan);

    selector.initializeForScreen(screen, preCapture);

    QCOMPARE(selector.m_cursorCompanionStyle,
             RegionCaptureSettingsManager::CursorCompanionStyle::Beaver);
    QCOMPARE(selector.m_magnifierPanel->currentColor(), QColor());

    settings.setCursorCompanionStyle(
        RegionCaptureSettingsManager::CursorCompanionStyle::Magnifier);
}

void TestRegionSelectorStyleSync::testInitializeForScreen_MagnifierStylePrewarmsCache()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No screens available for RegionSelector cursor companion setting test.");
    }

    const QPoint originalCursorPos = QCursor::pos();
    const QPoint cursorPos = screen->geometry().topLeft() + QPoint(100, 100);
    QCursor::setPos(cursorPos);
    QCoreApplication::processEvents();
    if (QCursor::pos() != cursorPos) {
        QCursor::setPos(originalCursorPos);
        QSKIP("System cursor position could not be adjusted for magnifier prewarm test.");
    }

    auto& settings = RegionCaptureSettingsManager::instance();
    settings.setCursorCompanionStyle(
        RegionCaptureSettingsManager::CursorCompanionStyle::Magnifier);

    RegionSelector selector;
    const QSize size = screen->geometry().size().boundedTo(QSize(320, 240));
    QPixmap preCapture(size);
    preCapture.fill(Qt::magenta);

    selector.initializeForScreen(screen, preCapture);

    QCOMPARE(selector.m_cursorCompanionStyle,
             RegionCaptureSettingsManager::CursorCompanionStyle::Magnifier);
    QCursor::setPos(originalCursorPos);
    QCOMPARE(selector.m_magnifierPanel->currentColor(), QColor(Qt::magenta));
}

void TestRegionSelectorStyleSync::testDisabledMagnifierIgnoresShiftAndCopyShortcuts()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No screens available for RegionSelector magnifier shortcut test.");
    }

    auto& settings = RegionCaptureSettingsManager::instance();
    settings.setMagnifierEnabled(false);

    RegionSelector selector;
    const QSize size = screen->geometry().size().boundedTo(QSize(320, 240));
    QPixmap preCapture(size);
    preCapture.fill(Qt::blue);
    selector.initializeForScreen(screen, preCapture);

    selector.m_magnifierPanel->setShowHexColor(false);
    QGuiApplication::clipboard()->setText(QStringLiteral("keep-me"));

    QKeyEvent shiftEvent(QEvent::KeyPress, Qt::Key_Shift, Qt::NoModifier);
    selector.keyPressEvent(&shiftEvent);
    QCOMPARE(selector.m_magnifierPanel->showHexColor(), false);

    QKeyEvent copyEvent(QEvent::KeyPress, Qt::Key_C, Qt::NoModifier);
    selector.keyPressEvent(&copyEvent);
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("keep-me"));

    settings.setMagnifierEnabled(true);
}

void TestRegionSelectorStyleSync::testBeaverStyleIgnoresShiftAndCopyShortcuts()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No screens available for RegionSelector favicon shortcut test.");
    }

    auto& settings = RegionCaptureSettingsManager::instance();
    settings.setCursorCompanionStyle(
        RegionCaptureSettingsManager::CursorCompanionStyle::Beaver);

    RegionSelector selector;
    const QSize size = screen->geometry().size().boundedTo(QSize(320, 240));
    QPixmap preCapture(size);
    preCapture.fill(Qt::darkGreen);
    selector.initializeForScreen(screen, preCapture);

    selector.m_magnifierPanel->setShowHexColor(false);
    QGuiApplication::clipboard()->setText(QStringLiteral("keep-me"));

    QKeyEvent shiftEvent(QEvent::KeyPress, Qt::Key_Shift, Qt::NoModifier);
    selector.keyPressEvent(&shiftEvent);
    QCOMPARE(selector.m_magnifierPanel->showHexColor(), false);

    QKeyEvent copyEvent(QEvent::KeyPress, Qt::Key_C, Qt::NoModifier);
    selector.keyPressEvent(&copyEvent);
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("keep-me"));

    settings.setCursorCompanionStyle(
        RegionCaptureSettingsManager::CursorCompanionStyle::Magnifier);
}

QTEST_MAIN(TestRegionSelectorStyleSync)
#include "tst_StyleSync.moc"
