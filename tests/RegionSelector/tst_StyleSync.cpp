#include <QtTest/QtTest>

#include <memory>

#include <QClipboard>
#include <QGuiApplication>
#include <QScreen>

#include "RegionSelector.h"
#include "annotations/ArrowAnnotation.h"
#include "cursor/CursorAuthority.h"
#include "cursor/CursorManager.h"
#include "settings/RegionCaptureSettingsManager.h"
#include "region/RegionInputHandler.h"
#include "tools/ToolManager.h"

namespace {
const QRect kSelectionRect(40, 40, 160, 120);
const QPoint kSelectionBodyPos(180, 140);
const QPoint kArrowControlHandlePos(100, 80);
const QPoint kArrowDraggedControlPos(110, 86);
}  // namespace

class TestRegionSelectorStyleSync : public QObject
{
    Q_OBJECT

private:
    static void prepareSelectionTool(RegionSelector& selector);
    static void addSelectedCurvedArrow(RegionSelector& selector);

private slots:
    void testUsesAuthorityModeByDefault();
    void testSelectionBodyHoverUsesMoveCursor();
    void testSelectionDragUsesClosedHandCursor();
    void testOverlayRestoreReturnsArrowToolCursor();
    void testArrowControlReleaseRestoresSelectionBodyCursor();
    void testRestoreRegionCursorAfterArrowControlHoverReturnsSelectionBodyCursor();
    void testPopupRestoreReturnsSelectionBodyCursor();
    void testPopupRestoreReturnsMosaicCursor();
    void testInitializeForScreen_DisabledMagnifierPreventsVisibility();
    void testInitializeForScreen_BeaverStyleDisablesMagnifierMode();
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

    QCOMPARE(selector.cursor().shape(), Qt::SizeAllCursor);
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

void TestRegionSelectorStyleSync::testOverlayRestoreReturnsArrowToolCursor()
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
        &selector, QStringLiteral("floating.overlay.toolbar"), CursorRequestSource::Overlay,
        CursorStyleSpec::fromShape(Qt::ArrowCursor));
    cursorManager.reapplyCursorForWidget(&selector);
    QCOMPARE(selector.cursor().shape(), Qt::ArrowCursor);

    authority.clearWidgetRequest(&selector, QStringLiteral("floating.overlay.toolbar"));
    selector.restoreRegionCursorAt(kSelectionBodyPos);
    QCOMPARE(selector.cursor().shape(), Qt::CrossCursor);
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

    QCOMPARE(selector.cursor().shape(), Qt::SizeAllCursor);
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
    QCOMPARE(selector.cursor().shape(), Qt::SizeAllCursor);
}

void TestRegionSelectorStyleSync::testPopupRestoreReturnsSelectionBodyCursor()
{
    RegionSelector selector;
    auto& authority = CursorAuthority::instance();
    auto& cursorManager = CursorManager::instance();

    prepareSelectionTool(selector);
    selector.restoreRegionCursorAt(kSelectionBodyPos);

    QCOMPARE(selector.cursor().shape(), Qt::SizeAllCursor);

    authority.submitWidgetRequest(
        &selector, QStringLiteral("floating.popup"), CursorRequestSource::Popup,
        CursorStyleSpec::fromShape(Qt::ArrowCursor));
    cursorManager.reapplyCursorForWidget(&selector);
    QCOMPARE(selector.cursor().shape(), Qt::ArrowCursor);

    authority.clearWidgetRequest(&selector, QStringLiteral("floating.popup"));
    selector.restoreRegionCursorAt(kSelectionBodyPos);
    QCOMPARE(selector.cursor().shape(), Qt::SizeAllCursor);
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
