#include <QtTest/QtTest>

#include <QApplication>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>
#include <QWindow>
#include <QMenu>
#include <QContextMenuEvent>
#include <QScopeGuard>
#include <QtMath>

#include "PinWindow.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/TextBoxAnnotation.h"
#include "region/TextAnnotationEditor.h"
#include "annotations/EmojiStickerAnnotation.h"
#include "PinWindowManager.h"
#include "pinwindow/PinWindowPlacement.h"
#include "PlatformFeatures.h"
#include "qml/PinToolOptionsViewModel.h"
#include "qml/PinToolbarViewModel.h"
#include "qml/QmlEmojiPickerPopup.h"
#include "qml/QmlBeautifyPanel.h"
#include "qml/QmlFloatingSubToolbar.h"
#include "qml/QmlWindowedToolbar.h"
#include "annotations/PolylineAnnotation.h"
#include "annotations/MosaicRectAnnotation.h"
#include "cursor/CursorAuthority.h"
#include "cursor/CursorManager.h"
#include "cursor/CursorStyleCatalog.h"
#include "pinwindow/RegionLayoutManager.h"
#include "settings/AnnotationSettingsManager.h"
#include "annotation/ColorPickOverlay.h"
#include "settings/Settings.h"
#include "settings/FileSettingsManager.h"
#include "beautify/BeautifySettings.h"
#include <QTemporaryDir>
#include "tools/ToolManager.h"

namespace {
constexpr int kToolbarOutsideClickGuardMs = 350;

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

QPixmap createTestPixmap(int width = 160, int height = 120)
{
    QPixmap pixmap(width, height);
    pixmap.fill(Qt::red);
    return pixmap;
}

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

private:
    static std::unique_ptr<PinWindow> arrowPin(QPixmap pixmap);

private slots:
    void testLongScreenshotAnnotationToolbar();
    void initTestCase();
    void testUsesAuthorityModeByDefault();
    void testAnnotationToolUsesWindowDevicePixelRatio();
    void testStrokeAndMosaicWidthsRestoreIndependently();
    void selectedArrowStyleIsUndoable();
    void colorPickSamplesPinnedPixelsNotAnnotations();
    void pickedColorEditsSelectionUndoably();
    void arrowEndpointDragIsUndoable();
    void annotationSnapRelease_data();
    void annotationSnapRelease();
    void emojiMoveIsUndoable();
    void escapeMidDragRestores();
    void toolbarUndoDisabledWhileDragging();
    void textDragEscapeRestores();
    void testMosaicPresetsUpdateCursorWithoutChangingStrokeWidth();
    void testNonAnnotationEdgeHoverUsesCorrectResizeCursor();
    void testOverlayRestoreReturnsArrowToolCursor();
    void testPolylineReleaseRecomputesHoverCursor();
    void testReleaseOverFloatingToolbarFinishesWindowDrag();
    void testAutoBlurMapsRotatedDisplayRectToAnnotationSpace();
    void testAutoBlurGaussianCoversMappedHiDpiRegion();
    void testAutomaticSavesPreserveEarlierImage_data();
    void testAutomaticSavesPreserveEarlierImage();
    void testAutoBlurMapsFlippedDisplayRectToAnnotationSpace();
    void testAutoBlurMapsCombinedRotationAndFlips();
    void testAutoBlurAnnotationSourceRestoresCombinedOrientation();
    void testAutoBlurGenerationInvalidatesOnTransformChange();
    void testLiveCaptureWaitsForAutoBlur();
    void testLiveCaptureVersionGate_data();
    void testLiveCaptureVersionGate();
    void testPopupRestoreReturnsMosaicCursor();
    void testTemporaryToolbarHideRestoresEmojiPicker();
    void testExplicitToolbarHideClearsEmojiToolState();
    void testOutsideClickHidesEmojiPickerWithToolbar();
    void testApplicationDeactivateHidesEmojiPickerWithToolbar();
    void testBeautifyPanelMarksToolbarButtonActive();
    void testSelectingBeautifyClearsPreviousToolSelection();
    void testSelectingOtherToolbarToolDismissesBeautifyPanel();
    void testTriggeringToolbarActionDismissesBeautifyPanel();
    void testSpaceShortcutDismissesBeautifyPanelWithToolbar();
    void testEscapeShortcutDismissesBeautifyPanelWithToolbar();
    void testRegionLayoutMoveCursorUsesAuthority();
    void testRegionLayoutRotatedHandleUsesVisualCursorDirection();
    void testLinuxBypassPinDoesNotBecomeToolbarTransientParent();
    void testLinuxBypassPinReceivesSpaceShortcutAfterShow();
    void testLinuxBypassPinReceivesSpaceShortcutAfterClickFocus();
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

void TestPinWindowStyleSync::testAnnotationToolUsesWindowDevicePixelRatio()
{
    PinWindow window(createTestPixmap(240, 160), QPoint(0, 0));
    if (!window.m_toolManager) {
        window.initializeAnnotationComponents();
    }

    QVERIFY(window.m_toolManager != nullptr);
    QCOMPARE(window.m_toolManager->context()->devicePixelRatio, window.devicePixelRatioF());
}

void TestPinWindowStyleSync::testStrokeAndMosaicWidthsRestoreIndependently()
{
    ScopedWidthSettings restoreSettings;
    auto& settings = AnnotationSettingsManager::instance();
    settings.saveWidthForTool(ToolId::Pencil, 4);
    settings.saveWidthForTool(ToolId::Mosaic, 18);

    PinWindow window(createTestPixmap(240, 160), QPoint(0, 0));
    if (!window.m_toolManager) {
        window.initializeAnnotationComponents();
    }

    QVERIFY(window.m_subToolbar);
    auto* optionsVM = window.m_subToolbar->viewModel();
    QVERIFY(optionsVM);
    QCOMPARE(window.m_annotationWidth, 4);
    QCOMPARE(window.m_toolManager->width(), 4);
    QCOMPARE(optionsVM->currentWidth(), 4);

    window.handleToolbarToolSelected(static_cast<int>(ToolId::Pencil));
    QCOMPARE(window.m_currentToolId, ToolId::Pencil);
    QCOMPARE(window.m_annotationWidth, 4);
    QCOMPARE(window.m_toolManager->width(), 4);
    QCOMPARE(optionsVM->currentWidth(), 4);

    optionsVM->handleWidthChanged(6);
    QCOMPARE(window.m_annotationWidth, 6);
    QCOMPARE(window.m_toolManager->width(), 6);
    QCOMPARE(settings.loadWidthForTool(ToolId::Pencil), 6);
    QCOMPARE(settings.loadWidthForTool(ToolId::Mosaic), 18);

    window.handleToolbarToolSelected(static_cast<int>(ToolId::Mosaic));
    QCOMPARE(window.m_currentToolId, ToolId::Mosaic);
    QCOMPARE(window.m_annotationWidth, 18);
    QCOMPARE(window.m_toolManager->width(), 18);
    QCOMPARE(optionsVM->currentWidth(), 18);

    optionsVM->handleMosaicWidthPresetSelected(30);
    QCOMPARE(window.m_annotationWidth, 30);
    QCOMPARE(window.m_toolManager->width(), 30);
    QCOMPARE(settings.loadWidthForTool(ToolId::Pencil), 6);
    QCOMPARE(settings.loadWidthForTool(ToolId::Mosaic), 30);

    window.handleToolbarToolSelected(static_cast<int>(ToolId::Pencil));
    QCOMPARE(window.m_currentToolId, ToolId::Pencil);
    QCOMPARE(window.m_annotationWidth, 6);
    QCOMPARE(window.m_toolManager->width(), 6);
    QCOMPARE(optionsVM->currentWidth(), 6);
}

void TestPinWindowStyleSync::testMosaicPresetsUpdateCursorWithoutChangingStrokeWidth()
{
    ScopedWidthSettings restoreSettings;
    auto& settings = AnnotationSettingsManager::instance();
    settings.saveWidthForTool(ToolId::Pencil, 7);
    settings.saveWidthForTool(ToolId::Mosaic, 18);

    PinWindow window(createTestPixmap(240, 160), QPoint(0, 0));
    if (!window.m_toolManager) {
        window.initializeAnnotationComponents();
    }

    QVERIFY(window.m_subToolbar);
    auto* optionsVM = window.m_subToolbar->viewModel();
    QVERIFY(optionsVM);

    window.handleToolbarToolSelected(static_cast<int>(ToolId::Mosaic));
    QCOMPARE(window.m_currentToolId, ToolId::Mosaic);
    QCOMPARE(window.m_toolManager->currentTool(), ToolId::Mosaic);

    constexpr int cursorPadding = 4;
    const QList<int> presetWidths{10, 18, 30};
    for (const int width : presetWidths) {
        optionsVM->handleMosaicWidthPresetSelected(width);

        QCOMPARE(window.m_annotationWidth, width);
        QCOMPARE(window.m_toolManager->width(), width);
        QCOMPARE(optionsVM->currentWidth(), width);
        QCOMPARE(settings.loadWidthForTool(ToolId::Mosaic), width);
        QCOMPARE(settings.loadWidthForTool(ToolId::Pencil), 7);

        const QCursor cursor = window.cursor();
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

    window.handleToolbarToolSelected(static_cast<int>(ToolId::Pencil));
    QCOMPARE(window.m_annotationWidth, 7);
    QCOMPARE(window.m_toolManager->width(), 7);
    QCOMPARE(optionsVM->currentWidth(), 7);
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
    verifyMoveCursor(window.cursor());
}

void TestPinWindowStyleSync::testReleaseOverFloatingToolbarFinishesWindowDrag()
{
    PinWindow window(createTestPixmap(240, 160), QPoint(0, 0));
    window.showToolbar();
    QVERIFY(window.m_toolbar);
    QVERIFY(window.m_toolbar->isVisible());

    window.m_isDragging = true;
    const QPoint globalPos = window.m_toolbar->geometry().center();
    QVERIFY(window.isGlobalPosOverFloatingUi(globalPos));

    const QPoint localPos = window.mapFromGlobal(globalPos);
    QMouseEvent releaseEvent(QEvent::MouseButtonRelease,
                             localPos,
                             globalPos,
                             Qt::LeftButton,
                             Qt::NoButton,
                             Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &releaseEvent);

    QVERIFY(!window.m_isDragging);
}

void TestPinWindowStyleSync::testAutomaticSavesPreserveEarlierImage_data()
{
    QTest::addColumn<bool>("beautify");
    QTest::newRow("pin") << false;
    QTest::newRow("beautify") << true;
}

void TestPinWindowStyleSync::testAutomaticSavesPreserveEarlierImage()
{
    QFETCH(bool, beautify);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto& settings = FileSettingsManager::instance();
    settings.saveScreenshotPath(dir.path());
    settings.saveAutoSaveScreenshots(true);
    settings.saveFilenameTemplate("same.png");
    QPixmap red(24, 24), blue(24, 24);
    red.fill(Qt::red);
    blue.fill(Qt::blue);
    PinWindow first(red, QPoint(0, 0)), second(blue, QPoint(0, 0));
    QSignalSpy a(&first, &PinWindow::saveCompleted), b(&second, &PinWindow::saveCompleted);
    BeautifySettings style;
    style.padding = 0;
    style.cornerRadius = 0;
    style.shadowEnabled = false;
    if (beautify) {
        first.onBeautifySave(style);
        second.onBeautifySave(style);
    } else {
        first.saveToFile();
        second.saveToFile();
    }
    QCOMPARE(a.count(), 1);
    QCOMPARE(b.count(), 1);
    const QString firstPath = a.first().at(1).toString();
    const QString secondPath = b.first().at(1).toString();
    QVERIFY(firstPath != secondPath);
    QCOMPARE(QImage(firstPath).pixelColor(12, 12), QColor(Qt::red));
    QCOMPARE(QImage(secondPath).pixelColor(12, 12), QColor(Qt::blue));
}

void TestPinWindowStyleSync::testAutoBlurGaussianCoversMappedHiDpiRegion()
{
    QPixmap display(200, 240);
    display.fill(Qt::red);
    display.setDevicePixelRatio(2.0);
    const QPixmap source = PinWindow::buildAutoBlurAnnotationSource(display, 90, false, false);
    const QRect rect = PinWindow::mapAutoBlurDetectionRect(
        QRect(20, 40, 60, 80), 2.0, display.size() / 2, source.size() / 2, 90, false, false);
    QVERIFY(!rect.isEmpty());
    MosaicRectAnnotation annotation(rect, std::make_shared<const QPixmap>(source),
                                     12, MosaicBlurType::Gaussian);
    QImage output(source.size(), QImage::Format_ARGB32);
    output.setDevicePixelRatio(2.0);
    output.fill(Qt::transparent);
    { QPainter painter(&output); annotation.draw(painter); }
    for (int y = rect.y() * 2; y < (rect.y() + rect.height()) * 2; ++y)
        for (int x = rect.x() * 2; x < (rect.x() + rect.width()) * 2; ++x)
            QCOMPARE(output.pixelColor(x, y), QColor(Qt::red));
}

void TestPinWindowStyleSync::testAutoBlurMapsRotatedDisplayRectToAnnotationSpace()
{
    QPixmap rotatedDisplay(100, 200);
    rotatedDisplay.fill(Qt::red);
    rotatedDisplay.setDevicePixelRatio(1.0);

    const QPixmap annotationSource = PinWindow::buildAutoBlurAnnotationSource(
        rotatedDisplay, 90, false, false);
    QCOMPARE(annotationSource.size(), QSize(200, 100));

    const QRect mapped = PinWindow::mapAutoBlurDetectionRect(
        QRect(10, 20, 30, 40),
        1.0,
        QSize(100, 200),
        QSize(200, 100),
        90,
        false,
        false);
    QCOMPARE(mapped, QRect(20, 60, 40, 30));
}

void TestPinWindowStyleSync::testAutoBlurMapsFlippedDisplayRectToAnnotationSpace()
{
    QPixmap flippedDisplay(200, 100);
    flippedDisplay.fill(Qt::red);
    flippedDisplay.setDevicePixelRatio(1.0);

    const QPixmap annotationSource = PinWindow::buildAutoBlurAnnotationSource(
        flippedDisplay, 0, true, false);
    QCOMPARE(annotationSource.size(), QSize(200, 100));

    const QRect mapped = PinWindow::mapAutoBlurDetectionRect(
        QRect(10, 20, 30, 40),
        1.0,
        QSize(200, 100),
        QSize(200, 100),
        0,
        true,
        false);
    QCOMPARE(mapped, QRect(160, 20, 30, 40));
}

void TestPinWindowStyleSync::testAutoBlurMapsCombinedRotationAndFlips()
{
    const QSize displaySize(100, 200);
    const QSize annotationSize(200, 100);
    const QRect detectionRect(10, 20, 30, 40);

    QCOMPARE(PinWindow::mapAutoBlurDetectionRect(
                 detectionRect, 1.0, displaySize, annotationSize,
                 90, true, false),
             QRect(140, 60, 40, 30));
    QCOMPARE(PinWindow::mapAutoBlurDetectionRect(
                 detectionRect, 1.0, displaySize, annotationSize,
                 90, false, true),
             QRect(20, 10, 40, 30));
}

void TestPinWindowStyleSync::testAutoBlurAnnotationSourceRestoresCombinedOrientation()
{
    QPixmap annotation(200, 100);
    annotation.fill(Qt::transparent);
    {
        QPainter painter(&annotation);
        painter.fillRect(QRect(0, 0, 100, 50), Qt::red);
        painter.fillRect(QRect(100, 0, 100, 50), Qt::green);
        painter.fillRect(QRect(0, 50, 100, 50), Qt::blue);
        painter.fillRect(QRect(100, 50, 100, 50), Qt::yellow);
    }

    QTransform orientation;
    orientation.rotate(90);
    orientation.scale(-1.0, 1.0);
    const QPixmap display = annotation.transformed(
        orientation, Qt::SmoothTransformation);
    const QPixmap restored = PinWindow::buildAutoBlurAnnotationSource(
        display, 90, true, false);

    QCOMPARE(restored.size(), annotation.size());
    const QImage restoredImage = restored.toImage();
    QCOMPARE(restoredImage.pixelColor(25, 25), QColor(Qt::red));
    QCOMPARE(restoredImage.pixelColor(175, 25), QColor(Qt::green));
    QCOMPARE(restoredImage.pixelColor(25, 75), QColor(Qt::blue));
    QCOMPARE(restoredImage.pixelColor(175, 75), QColor(Qt::yellow));
}

void TestPinWindowStyleSync::testAutoBlurGenerationInvalidatesOnTransformChange()
{
    PinWindow window(createTestPixmap(240, 160), QPoint(0, 0));
    const quint64 requestGeneration = window.m_autoBlurContentGeneration;
    QVERIFY(window.isAutoBlurRequestCurrent(requestGeneration));

    window.rotateRight();

    QVERIFY(!window.isAutoBlurRequestCurrent(requestGeneration));
}

void TestPinWindowStyleSync::testLiveCaptureWaitsForAutoBlur()
{
    PinWindow window(createTestPixmap(240, 160), QPoint(0, 0));
    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);

    const QRect sourceRegion(screen->geometry().topLeft(), QSize(100, 80));
    window.setSourceRegion(sourceRegion, screen);
    window.m_autoBlurInProgress = true;
    const quint64 requestGeneration = window.m_autoBlurContentGeneration;

    window.startLiveCapture();

    QVERIFY(!window.m_isLiveMode);
    QVERIFY(!window.m_captureEngine);
    QCOMPARE(window.m_autoBlurContentGeneration, requestGeneration);
    window.m_autoBlurInProgress = false;
}

void TestPinWindowStyleSync::testLiveCaptureVersionGate_data()
{
    QTest::addColumn<int>("build");
    QTest::addColumn<bool>("supported");
    QTest::newRow("1809") << 17763 << false;
    QTest::newRow("1909") << 18363 << false;
    QTest::newRow("2004") << 19041 << true;
    QTest::newRow("22H2") << 19045 << true;
    QTest::newRow("Windows11") << 22000 << true;
}

void TestPinWindowStyleSync::testLiveCaptureVersionGate()
{
    QFETCH(int, build);
    QFETCH(bool, supported);
    // Scope the simulated platform to this test process; no native capture is
    // started. Restore even if an assertion exits the test early.
    auto& capabilities = const_cast<SnapTray::PlatformCapabilities&>(
        PlatformFeatures::instance().capabilities());
    const auto saved = capabilities;
    const auto restore = qScopeGuard([&] { capabilities = saved; });
    capabilities = SnapTray::capabilitiesForPlatform(SnapTray::PlatformKind::Windows,
        SnapTray::DisplayServerKind::Unknown,
        QOperatingSystemVersion(QOperatingSystemVersion::Windows, 10, 0, build));
    PinWindow window(createTestPixmap(240, 160), QPoint(0, 0));
    auto* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);
    window.setSourceRegion(QRect(screen->geometry().topLeft(), QSize(100, 80)), screen);
    QContextMenuEvent context(QContextMenuEvent::Mouse, QPoint(1, 1), QPoint(1, 1));
    window.contextMenuEvent(&context);
    QVERIFY(window.m_startLiveAction);
    QCOMPARE(window.m_startLiveAction->isEnabled(), supported);
    window.m_contextMenu->hide();
    if (!supported) {
        QVERIFY(window.m_startLiveAction->text().contains("2004"));
        window.startLiveCapture();
        QVERIFY(!window.m_captureEngine);
        QVERIFY(!window.m_isLiveMode);
        QKeyEvent key(QEvent::KeyPress, Qt::Key_L, Qt::NoModifier);
        window.keyPressEvent(&key);
        QVERIFY(!window.m_captureEngine);
        QVERIFY(!window.m_isLiveMode);
    }
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

void TestPinWindowStyleSync::testBeautifyPanelMarksToolbarButtonActive()
{
    PinWindow window(createTestPixmap(), QPoint(0, 0));
    window.showToolbar();

    QVERIFY(window.m_toolbar);
    window.showBeautifyPanel();

    QCOMPARE(window.m_toolbar->viewModel()->activeTool(),
             static_cast<int>(ToolId::Beautify));
}

void TestPinWindowStyleSync::testSelectingBeautifyClearsPreviousToolSelection()
{
    PinWindow window(createTestPixmap(), QPoint(0, 0));

    if (!window.m_toolManager) {
        window.initializeAnnotationComponents();
    }

    window.showToolbar();
    window.enterAnnotationMode();
    window.m_currentToolId = ToolId::Crop;
    window.m_toolManager->setCurrentTool(ToolId::Crop);
    window.m_toolbar->viewModel()->setActiveTool(static_cast<int>(ToolId::Crop));

    window.showBeautifyPanel();
    QVERIFY(!window.isAnnotationMode());
    QCOMPARE(window.m_currentToolId, ToolId::Selection);
    QCOMPARE(window.m_toolbar->viewModel()->activeTool(),
             static_cast<int>(ToolId::Beautify));

    QVERIFY(window.m_beautifyPanel);
    window.m_beautifyPanel->hide();
    window.syncToolbarActiveButtonForVisibleState();

    QCOMPARE(window.m_toolbar->viewModel()->activeTool(), -1);
}

void TestPinWindowStyleSync::testSelectingOtherToolbarToolDismissesBeautifyPanel()
{
    PinWindow window(createTestPixmap(), QPoint(0, 0));
    window.showToolbar();
    window.showBeautifyPanel();

    QVERIFY(window.m_beautifyPanel);
    QVERIFY(window.m_beautifyPanel->isVisible());

    window.m_toolbar->viewModel()->handleButtonClicked(static_cast<int>(ToolId::Crop));

    QVERIFY(!window.m_beautifyPanel->isVisible());
    QCOMPARE(window.m_toolbar->viewModel()->activeTool(),
             static_cast<int>(ToolId::Crop));
}

void TestPinWindowStyleSync::testTriggeringToolbarActionDismissesBeautifyPanel()
{
    PinWindow window(createTestPixmap(), QPoint(0, 0));
    window.showToolbar();
    window.showBeautifyPanel();

    QVERIFY(window.m_beautifyPanel);
    QVERIFY(window.m_beautifyPanel->isVisible());

    window.m_toolbar->viewModel()->handleButtonClicked(static_cast<int>(ToolId::Undo));

    QVERIFY(!window.m_beautifyPanel->isVisible());
    QCOMPARE(window.m_toolbar->viewModel()->activeTool(), -1);
}

void TestPinWindowStyleSync::testSpaceShortcutDismissesBeautifyPanelWithToolbar()
{
    PinWindow window(createTestPixmap(), QPoint(0, 0));
    window.showToolbar();
    window.showBeautifyPanel();

    QVERIFY(window.isToolbarVisible());
    QVERIFY(window.m_beautifyPanel);
    QVERIFY(window.m_beautifyPanel->isVisible());

    QKeyEvent event(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &event);

    QVERIFY(!window.isToolbarVisible());
    QVERIFY(!window.m_beautifyPanel->isVisible());
}

void TestPinWindowStyleSync::testEscapeShortcutDismissesBeautifyPanelWithToolbar()
{
    PinWindow window(createTestPixmap(), QPoint(0, 0));
    window.showToolbar();
    window.showBeautifyPanel();

    QVERIFY(window.isToolbarVisible());
    QVERIFY(window.m_beautifyPanel);
    QVERIFY(window.m_beautifyPanel->isVisible());

    QKeyEvent event(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &event);

    QVERIFY(!window.isToolbarVisible());
    QVERIFY(!window.m_beautifyPanel->isVisible());
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
    verifyMoveCursor(window.cursor());
}

void TestPinWindowStyleSync::testRegionLayoutRotatedHandleUsesVisualCursorDirection()
{
    PinWindow window(createTestPixmap(240, 160), QPoint(0, 0));
    auto& authority = CursorAuthority::instance();

    LayoutRegion region;
    region.rect = QRect(0, 0, 240, 160);
    region.originalRect = region.rect;
    region.image = QImage(region.rect.size(), QImage::Format_ARGB32_Premultiplied);
    region.image.fill(Qt::blue);
    region.index = 1;

    window.rotateRight();
    window.setMultiRegionData({region});
    window.enterRegionLayoutMode();
    window.m_regionLayoutManager->selectRegion(0);

    const QPoint modelTop(region.rect.center().x(), region.rect.top());
    const QPoint viewTop = window.regionLayoutViewTransform(QSize(240, 160))
                               .map(QPointF(modelTop)).toPoint();
    QCOMPARE(window.regionLayoutHandleAtWidget(viewTop), ResizeHandler::Edge::Top);

    QMouseEvent moveEvent(QEvent::MouseMove, viewTop, window.mapToGlobal(viewTop),
                          Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &moveEvent);

    QCOMPARE(authority.resolvedSourceForWidget(&window), CursorRequestSource::LayoutMode);
    QCOMPARE(authority.resolvedStyleForWidget(&window).styleId,
             CursorStyleId::ResizeHorizontal);
}

void TestPinWindowStyleSync::testLinuxBypassPinDoesNotBecomeToolbarTransientParent()
{
#ifndef Q_OS_LINUX
    QSKIP("Linux-only toolbar transient-parent policy.");
#else
    PinWindow window(createTestPixmap(), QPoint(0, 0), nullptr, false, false);
    QVERIFY(window.windowFlags().testFlag(Qt::X11BypassWindowManagerHint));

    window.showPreparedWindow();
    QCoreApplication::processEvents();
    QVERIFY(window.windowHandle());

    SnapTray::QmlWindowedToolbar toolbar;
    toolbar.setAssociatedWidgets(&window, nullptr);
    toolbar.show();
    QCoreApplication::processEvents();

    QVERIFY(toolbar.window());
    QVERIFY(toolbar.window()->transientParent() != window.windowHandle());

    toolbar.close();
    window.close();
#endif
}

void TestPinWindowStyleSync::testLongScreenshotAnnotationToolbar()
{
    PinWindowManager manager;
    const auto pixmap = createTestPixmap(800, 30000);
    const auto placement = computeInitialPinWindowPlacement(pixmap, QRect(0, 0, 1920, 1080));
    auto* pin = manager.createPinWindow(pixmap, placement.position, true, placement.zoomLevel);
    pin->toggleToolbar();
    QCoreApplication::processEvents();
    QVERIFY(pin->isToolbarVisible());
    QCOMPARE(pin->size(), placement.displaySize);
    pin->toggleToolbar();
    QVERIFY(!pin->isToolbarVisible());
}

void TestPinWindowStyleSync::testLinuxBypassPinReceivesSpaceShortcutAfterShow()
{
#ifndef Q_OS_LINUX
    QSKIP("Linux-only pin focus policy.");
#else
    PinWindow window(createTestPixmap(), QPoint(0, 0), nullptr, false, false);
    QVERIFY(window.windowFlags().testFlag(Qt::X11BypassWindowManagerHint));

    window.showPreparedWindow();
    window.showToolbar();
    QCoreApplication::processEvents();
    QVERIFY(window.isToolbarVisible());
    QTRY_COMPARE_WITH_TIMEOUT(QApplication::focusWidget(), &window, 1000);

    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Space);
    QCoreApplication::processEvents();
    QVERIFY(!window.isToolbarVisible());

    window.close();
#endif
}

void TestPinWindowStyleSync::testLinuxBypassPinReceivesSpaceShortcutAfterClickFocus()
{
#ifndef Q_OS_LINUX
    QSKIP("Linux-only pin focus policy.");
#else
    PinWindow window(createTestPixmap(), QPoint(0, 0), nullptr, false, false);
    QVERIFY(window.windowFlags().testFlag(Qt::X11BypassWindowManagerHint));

    window.showPreparedWindow();
    window.showToolbar();
    QCoreApplication::processEvents();
    QVERIFY(window.isToolbarVisible());

    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
    QCoreApplication::processEvents();
    QCOMPARE(QApplication::focusWidget(), &window);

    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Space);
    QCoreApplication::processEvents();
    QVERIFY(!window.isToolbarVisible());

    window.close();
#endif
}


void TestPinWindowStyleSync::selectedArrowStyleIsUndoable()
{
    auto& settings = AnnotationSettingsManager::instance();
    const QColor savedColor = settings.loadColor();
    const int savedWidth = settings.loadWidthForTool(ToolId::Arrow);
    const int savedMarker = settings.loadWidthForTool(ToolId::Marker);
    const auto restore = qScopeGuard([&] {
        settings.saveColor(savedColor);
        settings.saveWidthForTool(ToolId::Marker, savedMarker);
        settings.saveWidthForTool(ToolId::Arrow, savedWidth);
    });
    PinWindow window(createTestPixmap(240, 160), QPoint(0, 0));
    if (!window.m_toolManager) {
        window.initializeAnnotationComponents();
    }
    window.handleToolbarToolSelected(static_cast<int>(ToolId::Arrow));
    window.m_annotationLayer->addItem(
        std::make_unique<ArrowAnnotation>(QPoint(20, 20), QPoint(120, 20), Qt::red, 3));
    window.m_annotationLayer->setSelectedIndex(0);
    auto* vm = window.m_subToolbar->viewModel();
    QVERIFY(vm->editingSelection());
    QCOMPARE(vm->targetLabel(), QStringLiteral("Selected: Arrow"));
    vm->handleWidthChanged(11);
    auto arrow = [&] { return static_cast<ArrowAnnotation*>(window.m_annotationLayer->itemAt(0)); };
    QCOMPARE(arrow()->width(), 11);
    QCOMPARE(settings.loadWidthForTool(ToolId::Arrow), savedWidth);
    // The line style menu result also edits the selection.
    window.onLineStyleChanged(LineStyle::Dotted);
    QCOMPARE(arrow()->lineStyle(), LineStyle::Dotted);
    window.handleToolbarUndo();
    QCOMPARE(arrow()->lineStyle(), LineStyle::Solid);
    window.handleToolbarUndo();
    QCOMPARE(arrow()->width(), 3);
    window.handleToolbarRedo();
    QCOMPARE(arrow()->width(), 11);
    QCOMPARE(settings.loadColor(), savedColor);
    // Picking a tool ends the selection and returns the strip to that tool,
    // showing that tool's own width (review #4).
    settings.saveWidthForTool(ToolId::Marker, 20);
    window.handleToolbarToolSelected(static_cast<int>(ToolId::Marker));
    QVERIFY(!vm->editingSelection());
    QCOMPARE(vm->currentWidth(), window.m_toolManager->width());
}

namespace {
// Every pixel has its own color, so a wrong coordinate shows.
QImage uniquePixels(const QSize& size)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < size.height(); ++y)
        for (int x = 0; x < size.width(); ++x)
            image.setPixelColor(x, y, QColor(x * 5 % 256, y * 7 % 256, (x + y) % 256));
    return image;
}
}  // namespace

// #271: the eyedropper samples the pinned image as shown, never annotations,
// and a zoomed pin yields original pixels rather than blends of neighbours.
void TestPinWindowStyleSync::colorPickSamplesPinnedPixelsNotAnnotations()
{
    const QImage original = uniquePixels(QSize(40, 30));
    PinWindow window(QPixmap::fromImage(original), QPoint(0, 0));
    if (!window.m_toolManager) window.initializeAnnotationComponents();
    window.m_annotationLayer->addItem(
        std::make_unique<ArrowAnnotation>(QPoint(0, 15), QPoint(40, 15), Qt::blue, 12));

    window.setZoomLevel(2.0);
    ColorPickSource source = window.colorPickSource();
    QVERIFY(source.isValid());
    QCOMPARE(source.sampleRect.size(), QSize(80, 60));
    QCOMPARE(source.image.size(), QSize(80, 60));
    for (const QPoint& pixel : {QPoint(0, 0), QPoint(13, 15), QPoint(39, 29), QPoint(20, 14)}) {
        for (const QPoint& sub : {QPoint(0, 0), QPoint(1, 1)}) {
            QCOMPARE(ColorPick::colorAt(source.image, pixel * 2 + sub),
                     original.pixelColor(pixel));
        }
    }

    // Rotation and flips are part of the image as shown.
    window.setZoomLevel(1.0);
    window.rotateRight();
    source = window.colorPickSource();
    const QImage rotated = original.transformed(QTransform().rotate(90));
    QCOMPARE(source.image.size(), rotated.size());
    QCOMPARE(ColorPick::colorAt(source.image, QPoint(3, 31)), rotated.pixelColor(3, 31));
    QCOMPARE(ColorPick::colorAt(source.image, QPoint(29, 0)), rotated.pixelColor(29, 0));
}

void TestPinWindowStyleSync::pickedColorEditsSelectionUndoably()
{
    auto& settings = AnnotationSettingsManager::instance();
    const QColor savedColor = settings.loadColor();
    const auto restore = qScopeGuard([&] { settings.saveColor(savedColor); });
    settings.saveColor(Qt::red);
    PinWindow window(createTestPixmap(240, 160), QPoint(0, 0));
    if (!window.m_toolManager) window.initializeAnnotationComponents();
    window.handleToolbarToolSelected(static_cast<int>(ToolId::Arrow));
    auto* vm = window.m_subToolbar->viewModel();
    const QColor picked(0x1a, 0x73, 0xe8);

    // Nothing selected: the picked color becomes the current, saved color.
    window.applyPickedColor(picked);
    QCOMPARE(vm->currentColor(), picked);
    QCOMPARE(window.m_toolManager->color(), picked);
    QCOMPARE(settings.loadColor(), picked);

    // A selected annotation takes it as one undoable change.
    settings.saveColor(Qt::red);
    window.m_annotationLayer->addItem(
        std::make_unique<ArrowAnnotation>(QPoint(20, 20), QPoint(120, 20), Qt::green, 3));
    window.m_annotationLayer->setSelectedIndex(0);
    QVERIFY(vm->editingSelection());
    window.applyPickedColor(picked);
    auto arrow = [&] { return static_cast<ArrowAnnotation*>(window.m_annotationLayer->itemAt(0)); };
    QCOMPARE(arrow()->color(), picked);
    QCOMPARE(settings.loadColor(), QColor(Qt::red));
    window.handleToolbarUndo();
    QCOMPARE(arrow()->color(), QColor(Qt::green));
}

// A PinWindow with the Arrow tool, so annotation handles take the mouse.
std::unique_ptr<PinWindow> TestPinWindowStyleSync::arrowPin(QPixmap pixmap)
{
    auto window = std::make_unique<PinWindow>(pixmap, QPoint(0, 0));
    if (!window->m_toolManager) window->initializeAnnotationComponents();
    window->handleToolbarToolSelected(static_cast<int>(ToolId::Arrow));
    return window;
}

// #237: dragging an arrow's end handle is one undo step.
void TestPinWindowStyleSync::arrowEndpointDragIsUndoable()
{
    auto window = arrowPin(createTestPixmap(240, 160));
    window->m_annotationLayer->addItem(
        std::make_unique<ArrowAnnotation>(QPoint(20, 20), QPoint(120, 20), Qt::red, 3));
    window->m_annotationLayer->setSelectedIndex(0);
    const auto view = [&](QPoint original) { return window->mapFromOriginalCoords(original); };
    auto arrow = [&] { return static_cast<ArrowAnnotation*>(window->m_annotationLayer->itemAt(0)); };
    QVERIFY(window->handleArrowAnnotationPress(view(QPoint(120, 20))));
    window->handleArrowAnnotationMove(view(QPoint(130, 40)));
    window->handleArrowAnnotationMove(view(QPoint(140, 50)));
    window->handleArrowAnnotationRelease(view(QPoint(140, 50)));
    QCOMPARE(arrow()->end(), QPoint(140, 50));
    window->handleToolbarUndo();
    QCOMPARE(window->m_annotationLayer->itemCount(), size_t(1));   // the edit, not the add
    QCOMPARE(arrow()->end(), QPoint(120, 20));
    window->handleToolbarRedo();
    QCOMPARE(arrow()->end(), QPoint(140, 50));
}

void TestPinWindowStyleSync::annotationSnapRelease_data()
{
    QTest::addColumn<qreal>("dpr"); QTest::addColumn<qreal>("zoom");
    for (qreal dpr : {1.0, 1.25, 1.5, 2.0}) for (qreal zoom : {0.5, 1.0, 2.0})
        QTest::newRow(qPrintable(QString("%1/%2").arg(dpr).arg(zoom))) << dpr << zoom;
}
void TestPinWindowStyleSync::annotationSnapRelease()
{
    QFETCH(qreal, dpr); QFETCH(qreal, zoom);
    auto pixmap = createTestPixmap(1600,1200); pixmap.setDevicePixelRatio(dpr);
    auto window = arrowPin(pixmap); window->setZoomLevel(zoom);
    AnnotationSettingsManager::instance().saveAlignmentAssist(true);
    auto* layer = window->m_annotationLayer;
    layer->addItem(std::make_unique<EmojiStickerAnnotation>(QPoint(200,100),QStringLiteral("🙂")));
    layer->addItem(std::make_unique<EmojiStickerAnnotation>(QPoint(140,220),QStringLiteral("🙂")));
    layer->setSelectedIndex(1);
    auto emoji = [&] { return static_cast<EmojiStickerAnnotation*>(layer->itemAt(1)); };
    QVERIFY(window->handleEmojiStickerAnnotationPress({140,220}));
    QVERIFY(window->handleEmojiStickerAnnotationMove({197,220}));
    QCOMPARE(emoji()->precisePosition(),QPointF(200,220));
    QKeyEvent altDown(QEvent::KeyPress,Qt::Key_Alt,Qt::AltModifier);
    window->keyPressEvent(&altDown); QCOMPARE(emoji()->precisePosition(),QPointF(197,220));
    QKeyEvent altUp(QEvent::KeyRelease,Qt::Key_Alt,Qt::NoModifier);
    window->keyReleaseEvent(&altUp); QCOMPARE(emoji()->precisePosition(),QPointF(200,220));
    QVERIFY(window->handleEmojiStickerAnnotationRelease({240,220}));
    QCOMPARE(emoji()->precisePosition(),QPointF(240,220));
    QVERIFY(layer->snapController().guideBounds().isEmpty());
    layer->undo(); QCOMPARE(emoji()->precisePosition(),QPointF(140,220));
    layer->redo(); QCOMPARE(emoji()->precisePosition(),QPointF(240,220));
    QCOMPARE(static_cast<EmojiStickerAnnotation*>(layer->itemAt(0))->position(),QPoint(200,100));
}

void TestPinWindowStyleSync::emojiMoveIsUndoable()
{
    auto window = arrowPin(createTestPixmap(240, 160));
    window->m_annotationLayer->addItem(
        std::make_unique<EmojiStickerAnnotation>(QPoint(100, 80), QStringLiteral("🙂"), 1.0));
    window->m_annotationLayer->setSelectedIndex(0);
    auto emoji = [&] { return static_cast<EmojiStickerAnnotation*>(window->m_annotationLayer->itemAt(0)); };
    const QPoint start = emoji()->center().toPoint();
    const auto view = [&](QPoint original) { return window->mapFromOriginalCoords(original); };
    QVERIFY(window->handleEmojiStickerAnnotationPress(view(start)));
    window->handleEmojiStickerAnnotationMove(view(start + QPoint(10, 0)));
    window->handleEmojiStickerAnnotationMove(view(start + QPoint(20, 5)));
    window->handleEmojiStickerAnnotationRelease(view(start + QPoint(20, 5)));
    QCOMPARE(emoji()->position(), QPoint(120, 85));
    window->handleToolbarUndo();
    QCOMPARE(window->m_annotationLayer->itemCount(), size_t(1));
    QCOMPARE(emoji()->position(), QPoint(100, 80));
}

void TestPinWindowStyleSync::escapeMidDragRestores()
{
    auto window = arrowPin(createTestPixmap(240, 160));
    window->m_annotationLayer->addItem(
        std::make_unique<ArrowAnnotation>(QPoint(20, 20), QPoint(120, 20), Qt::red, 3));
    window->m_annotationLayer->setSelectedIndex(0);
    const auto token = window->m_annotationLayer->historyStateToken();
    const auto view = [&](QPoint original) { return window->mapFromOriginalCoords(original); };
    auto arrow = [&] { return static_cast<ArrowAnnotation*>(window->m_annotationLayer->itemAt(0)); };
    QVERIFY(window->handleArrowAnnotationPress(view(QPoint(40, 20))));
    window->handleArrowAnnotationMove(view(QPoint(50, 50)));
    QCOMPARE(arrow()->start(), QPoint(30, 50));
    QVERIFY(!window->m_consumeNextToolRelease);
    QTest::keyClick(window.get(), Qt::Key_Escape);
    QVERIFY(window->m_consumeNextToolRelease);   // the held button's release places nothing
    QCOMPARE(arrow()->start(), QPoint(20, 20));
    QCOMPARE(window->m_annotationLayer->historyStateToken(), token);
    window->handleArrowAnnotationMove(view(QPoint(70, 70)));   // the rest of the drag does nothing
    window->handleArrowAnnotationRelease(view(QPoint(70, 70)));
    QCOMPARE(arrow()->start(), QPoint(20, 20));
}

void TestPinWindowStyleSync::toolbarUndoDisabledWhileDragging()
{
    auto window = arrowPin(createTestPixmap(240, 160));
    window->m_annotationLayer->addItem(
        std::make_unique<ArrowAnnotation>(QPoint(20, 20), QPoint(120, 20), Qt::red, 3));
    window->m_annotationLayer->setSelectedIndex(0);
    window->updateUndoRedoState();
    QVERIFY(window->m_toolbar->viewModel()->canUndo());
    const auto view = [&](QPoint original) { return window->mapFromOriginalCoords(original); };
    QVERIFY(window->handleArrowAnnotationPress(view(QPoint(40, 20))));
    window->handleArrowAnnotationMove(view(QPoint(50, 50)));
    QVERIFY(!window->m_toolbar->viewModel()->canUndo());
    window->handleArrowAnnotationRelease(view(QPoint(50, 50)));
    QVERIFY(window->m_toolbar->viewModel()->canUndo());
}

// Review #237-1: Escape during a text drag in a pin puts the text back.
void TestPinWindowStyleSync::textDragEscapeRestores()
{
    auto window = arrowPin(createTestPixmap(240, 160));
    window->m_annotationLayer->addItem(
        std::make_unique<TextBoxAnnotation>(QPointF(30, 30), QStringLiteral("Pin"), QFont(), Qt::red));
    window->m_annotationLayer->setSelectedIndex(0);
    auto text = [&] { return static_cast<TextBoxAnnotation*>(window->m_annotationLayer->itemAt(0)); };
    window->m_textAnnotationEditor->startDragging(QPoint(35, 35));
    window->m_textAnnotationEditor->updateDragging(QPoint(65, 55));
    QCOMPARE(text()->position(), QPointF(60, 50));
    QTest::keyClick(window.get(), Qt::Key_Escape);
    QVERIFY(window->m_consumeNextToolRelease);
    QCOMPARE(text()->position(), QPointF(30, 30));
    QVERIFY(!window->m_textAnnotationEditor->isDragging());
    QVERIFY(!window->m_annotationLayer->isGestureActive());
}

QTEST_MAIN(TestPinWindowStyleSync)
#include "tst_StyleSync.moc"
