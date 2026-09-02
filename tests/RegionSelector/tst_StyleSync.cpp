#include <QtTest/QtTest>

#include <memory>

#include <QClipboard>
#include <QGuiApplication>
#include <QSettings>
#include <QScreen>
#include <QtMath>
#include "RegionSelector.h"
#include "RegionSelectorTestAccess.h"
#include "annotations/ArrowAnnotation.h"
#include "cursor/CursorAuthority.h"
#include "cursor/CursorManager.h"
#include "cursor/CursorStyleCatalog.h"
#include "qml/QmlFloatingSubToolbar.h"
#include "qml/QmlFloatingToolbar.h"
#include "qml/PinToolOptionsViewModel.h"
#include "settings/AnnotationSettingsManager.h"
#include "settings/RegionCaptureSettingsManager.h"
#include "settings/Settings.h"
#include "region/RegionInputHandler.h"
#include "region/CaptureShortcutHintsOverlay.h"
#include "region/SelectionDimensionLabel.h"
#include "region/SelectionDirtyRegionPlanner.h"
#include "tools/ToolManager.h"

namespace {
const QRect kSelectionRect(40, 40, 160, 120);
const QPoint kSelectionBodyPos(180, 140);
const QPoint kArrowControlHandlePos(100, 80);
const QPoint kArrowDraggedControlPos(110, 86);

class ScopedWidthSettings final
{
public:
    ScopedWidthSettings()
        : m_settings(SnapTray::getSettings())
        , m_hadStrokeWidth(m_settings.contains(QStringLiteral("annotationWidth")))
        , m_strokeWidth(m_settings.value(QStringLiteral("annotationWidth")))
        , m_hadMosaicWidth(m_settings.contains(QStringLiteral("mosaicBrushSize")))
        , m_mosaicWidth(m_settings.value(QStringLiteral("mosaicBrushSize")))
    {
    }

    ~ScopedWidthSettings()
    {
        restore(QStringLiteral("annotationWidth"), m_hadStrokeWidth, m_strokeWidth);
        restore(QStringLiteral("mosaicBrushSize"), m_hadMosaicWidth, m_mosaicWidth);
        m_settings.sync();
    }

private:
    void restore(const QString& key, bool existed, const QVariant& value)
    {
        if (existed) {
            m_settings.setValue(key, value);
        }
        else {
            m_settings.remove(key);
        }
    }

    QSettings m_settings;
    bool m_hadStrokeWidth;
    QVariant m_strokeWidth;
    bool m_hadMosaicWidth;
    QVariant m_mosaicWidth;
};

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
    void testStrokeAndMosaicWidthsRestoreIndependently();
    void testSelectionBodyHoverUsesMoveCursor();
    void testSelectionBodyHoverUsesEventPosWhenLiveCursorLags();
    void testSelectionCompletionShowsToolbarAfterWindowsHandoff();
    void testSelectionCompletionPositionsToolbarAfterWindowsHandoff();
    void testSelectionDragUsesClosedHandCursor();
    void testReleaseOverFloatingUiFinishesSelectionDrag();
    void testReleaseOverFloatingUiFinishesDrawingDrag();
    void testReleaseOverFloatingUiCancelsSingleClickAnnotation_data();
    void testReleaseOverFloatingUiCancelsSingleClickAnnotation();
    void testAutoBlurRequestGuardRejectsStaleContext();
    void testOverlayRequestRestoreReturnsArrowToolCursor();
    void testFloatingToolbarWindowOwnsArrowCursor();
    void testToolbarLeaveRestoresArrowToolCrossCursor();
    void testToolbarLeaveRestoresSelectionBodyMoveCursor();
    void testArrowControlReleaseRestoresSelectionBodyCursor();
    void testRestoreRegionCursorAfterArrowControlHoverReturnsSelectionBodyCursor();
    void testPopupRestoreReturnsSelectionBodyCursor();
    void testPopupRestoreReturnsMosaicCursor();
    void testMosaicWidthChangeImmediatelyUpdatesCursor();
    void testInitializeForScreen_DisabledMagnifierPreventsVisibility();
    void testInitializeForScreen_BeaverStyleDisablesMagnifierMode();
    void testInitializeForScreen_BeaverStyleSkipsMagnifierPrewarm();
    void testInitializeForScreen_MagnifierStylePrewarmsCache();
    void testDisabledMagnifierIgnoresShiftAndCopyShortcuts();
    void testBeaverStyleIgnoresShiftAndCopyShortcuts();
    void testHostFallbackPaintsAboveShortcutHints();
    void testHostFallbackUsesOverlayRenderHints();
    void testInitialRevealTimeoutRevealsReadySelector();
#ifdef Q_OS_MACOS
    void testMacInitialCursorCompanionUsesHostUntilDetachedUiAppears();
#endif
#ifdef Q_OS_LINUX
    void testLinuxCaptureSurfaceRemainsManaged();
    void testLinuxTransparentCaptureHelpersDoNotBypassWindowManager();
    void testLinuxSelectionToolbarPrewarmsAfterShow();
#endif
};

void TestRegionSelectorStyleSync::testHostFallbackPaintsAboveShortcutHints()
{
    RegionSelector selector;
    selector.resize(QSize(640, 480));
    selector.m_initialRevealState = RegionSelector::InitialRevealState::Revealed;
    selector.m_cursorCompanionStyle =
        RegionCaptureSettingsManager::CursorCompanionStyle::Magnifier;
    selector.m_shortcutHintsVisible = true;

    const QRect hintsRect =
        selector.m_shortcutHintsOverlay->panelRectForViewport(selector.size());
    QVERIFY(hintsRect.isValid());

    // Keep the magnifier below the cursor so its opaque panel overlaps the
    // top of the shortcut-hints panel.
    const QPoint cursorPos(hintsRect.left() + 16, hintsRect.top() - 32);
    const SelectionDirtyRegionPlanner dirtyRegionPlanner;
    const QRect fallbackRect = dirtyRegionPlanner.cursorCompanionRectForCursor(
        RegionCaptureSettingsManager::CursorCompanionStyle::Magnifier,
        cursorPos,
        selector.size());
    QVERIFY(fallbackRect.intersects(hintsRect));

    QPixmap background(selector.size());
    background.fill(QColor(72, 118, 164));

    selector.m_inputState.currentPoint = cursorPos;
    selector.m_hostFallbackCursorCompanionRect = fallbackRect;
    selector.m_magnifierOverlay->syncToHost(
        &selector,
        cursorPos,
        &background,
        RegionCaptureSettingsManager::CursorCompanionStyle::Magnifier,
        false);

    auto makeCanvas = [&selector]() {
        QImage image(selector.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        return image;
    };
    auto drawFallback = [&selector, &fallbackRect](QImage& image) {
        QPainter painter(&image);
        painter.setClipRect(fallbackRect, Qt::IntersectClip);
        selector.m_magnifierOverlay->paintFallback(painter, selector.size());
    };
    auto drawHints = [&selector](QImage& image) {
        QPainter painter(&image);
        selector.m_shortcutHintsOverlay->draw(painter, selector.size());
    };

    QImage expected = makeCanvas();
    drawHints(expected);
    drawFallback(expected);

    QImage reversed = makeCanvas();
    drawFallback(reversed);
    drawHints(reversed);
    QVERIFY2(expected != reversed,
             "The probe must exercise pixels shared by the hints and companion.");

    QImage actual = makeCanvas();
    {
        QPainter painter(&actual);
        selector.paintHostForegroundOverlays(painter, false);
    }
    QCOMPARE(actual, expected);
}

void TestRegionSelectorStyleSync::testHostFallbackUsesOverlayRenderHints()
{
    RegionSelector selector;
    selector.resize(QSize(180, 160));

    QImage sourceImage(QSize(192, 192), QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < sourceImage.height(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(sourceImage.scanLine(y));
        for (int x = 0; x < sourceImage.width(); ++x) {
            const bool light = ((x + y) % 3) == 0;
            row[x] = light ? qRgba(255, 255, 255, 255) : qRgba(0, 0, 0, 255);
        }
    }
    selector.m_magnifierOverlay->m_beaverPixmap = QPixmap::fromImage(sourceImage);

    const QPoint cursorPos(24, 24);
    const SelectionDirtyRegionPlanner dirtyRegionPlanner;
    const QRect targetRect = dirtyRegionPlanner.beaverRectForCursor(
        cursorPos, selector.size());
    selector.m_magnifierOverlay->syncToHost(
        &selector,
        cursorPos,
        nullptr,
        RegionCaptureSettingsManager::CursorCompanionStyle::Beaver,
        false);

    auto makeCanvas = [&selector]() {
        QImage image(selector.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        return image;
    };
    auto drawSource = [&](bool smooth) {
        QImage image = makeCanvas();
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, smooth);
        painter.drawPixmap(targetRect, selector.m_magnifierOverlay->m_beaverPixmap);
        return image;
    };

    const QImage fastReference = drawSource(false);
    const QImage smoothReference = drawSource(true);
    QVERIFY2(fastReference != smoothReference,
             "The probe image must distinguish fast and smooth pixmap scaling.");

    QImage fallback = makeCanvas();
    {
        QPainter painter(&fallback);
        QVERIFY(!painter.testRenderHint(QPainter::Antialiasing));
        QVERIFY(!painter.testRenderHint(QPainter::SmoothPixmapTransform));
        selector.m_magnifierOverlay->paintFallback(painter, selector.size());
        QVERIFY(!painter.testRenderHint(QPainter::Antialiasing));
        QVERIFY(!painter.testRenderHint(QPainter::SmoothPixmapTransform));
    }

    QCOMPARE(fallback, smoothReference);
}

#ifdef Q_OS_MACOS
void TestRegionSelectorStyleSync::testMacInitialCursorCompanionUsesHostUntilDetachedUiAppears()
{
    RegionSelector selector;
    selector.setAttribute(Qt::WA_DeleteOnClose, false);
    selector.resize(QSize(640, 480));
    selector.m_initialRevealState = RegionSelector::InitialRevealState::Revealed;
    selector.m_cursorCompanionStyle =
        RegionCaptureSettingsManager::CursorCompanionStyle::Beaver;
    selector.m_inputState.currentPoint = selector.rect().center();

    QPixmap beaverProbe(QSize(192, 192));
    beaverProbe.fill(QColor(78, 142, 206));
    selector.m_magnifierOverlay->m_beaverPixmap = beaverProbe;

    // This assertion is platform-window independent: with no detached QML UI,
    // synchronization selects the host paint path and never shows the overlay.
    selector.syncMagnifierOverlay();
    QVERIFY(!selector.cursorCompanionRequiresOverlay());
    QVERIFY(!selector.m_magnifierOverlay->isVisible());
    QVERIFY(selector.m_hostFallbackCursorCompanionRect.isValid());

    const QPoint cursorGlobal = QCursor::pos();
    QScreen* screen = QGuiApplication::screenAt(cursorGlobal);
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return;
    }

    const QRect screenGeometry = screen->geometry();
    const QSize hostSize = screenGeometry.size().boundedTo(QSize(640, 480));
    QPoint hostTopLeft = cursorGlobal - QPoint(hostSize.width() / 2, hostSize.height() / 2);
    hostTopLeft.setX(qBound(screenGeometry.left(),
                            hostTopLeft.x(),
                            screenGeometry.right() - hostSize.width() + 1));
    hostTopLeft.setY(qBound(screenGeometry.top(),
                            hostTopLeft.y(),
                            screenGeometry.bottom() - hostSize.height() + 1));

    selector.setGeometry(QRect(hostTopLeft, hostSize));
    selector.m_inputState.currentPoint = cursorGlobal - hostTopLeft;

    selector.show();
    QTRY_VERIFY(selector.isVisible());
    selector.syncMagnifierOverlay();

    QVERIFY(!selector.cursorCompanionRequiresOverlay());
    QVERIFY(!selector.m_magnifierOverlay->isVisible());
    QVERIFY(selector.m_hostFallbackCursorCompanionRect.isValid());

    // Keep the toolbar visible and its explicit position stable during repaint.
    selector.m_selectionManager->setSelectionRect(selector.rect());
    selector.m_toolbarUserDragged = true;
    selector.m_qmlToolbar->setPosition(screenGeometry.topLeft() + QPoint(12, 12));
    selector.m_qmlToolbar->show();
    QTRY_VERIFY(selector.m_qmlToolbar->isVisible());
    if (selector.m_qmlToolbar->geometry().contains(cursorGlobal)) {
        const QSize toolbarSize = selector.m_qmlToolbar->geometry().size();
        selector.m_qmlToolbar->setPosition(
            screenGeometry.bottomRight() -
            QPoint(toolbarSize.width() + 12, toolbarSize.height() + 12));
    }
    QVERIFY(!selector.m_qmlToolbar->geometry().contains(cursorGlobal));

    QVERIFY(selector.cursorCompanionRequiresOverlay());
    selector.syncMagnifierOverlay();
    QVERIFY(selector.m_magnifierOverlay->isVisible());

    // Exercise the real paint event synchronously so native exposure timing and
    // unrelated cursor/focus events cannot interrupt the renderer handoff.
    QImage overlayFrame(selector.m_magnifierOverlay->size(),
                        QImage::Format_ARGB32_Premultiplied);
    overlayFrame.fill(Qt::transparent);
    selector.m_magnifierOverlay->render(&overlayFrame);
    QVERIFY(selector.m_magnifierOverlay->hasPaintedSinceShow());

    // Once the top-level renderer has painted, the next synchronization drops
    // the temporary host copy and leaves a single companion surface.
    selector.syncMagnifierOverlay();
    QVERIFY(selector.m_magnifierOverlay->isVisible());
    QVERIFY(selector.m_magnifierOverlay->hasPaintedSinceShow());
    QVERIFY(!selector.m_hostFallbackCursorCompanionRect.isValid());

    selector.m_qmlToolbar->hide();
    selector.m_magnifierOverlay->hideOverlay();
    selector.hide();
}
#endif

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

void TestRegionSelectorStyleSync::testStrokeAndMosaicWidthsRestoreIndependently()
{
    ScopedWidthSettings restoreSettings;
    auto& settings = AnnotationSettingsManager::instance();
    settings.saveWidthForTool(ToolId::Pencil, 4);
    settings.saveWidthForTool(ToolId::Mosaic, 18);

    RegionSelector selector;
    selector.m_qmlSubToolbar.reset();
    QCOMPARE(selector.m_inputState.annotationWidth, 4);

    selector.handleToolbarClick(ToolId::Pencil);
    QCOMPARE(selector.m_inputState.currentTool, ToolId::Pencil);
    QCOMPARE(selector.m_inputState.annotationWidth, 4);
    QCOMPARE(selector.m_toolManager->width(), 4);
    QCOMPARE(selector.m_toolOptionsViewModel->currentWidth(), 4);

    selector.onLineWidthChanged(6);
    QCOMPARE(settings.loadWidthForTool(ToolId::Pencil), 6);
    QCOMPARE(settings.loadWidthForTool(ToolId::Mosaic), 18);

    selector.handleToolbarClick(ToolId::Mosaic);
    QCOMPARE(selector.m_inputState.currentTool, ToolId::Mosaic);
    QCOMPARE(selector.m_inputState.annotationWidth, 18);
    QCOMPARE(selector.m_toolManager->width(), 18);
    QCOMPARE(selector.m_toolOptionsViewModel->currentWidth(), 18);

    selector.onLineWidthChanged(30);
    QCOMPARE(settings.loadWidthForTool(ToolId::Pencil), 6);
    QCOMPARE(settings.loadWidthForTool(ToolId::Mosaic), 30);

    selector.handleToolbarClick(ToolId::Mosaic);
    QCOMPARE(selector.m_inputState.currentTool, ToolId::Selection);
    QCOMPARE(selector.m_toolManager->currentTool(), ToolId::Selection);
    QCOMPARE(selector.m_inputState.annotationWidth, 6);
    QCOMPARE(selector.m_toolManager->width(), 6);
    QCOMPARE(selector.m_toolOptionsViewModel->currentWidth(), 6);

    selector.handleToolbarClick(ToolId::Pencil);
    QCOMPARE(selector.m_inputState.currentTool, ToolId::Pencil);
    QCOMPARE(selector.m_inputState.annotationWidth, 6);
    QCOMPARE(selector.m_toolManager->width(), 6);
    QCOMPARE(selector.m_toolOptionsViewModel->currentWidth(), 6);
}

#ifdef Q_OS_LINUX
void TestRegionSelectorStyleSync::testLinuxCaptureSurfaceRemainsManaged()
{
    RegionSelector selector;
    QVERIFY(!selector.windowFlags().testFlag(Qt::X11BypassWindowManagerHint));
}

void TestRegionSelectorStyleSync::testLinuxTransparentCaptureHelpersDoNotBypassWindowManager()
{
    MagnifierOverlay magnifierOverlay(nullptr);
    SelectionDimmingOverlay dimmingOverlay;
    SelectionPreviewOverlay previewOverlay;
    CaptureChromeWindow chromeWindow;

    QVERIFY(!magnifierOverlay.windowFlags().testFlag(Qt::X11BypassWindowManagerHint));
    QVERIFY(!dimmingOverlay.windowFlags().testFlag(Qt::X11BypassWindowManagerHint));
    QVERIFY(!previewOverlay.windowFlags().testFlag(Qt::X11BypassWindowManagerHint));
    QVERIFY(!chromeWindow.windowFlags().testFlag(Qt::X11BypassWindowManagerHint));
}

void TestRegionSelectorStyleSync::testLinuxSelectionToolbarPrewarmsAfterShow()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No screen available for toolbar prewarm test.");
    }

    RegionSelector selector;
    QVERIFY(!RegionSelectorTestAccess::toolbarWindow(selector));

    QPixmap capture(screen->geometry().size());
    capture.fill(Qt::black);
    selector.initializeForScreen(screen, capture);
    selector.show();
    QCoreApplication::processEvents();

    QTRY_VERIFY_WITH_TIMEOUT(RegionSelectorTestAccess::toolbarWindow(selector), 1000);
    QVERIFY(!RegionSelectorTestAccess::toolbarVisible(selector));

    selector.close();
}
#endif

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

void TestRegionSelectorStyleSync::testSelectionCompletionShowsToolbarAfterWindowsHandoff()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No screen available for selection completion toolbar test.");
    }

    RegionSelector selector;
    QPixmap capture(screen->geometry().size());
    capture.fill(Qt::black);
    selector.initializeForScreen(screen, capture);
    RegionSelectorTestAccess::markInitialRevealRevealed(selector);
#ifdef Q_OS_WIN
    selector.show();
    QCoreApplication::processEvents();
#endif

    const QPoint start(40, 40);
    const QPoint end(180, 150);
    RegionSelectorTestAccess::dispatchMousePress(selector, start);

    QMouseEvent moveEvent(QEvent::MouseMove,
                          QPointF(end),
                          QPointF(end),
                          Qt::NoButton,
                          Qt::LeftButton,
                          Qt::NoModifier);
    selector.m_inputHandler->handleMouseMove(&moveEvent);

    RegionSelectorTestAccess::dispatchMouseRelease(selector, end);

#ifdef Q_OS_WIN
    QVERIFY(RegionSelectorTestAccess::selectionCompletionHandoffPending(selector));
    QVERIFY(!RegionSelectorTestAccess::toolbarVisible(selector));
    QTRY_VERIFY(!RegionSelectorTestAccess::selectionCompletionHandoffPending(selector));
    QTRY_VERIFY(RegionSelectorTestAccess::toolbarVisible(selector));
#else
    QVERIFY(RegionSelectorTestAccess::toolbarVisible(selector));
#endif
}

void TestRegionSelectorStyleSync::testSelectionCompletionPositionsToolbarAfterWindowsHandoff()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No screen available for selection completion toolbar placement test.");
    }

    RegionSelector selector;
    QPixmap capture(screen->geometry().size());
    capture.fill(Qt::black);
    selector.initializeForScreen(screen, capture);
    RegionSelectorTestAccess::markInitialRevealRevealed(selector);
#ifdef Q_OS_WIN
    selector.show();
    QCoreApplication::processEvents();
#endif

    const QSize viewportSize = selector.size();
    if (viewportSize.width() < 360 || viewportSize.height() < 260) {
        QSKIP("Screen is too small for deterministic toolbar placement test.");
    }

    const int selectionWidth = qMin(260, viewportSize.width() - 80);
    const int selectionHeight = 100;
    const int left = qMax(40, (viewportSize.width() - selectionWidth) / 2);
    const int top = viewportSize.height() - selectionHeight - 40;
    const QPoint start(left, top);
    const QPoint end(left + selectionWidth, top + selectionHeight);

    RegionSelectorTestAccess::dispatchMousePress(selector, start);
    QMouseEvent moveEvent(QEvent::MouseMove,
                          QPointF(end),
                          QPointF(end),
                          Qt::NoButton,
                          Qt::LeftButton,
                          Qt::NoModifier);
    selector.m_inputHandler->handleMouseMove(&moveEvent);
    RegionSelectorTestAccess::dispatchMouseRelease(selector, end);

#ifdef Q_OS_WIN
    QVERIFY(RegionSelectorTestAccess::selectionCompletionHandoffPending(selector));
    QVERIFY(!RegionSelectorTestAccess::toolbarVisible(selector));
    QTRY_VERIFY(!RegionSelectorTestAccess::selectionCompletionHandoffPending(selector));
    QTRY_VERIFY(RegionSelectorTestAccess::toolbarVisible(selector));
#else
    QVERIFY(RegionSelectorTestAccess::toolbarVisible(selector));
#endif
    const QRect toolbarGeometry = RegionSelectorTestAccess::toolbarGeometry(selector);
    QVERIFY(toolbarGeometry.isValid());
    QVERIFY(!toolbarGeometry.isEmpty());

    const QRect selectionRect = RegionSelectorTestAccess::selectionRect(selector);
    QFont labelFont;
    labelFont.setPointSize(12);
    labelFont.setBold(true);
    const QString dimensions = SelectionDimensionLabel::widgetLabel(
        selectionRect,
        RegionSelectorTestAccess::devicePixelRatio(selector));
    const QRect dimensionRect = SelectionDimensionLabel::selectionPanelLayout(
        selectionRect,
        dimensions,
        labelFont,
        viewportSize,
        SelectionDimensionLabel::controlAnchorSize(false)).panelRect;
    QVERIFY(dimensionRect.isValid());
    QVERIFY(!dimensionRect.isEmpty());

    const QPoint expectedLocalTopLeft = SnapTray::QmlFloatingToolbar::resolveTopLeftForSelection(
        selectionRect,
        toolbarGeometry.size(),
        QRect(QPoint(), viewportSize),
        SnapTray::QmlFloatingToolbar::HorizontalAlignment::RightEdge,
        dimensionRect);

    QCOMPARE(toolbarGeometry.topLeft(), selector.mapToGlobal(expectedLocalTopLeft));
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

void TestRegionSelectorStyleSync::testReleaseOverFloatingUiFinishesSelectionDrag()
{
    RegionSelector selector;
    const QRect toolbarGeometry = showToolbarForTool(selector, ToolId::Selection);
    if (!toolbarGeometry.isValid() || toolbarGeometry.isEmpty()) {
        QSKIP("Floating toolbar is unavailable on this platform.");
    }

    QMouseEvent pressEvent(QEvent::MouseButtonPress,
                           kSelectionBodyPos, kSelectionBodyPos,
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    selector.m_inputHandler->handleMousePress(&pressEvent);
    QVERIFY(selector.m_selectionManager->isMoving());

    // Selection dragging normally suppresses its toolbar. Re-show it to exercise
    // the release path where a captured mouse-up lands on a floating surface.
    selector.m_qmlToolbar->show();
    QCoreApplication::processEvents();
    const QPoint releaseGlobalPos = selector.m_qmlToolbar->geometry().center();
    QVERIFY(selector.isGlobalPosOverFloatingUi(releaseGlobalPos));
    const QPoint releaseLocalPos = selector.mapFromGlobal(releaseGlobalPos);
    QMouseEvent releaseEvent(QEvent::MouseButtonRelease,
                             QPointF(releaseLocalPos),
                             QPointF(releaseLocalPos),
                             QPointF(releaseGlobalPos),
                             Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    selector.mouseReleaseEvent(&releaseEvent);

    QVERIFY(selector.m_selectionManager->isComplete());
    QCOMPARE(CursorManager::instance().dragStateForWidget(&selector), DragState::None);
}

void TestRegionSelectorStyleSync::testReleaseOverFloatingUiFinishesDrawingDrag()
{
    RegionSelector selector;
    const QRect toolbarGeometry = showToolbarForTool(selector, ToolId::Shape);
    if (!toolbarGeometry.isValid() || toolbarGeometry.isEmpty()) {
        QSKIP("Floating toolbar is unavailable on this platform.");
    }

    QMouseEvent pressEvent(QEvent::MouseButtonPress,
                           kSelectionBodyPos, kSelectionBodyPos,
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    selector.m_inputHandler->handleMousePress(&pressEvent);
    QVERIFY(selector.m_inputState.isDrawing);

    const QPoint releaseGlobalPos = toolbarGeometry.center();
    QVERIFY(selector.isGlobalPosOverFloatingUi(releaseGlobalPos));
    const QPoint releaseLocalPos = selector.mapFromGlobal(releaseGlobalPos);
    QMouseEvent releaseEvent(QEvent::MouseButtonRelease,
                             QPointF(releaseLocalPos),
                             QPointF(releaseLocalPos),
                             QPointF(releaseGlobalPos),
                             Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    selector.mouseReleaseEvent(&releaseEvent);

    QCOMPARE(selector.m_annotationLayer->itemCount(), static_cast<size_t>(1));
    QVERIFY(!selector.m_inputState.isDrawing);
    QVERIFY(!selector.m_toolManager->isDrawing());
}

void TestRegionSelectorStyleSync::testReleaseOverFloatingUiCancelsSingleClickAnnotation_data()
{
    QTest::addColumn<int>("toolId");

    QTest::newRow("step-badge") << static_cast<int>(ToolId::StepBadge);
    QTest::newRow("emoji-sticker") << static_cast<int>(ToolId::EmojiSticker);
}

void TestRegionSelectorStyleSync::testReleaseOverFloatingUiCancelsSingleClickAnnotation()
{
    QFETCH(int, toolId);
    const ToolId tool = static_cast<ToolId>(toolId);

    RegionSelector selector;
    const QRect toolbarGeometry = showToolbarForTool(selector, tool);
    if (!toolbarGeometry.isValid() || toolbarGeometry.isEmpty()) {
        QSKIP("Floating toolbar is unavailable on this platform.");
    }

    QSignalSpy drawingStateSpy(
        selector.m_inputHandler, &RegionInputHandler::drawingStateChanged);
    QMouseEvent pressEvent(QEvent::MouseButtonPress,
                           kSelectionBodyPos, kSelectionBodyPos,
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    selector.m_inputHandler->handleMousePress(&pressEvent);
    QVERIFY(selector.m_inputState.isDrawing);
    QCOMPARE(selector.m_annotationLayer->itemCount(), static_cast<size_t>(0));

    const QPoint releaseGlobalPos = toolbarGeometry.center();
    QVERIFY(selector.isGlobalPosOverFloatingUi(releaseGlobalPos));
    const QPoint releaseLocalPos = selector.mapFromGlobal(releaseGlobalPos);
    QMouseEvent releaseEvent(QEvent::MouseButtonRelease,
                             QPointF(releaseLocalPos),
                             QPointF(releaseLocalPos),
                             QPointF(releaseGlobalPos),
                             Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    selector.mouseReleaseEvent(&releaseEvent);

    QCOMPARE(selector.m_annotationLayer->itemCount(), static_cast<size_t>(0));
    QVERIFY(!selector.m_inputState.isDrawing);
    QVERIFY(!selector.m_toolManager->isDrawing());
    QCOMPARE(drawingStateSpy.count(), 2);
    QCOMPARE(drawingStateSpy.at(0).at(0).toBool(), true);
    QCOMPARE(drawingStateSpy.at(1).at(0).toBool(), false);

    // Cancelling the captured release must leave the tool ready for the next
    // ordinary canvas click.
    QMouseEvent nextPressEvent(QEvent::MouseButtonPress,
                               kSelectionBodyPos, kSelectionBodyPos,
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    selector.m_inputHandler->handleMousePress(&nextPressEvent);
    QMouseEvent nextReleaseEvent(QEvent::MouseButtonRelease,
                                 kSelectionBodyPos, kSelectionBodyPos,
                                 Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    selector.m_inputHandler->handleMouseRelease(&nextReleaseEvent);

    QCOMPARE(selector.m_annotationLayer->itemCount(), static_cast<size_t>(1));
    QVERIFY(!selector.m_inputState.isDrawing);
}

void TestRegionSelectorStyleSync::testAutoBlurRequestGuardRejectsStaleContext()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No screen available for auto-blur request guard test.");
    }

    RegionSelector selector;
    QPixmap capture(240, 160);
    capture.fill(Qt::darkCyan);
    selector.initializeForScreen(screen, capture);
    selector.m_selectionManager->setSelectionRect(QRect(20, 20, 100, 80));

    RegionSelector::AutoBlurRequestSnapshot request;
    request.generation = selector.m_autoBlurGeneration;
    request.selectionRect = selector.m_selectionManager->selectionRect();
    request.clampedPhysicalRect = QRect(20, 20, 100, 80);
    request.devicePixelRatio = selector.m_devicePixelRatio;
    request.sourcePixmap = selector.m_sharedSourcePixmap;
    request.sourceScreen = selector.m_currentScreen;
    request.annotationLayer = selector.m_annotationLayer;
    request.blockSize = 14;
    request.blurType = MosaicBlurType::Pixelate;
    QVERIFY(selector.isAutoBlurRequestCurrent(request));

    auto stale = request;
    ++stale.generation;
    QVERIFY(!selector.isAutoBlurRequestCurrent(stale));

    stale = request;
    stale.selectionRect.translate(1, 0);
    QVERIFY(!selector.isAutoBlurRequestCurrent(stale));

    stale = request;
    stale.devicePixelRatio += 0.25;
    QVERIFY(!selector.isAutoBlurRequestCurrent(stale));

    stale = request;
    stale.sourcePixmap = std::make_shared<const QPixmap>(capture);
    QVERIFY(!selector.isAutoBlurRequestCurrent(stale));

    stale = request;
    stale.sourceScreen = nullptr;
    QVERIFY(!selector.isAutoBlurRequestCurrent(stale));

    QPointer<QScreen> savedCurrentScreen = selector.m_currentScreen;
    selector.m_currentScreen = nullptr;
    stale = request;
    stale.sourceScreen = nullptr;
    QVERIFY(!selector.isAutoBlurRequestCurrent(stale));
    selector.m_currentScreen = savedCurrentScreen;

    stale = request;
    stale.annotationLayer = nullptr;
    QVERIFY(!selector.isAutoBlurRequestCurrent(stale));

    const quint64 selectionGeneration = selector.m_autoBlurGeneration;
    selector.m_selectionManager->setSelectionRect(QRect(21, 20, 100, 80));
    QCOMPARE(selector.m_autoBlurGeneration, selectionGeneration + 1);
    QVERIFY(!selector.isAutoBlurRequestCurrent(request));

    const quint64 captureGeneration = selector.m_autoBlurGeneration;
    QPixmap replacementCapture(240, 160);
    replacementCapture.fill(Qt::darkMagenta);
    selector.applyCaptureContext(
        {replacementCapture, selector.m_devicePixelRatio, selector.m_currentScreen});
    QCOMPARE(selector.m_autoBlurGeneration, captureGeneration + 1);
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

void TestRegionSelectorStyleSync::testMosaicWidthChangeImmediatelyUpdatesCursor()
{
    ScopedWidthSettings restoreSettings;
    auto& settings = AnnotationSettingsManager::instance();
    settings.saveWidthForTool(ToolId::Pencil, 7);
    settings.saveWidthForTool(ToolId::Mosaic, 18);

    RegionSelector selector;
    selector.m_selectionManager->setSelectionRect(kSelectionRect);
    selector.m_inputState.currentTool = ToolId::Mosaic;
    selector.m_toolManager->setCurrentTool(ToolId::Mosaic);

    constexpr int cursorPadding = 4;
    const QList<int> presetWidths{10, 18, 30};
    for (const int width : presetWidths) {
        selector.onLineWidthChanged(width);

        QCOMPARE(selector.m_inputState.annotationWidth, width);
        QCOMPARE(selector.m_toolManager->width(), width);
        QCOMPARE(settings.loadWidthForTool(ToolId::Mosaic), width);
        QCOMPARE(settings.loadWidthForTool(ToolId::Pencil), 7);

        const QCursor cursor = selector.cursor();
        QVERIFY(!cursor.pixmap().isNull());
        QCOMPARE(cursor.shape(), Qt::BitmapCursor);

        const int brushFootprint = width * 2;
        const int expectedLogicalExtent = brushFootprint + (cursorPadding * 2);
        const qreal cursorDpr = cursor.pixmap().devicePixelRatio();
        const qreal expectedDeviceIndependentExtent =
            qCeil(expectedLogicalExtent * cursorDpr) / cursorDpr;
        const QSizeF expectedCursorSize(expectedDeviceIndependentExtent,
                                        expectedDeviceIndependentExtent);
        QCOMPARE(cursor.pixmap().deviceIndependentSize(), expectedCursorSize);
        QCOMPARE(cursor.hotSpot(),
                 QPoint(expectedLogicalExtent / 2,
                        expectedLogicalExtent / 2));
    }

    selector.m_toolManager->setCurrentTool(ToolId::Pencil);
    QCOMPARE(selector.m_toolManager->width(), 7);
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

void TestRegionSelectorStyleSync::testInitialRevealTimeoutRevealsReadySelector()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No screens available for RegionSelector initial reveal test.");
    }

    RegionSelector selector;
    const QSize size = screen->geometry().size().boundedTo(QSize(320, 240));
    QPixmap preCapture(size);
    preCapture.fill(Qt::darkBlue);

    selector.initializeForScreen(screen, preCapture);
    selector.show();
    QCoreApplication::processEvents();

    RegionSelectorTestAccess::markInitialRevealReadyToReveal(selector);
    selector.setWindowOpacity(0.0);

    QVERIFY(!RegionSelectorTestAccess::initialRevealStateIsRevealed(selector));

    RegionSelectorTestAccess::invokeHandleInitialRevealTimeout(selector);

    QVERIFY(RegionSelectorTestAccess::initialRevealStateIsRevealed(selector));
    QCOMPARE(selector.windowOpacity(), 1.0);

    selector.close();
}

QTEST_MAIN(TestRegionSelectorStyleSync)
#include "tst_StyleSync.moc"
