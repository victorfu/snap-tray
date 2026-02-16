#include "PinWindow.h"
#include "annotation/AnnotationContext.h"
#include "PinWindowManager.h"
#include "OCRManager.h"
#include "QRCodeManager.h"
#include "QRCodeResultDialog.h"
#include "PlatformFeatures.h"
#include "WatermarkRenderer.h"
#include "cursor/CursorManager.h"
#include "platform/WindowLevel.h"
#include "capture/ICaptureEngine.h"
#include "pinwindow/ResizeHandler.h"
#include "pinwindow/UIIndicators.h"
#include "pinwindow/RegionLayoutManager.h"
#include "pinwindow/RegionLayoutRenderer.h"
#include "pinwindow/PinMergeHelper.h"
#include "pinwindow/PinHistoryStore.h"
#include "toolbar/WindowedToolbar.h"
#include "toolbar/WindowedSubToolbar.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/ErasedItemsGroup.h"
#include "annotations/EmojiStickerAnnotation.h"
#include "annotations/MosaicStroke.h"
#include "annotations/MosaicRectAnnotation.h"
#include "detection/AutoBlurManager.h"
#include "tools/ToolManager.h"
#include "tools/IToolHandler.h"
#include "tools/handlers/EmojiStickerToolHandler.h"
#include "tools/handlers/CropToolHandler.h"
#include "settings/AnnotationSettingsManager.h"
#include "settings/AutoBlurSettingsManager.h"
#include "settings/FileSettingsManager.h"
#include "settings/PinWindowSettingsManager.h"
#include "settings/BeautifySettingsManager.h"
#include "beautify/BeautifyPanel.h"
#include "beautify/BeautifyRenderer.h"
#include "settings/OCRSettingsManager.h"
#include "ui/GlobalToast.h"
#include "utils/FilenameTemplateEngine.h"
#include "utils/ImageSaveUtils.h"
#include "OCRResultDialog.h"
#include "InlineTextEditor.h"
#include "region/TextAnnotationEditor.h"
#include "region/RegionSettingsHelper.h"
#include "toolbar/ToolOptionsPanel.h"
#include "annotations/TextBoxAnnotation.h"
#include "TransformationGizmo.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/PolylineAnnotation.h"
#include "utils/CoordinateHelper.h"
#include "colorwidgets/ColorPickerDialogCompat.h"
#include "tools/ToolTraits.h"

using snaptray::colorwidgets::ColorPickerDialogCompat;



#include <QPainter>
#include <QImage>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QMoveEvent>
#include <QMenu>
#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QScreen>
#include <QtMath>
#include <QDateTime>
#include <QToolTip>
#include <QTextEdit>
#include <QLabel>
#include <QTimer>
#include <QPointer>
#include <QTransform>
#include <QFontMetrics>
#include <QDebug>
#include <QActionGroup>
#include <QDesktopServices>
#include <QUrl>
#include <QThreadPool>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <limits>

namespace {
    // Full opacity constant for pixel alpha check
    constexpr int kPixelFullOpacity = 255;
    constexpr qreal kTransformEpsilon = 1e-6;

    struct TextCompensation
    {
        qreal rotation = 0.0;
        bool mirrorX = false;
        bool mirrorY = false;
    };

    QSize logicalSizeFromPixmap(const QPixmap& pixmap)
    {
        if (pixmap.isNull()) {
            return QSize();
        }
        qreal dpr = pixmap.devicePixelRatio();
        if (dpr <= 0.0) {
            dpr = 1.0;
        }
        return CoordinateHelper::toLogical(pixmap.size(), dpr);
    }

    QString saveErrorDetail(const ImageSaveUtils::Error& error)
    {
        if (error.stage.isEmpty()) {
            return error.message.isEmpty() ? QStringLiteral("Unknown error") : error.message;
        }
        if (error.message.isEmpty()) {
            return error.stage;
        }
        return QStringLiteral("%1: %2").arg(error.stage, error.message);
    }
    
    // Optimized: Only copy 4 single-pixel regions instead of the entire image.
    // On high-DPI displays (e.g., 4K at 200% scaling), the original toImage()
    // would copy ~15MB of pixel data just to check 4 corner pixels.
    bool hasTransparentCornerPixels(const QPixmap& pixmap)
    {
        if (pixmap.isNull()) {
            return false;
        }

        int width = pixmap.width();
        int height = pixmap.height();
        if (width <= 0 || height <= 0) {
            return false;
        }

        const QPoint corners[] = {
            {0, 0},
            {width - 1, 0},
            {0, height - 1},
            {width - 1, height - 1}
        };

        for (const QPoint& corner : corners) {
            QImage cornerImg = pixmap.copy(corner.x(), corner.y(), 1, 1).toImage();
            if (cornerImg.isNull()) {
                continue;
            }
            if (qAlpha(cornerImg.pixel(0, 0)) < kPixelFullOpacity) {
                return true;
            }
        }
        return false;
    }

    QTransform buildPinWindowTransform(const QRectF& pixmapRect, int rotationAngle, bool flipHorizontal, bool flipVertical)
    {
        QTransform transform;

        if (rotationAngle == 90) {
            transform.translate(pixmapRect.width(), 0);
            transform.rotate(90);
        }
        else if (rotationAngle == 180) {
            transform.translate(pixmapRect.width(), pixmapRect.height());
            transform.rotate(180);
        }
        else if (rotationAngle == 270) {
            transform.translate(0, pixmapRect.height());
            transform.rotate(270);
        }

        if (flipHorizontal || flipVertical) {
            QPointF center(pixmapRect.width() / 2.0, pixmapRect.height() / 2.0);
            transform.translate(center.x(), center.y());
            if (flipHorizontal) transform.scale(-1, 1);
            if (flipVertical) transform.scale(1, -1);
            transform.translate(-center.x(), -center.y());
        }

        return transform;
    }

    QTransform buildOrientationLinearTransform(int rotationAngle, bool flipHorizontal, bool flipVertical)
    {
        QTransform transform;
        if (rotationAngle != 0) {
            transform.rotate(rotationAngle);
        }
        if (flipHorizontal || flipVertical) {
            transform.scale(flipHorizontal ? -1.0 : 1.0, flipVertical ? -1.0 : 1.0);
        }
        return transform;
    }

    bool sameLinearTransform(const QTransform& lhs, const QTransform& rhs)
    {
        return qAbs(lhs.m11() - rhs.m11()) <= kTransformEpsilon &&
               qAbs(lhs.m12() - rhs.m12()) <= kTransformEpsilon &&
               qAbs(lhs.m21() - rhs.m21()) <= kTransformEpsilon &&
               qAbs(lhs.m22() - rhs.m22()) <= kTransformEpsilon;
    }

    TextCompensation computeTextCompensation(int rotationAngle, bool flipHorizontal, bool flipVertical)
    {
        const QTransform windowOrientation = buildOrientationLinearTransform(rotationAngle, flipHorizontal, flipVertical);
        const QTransform target = windowOrientation.inverted();

        struct Candidate
        {
            qreal rotation;
            bool mirrorX;
            bool mirrorY;
        };

        static const Candidate kCandidates[] = {
            {0.0, false, false},
            {90.0, false, false},
            {180.0, false, false},
            {270.0, false, false},
            {0.0, true, false},
            {0.0, false, true},
            {90.0, true, false},
            {90.0, false, true},
            {180.0, true, false},
            {180.0, false, true},
            {270.0, true, false},
            {270.0, false, true},
            {0.0, true, true},
            {90.0, true, true},
            {180.0, true, true},
            {270.0, true, true}
        };

        bool found = false;
        int bestCost = (std::numeric_limits<int>::max)();
        TextCompensation best;

        for (const Candidate& candidate : kCandidates) {
            QTransform candidateTransform = buildOrientationLinearTransform(
                static_cast<int>(candidate.rotation), candidate.mirrorX, candidate.mirrorY);
            if (!sameLinearTransform(candidateTransform, target)) {
                continue;
            }

            const int flipCost = (candidate.mirrorX ? 1 : 0) + (candidate.mirrorY ? 1 : 0);
            const int rotationCost = (qFuzzyIsNull(candidate.rotation) ? 0 : 1);
            const int cost = flipCost * 10 + rotationCost;
            if (!found || cost < bestCost) {
                found = true;
                bestCost = cost;
                best.rotation = candidate.rotation;
                best.mirrorX = candidate.mirrorX;
                best.mirrorY = candidate.mirrorY;
            }
        }

        return found ? best : TextCompensation{};
    }

    void refreshMosaicSourceForItem(AnnotationItem* item, const SharedPixmap& sourcePixmap)
    {
        if (!item) {
            return;
        }

        if (auto* mosaic = dynamic_cast<MosaicStroke*>(item)) {
            mosaic->setSourcePixmap(sourcePixmap);
            return;
        }
        if (auto* mosaicRect = dynamic_cast<MosaicRectAnnotation*>(item)) {
            mosaicRect->setSourcePixmap(sourcePixmap);
            return;
        }
        if (auto* erasedGroup = dynamic_cast<ErasedItemsGroup*>(item)) {
            erasedGroup->forEachStoredItem([&sourcePixmap](AnnotationItem* storedItem) {
                refreshMosaicSourceForItem(storedItem, sourcePixmap);
            });
        }
    }

    void refreshAllMosaicSources(AnnotationLayer* layer, const SharedPixmap& sourcePixmap)
    {
        if (!layer) {
            return;
        }

        layer->forEachItem([&sourcePixmap](AnnotationItem* item) {
            refreshMosaicSourceForItem(item, sourcePixmap);
        }, true);
    }

    // Crop endpoints can land on the logical right/bottom boundary (x == width / y == height)
    // after inverse-transform mapping. Clamp using an endpoint-inclusive box first, then
    // convert back to pixel-index bounds so edge-aligned crops keep their final row/column.
    QRect clampCropRectToImagePixels(const QRect& cropRect, const QSize& imageSize)
    {
        if (!imageSize.isValid() || imageSize.isEmpty() || cropRect.isEmpty()) {
            return QRect();
        }

        const QRect endpointBounds(
            QPoint(0, 0),
            QSize(imageSize.width() + 1, imageSize.height() + 1));
        QRect clamped = cropRect.intersected(endpointBounds);
        if (clamped.isEmpty()) {
            return QRect();
        }

        const int maxX = imageSize.width() - 1;
        const int maxY = imageSize.height() - 1;
        clamped.setLeft(qBound(0, clamped.left(), maxX));
        clamped.setTop(qBound(0, clamped.top(), maxY));
        clamped.setRight(qBound(0, clamped.right(), maxX));
        clamped.setBottom(qBound(0, clamped.bottom(), maxY));

        return clamped.isValid() ? clamped : QRect();
    }
}  // namespace

PinWindow::PinWindow(const QPixmap& screenshot,
                     const QPoint& position,
                     QWidget* parent,
                     bool autoSaveToCache)
    : QWidget(parent)
    , m_originalPixmap(screenshot)
    , m_zoomLevel(1.0)
    , m_isDragging(false)
    , m_contextMenu(nullptr)
    , m_isResizing(false)
    , m_rotationAngle(0)
    , m_flipHorizontal(false)
    , m_flipVertical(false)
    , m_ocrManager(nullptr)
    , m_ocrInProgress(false)
    , m_qrCodeManager(nullptr)
    , m_qrCodeInProgress(false)
    , m_opacity(PinWindowSettingsManager::instance().loadDefaultOpacity())
    , m_currentZoomAction(nullptr)
    , m_smoothing(true)
    , m_clickThrough(false)
{
    // Frameless, always on top
    // Note: Removed Qt::Tool flag as it causes the window to hide when app loses focus on macOS
    setWindowFlags(Qt::FramelessWindowHint |
        Qt::WindowStaysOnTopHint);

    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_TranslucentBackground);
#ifdef Q_OS_MACOS
    // Ensure the window stays visible even when app is not focused
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
#endif

    // Initial size matches screenshot (use logical size for HiDPI)
    m_displayPixmap = m_originalPixmap;
    QSize logicalSize = logicalSizeFromPixmap(m_displayPixmap);
    setFixedSize(logicalSize);

    // Cache the base corner radius so pin window chrome matches rounded captures.
    qreal baseDpr = m_originalPixmap.devicePixelRatio();
    if (baseDpr <= 0.0) {
        baseDpr = 1.0;
    }
    QSize baseLogicalSize = logicalSizeFromPixmap(m_originalPixmap);
    int maxRadius = qMin(baseLogicalSize.width(), baseLogicalSize.height()) / 2;
    m_baseCornerRadius = qMin(AnnotationSettingsManager::instance().loadCornerRadius(), maxRadius);
    if (m_baseCornerRadius > 0 && !hasTransparentCornerPixels(m_originalPixmap)) {
        m_baseCornerRadius = 0;
    }

    // Load watermark settings (used by context menu when created lazily)
    m_watermarkSettings = WatermarkSettingsManager::instance().load();

    // Context menu is now created lazily in contextMenuEvent() to improve initial performance

    // Auto-save to cache folder asynchronously (can be deferred by caller).
    if (autoSaveToCache) {
        saveToCacheAsync();
    }

    // OCR manager is now created lazily in performOCR() to improve initial performance

    // Enable mouse tracking for resize cursor
    setMouseTracking(true);

    // Register immediately so drag/resize cursor states work before toolbar init.
    CursorManager::instance().registerWidget(this);

    // Initialize components
    m_resizeHandler = new ResizeHandler(0, kMinPinSize, this);
    m_uiIndicators = new UIIndicators(this, this);
    connect(m_uiIndicators, &UIIndicators::exitClickThroughRequested,
        this, [this]() { setClickThrough(false); });

    // Must show() first, then move() to get correct positioning on macOS
    // Moving before show() can result in incorrect window placement
    show();

    // Enable visibility on all virtual desktops/workspaces
    setWindowVisibleOnAllWorkspaces(this, true);

    move(position);

    // Apply default opacity (90%)
    setWindowOpacity(m_opacity);

    // Initialize loading spinner for OCR
    m_loadingSpinner = new LoadingSpinnerRenderer(this);
    connect(m_loadingSpinner, &LoadingSpinnerRenderer::needsRepaint,
        this, QOverload<>::of(&QWidget::update));

    // Initialize resize finish timer for high-quality update
    m_resizeFinishTimer = new QTimer(this);
    m_resizeFinishTimer->setSingleShot(true);
    connect(m_resizeFinishTimer, &QTimer::timeout, this, &PinWindow::onResizeFinished);

    // Track cursor near edges for click-through resize
    m_clickThroughHoverTimer = new QTimer(this);
    m_clickThroughHoverTimer->setInterval(50);  // Click-through hover check
    connect(m_clickThroughHoverTimer, &QTimer::timeout, this, &PinWindow::updateClickThroughForCursor);
}

PinWindow::~PinWindow()
{
    m_isDestructing = true;

    if (m_autoBlurWatcher) {
        m_autoBlurWatcher->waitForFinished();
    }

    // Clean up cursor state before destruction (ensures Drag context is cleared)
    CursorManager::instance().clearAllForWidget(this);

    // Stop live capture if active
    if (m_isLiveMode) {
        stopLiveCapture();
    }

    // Clean up floating windows (not parented to this window)
    if (m_toolbar) {
        m_toolbar->close();
        m_toolbar.reset();
    }
    if (m_subToolbar) {
        m_subToolbar->close();
        m_subToolbar.reset();
    }
    if (m_colorPickerDialog) {
        m_colorPickerDialog->close();
        m_colorPickerDialog.reset();
    }
    if (m_beautifyPanel) {
        m_beautifyPanel->hide();
        m_beautifyPanel.reset();
    }
    // InlineTextEditor, TextAnnotationEditor are QObjects parented to this
}

void PinWindow::setPinWindowManager(PinWindowManager* manager)
{
    m_pinWindowManager = manager;
}

void PinWindow::setZoomLevel(qreal zoom)
{
    m_zoomLevel = qBound(0.1, zoom, 5.0);
    if (m_currentZoomAction) {
        m_currentZoomAction->setText(QString("%1%").arg(qRound(m_zoomLevel * 100)));
    }
    updateSize();
    syncCropHandlerImageSize();
}

void PinWindow::setOpacity(qreal opacity)
{
    m_opacity = qBound(0.1, opacity, 1.0);
    setWindowOpacity(m_opacity);
}

void PinWindow::adjustOpacityByStep(int direction)
{
    const qreal opacityStep = PinWindowSettingsManager::instance().loadOpacityStep();
    qreal oldOpacity = m_opacity;
    qreal newOpacity = m_opacity + (direction >= 0 ? opacityStep : -opacityStep);
    newOpacity = qBound(0.1, newOpacity, 1.0);

    if (qFuzzyCompare(oldOpacity, newOpacity)) {
        return;
    }

    setOpacity(newOpacity);
    m_uiIndicators->showOpacityIndicator(m_opacity);
}

void PinWindow::rotateRight()
{
    m_rotationAngle = (m_rotationAngle + 90) % 360;
    clearCropUndoHistory();
    updateSize();
    syncCropHandlerImageSize();
}

void PinWindow::rotateLeft()
{
    m_rotationAngle = (m_rotationAngle + 270) % 360;  // +270 is same as -90
    clearCropUndoHistory();
    updateSize();
    syncCropHandlerImageSize();
}

void PinWindow::flipHorizontal()
{
    m_flipHorizontal = !m_flipHorizontal;
    clearCropUndoHistory();
    updateSize();
    syncCropHandlerImageSize();
}

void PinWindow::flipVertical()
{
    m_flipVertical = !m_flipVertical;
    clearCropUndoHistory();
    updateSize();
    syncCropHandlerImageSize();
}

QPoint PinWindow::mapToOriginalCoords(const QPoint& displayPos) const
{
    return mapToOriginalCoords(QPointF(displayPos)).toPoint();
}

QPoint PinWindow::mapFromOriginalCoords(const QPoint& originalPos) const
{
    return mapFromOriginalCoords(QPointF(originalPos)).toPoint();
}

QPointF PinWindow::mapToOriginalCoords(const QPointF& displayPos) const
{
    if (m_rotationAngle == 0 && !m_flipHorizontal && !m_flipVertical) {
        return displayPos;
    }

    QTransform inverse = buildPinWindowTransform(rect(), m_rotationAngle, m_flipHorizontal, m_flipVertical).inverted();
    return inverse.map(displayPos);
}

QPointF PinWindow::mapFromOriginalCoords(const QPointF& originalPos) const
{
    if (m_rotationAngle == 0 && !m_flipHorizontal && !m_flipVertical) {
        return originalPos;
    }

    QTransform transform = buildPinWindowTransform(rect(), m_rotationAngle, m_flipHorizontal, m_flipVertical);
    return transform.map(originalPos);
}

void PinWindow::applyTextOrientationCompensation(TextBoxAnnotation* textItem,
                                                 const QPointF& baselineOriginal) const
{
    if (!textItem) {
        return;
    }

    const TextCompensation compensation = computeTextCompensation(
        m_rotationAngle, m_flipHorizontal, m_flipVertical);
    textItem->setRotation(compensation.rotation);
    textItem->setMirror(compensation.mirrorX, compensation.mirrorY);

    QFontMetrics fm(textItem->font());
    QPointF localBaseline(TextBoxAnnotation::kPadding, fm.ascent() + TextBoxAnnotation::kPadding);
    textItem->setPosition(textItem->topLeftFromTransformedLocalPoint(baselineOriginal, localBaseline));
}

void PinWindow::applyEmojiOrientationCompensation(EmojiStickerAnnotation* emojiItem) const
{
    if (!emojiItem) {
        return;
    }

    const TextCompensation compensation = computeTextCompensation(
        m_rotationAngle, m_flipHorizontal, m_flipVertical);
    emojiItem->setRotation(compensation.rotation);
    emojiItem->setMirror(compensation.mirrorX, compensation.mirrorY);
}

void PinWindow::compensateNewestEmojiIfNeeded(const AnnotationItem* previousLastItem) const
{
    if (!m_annotationLayer) {
        return;
    }

    const size_t currentCount = m_annotationLayer->itemCount();
    if (currentCount == 0) {
        return;
    }

    auto* newestItem = m_annotationLayer->itemAt(static_cast<int>(currentCount - 1));
    // AnnotationLayer may trim history at capacity, so count can stay unchanged
    // even when a new item is added. Compare last-item pointer instead.
    if (!newestItem || newestItem == previousLastItem) {
        return;
    }

    auto* newestEmoji = dynamic_cast<EmojiStickerAnnotation*>(newestItem);
    applyEmojiOrientationCompensation(newestEmoji);
}

void PinWindow::applyStepBadgeOrientationCompensation(StepBadgeAnnotation* badgeItem) const
{
    if (!badgeItem) {
        return;
    }

    const TextCompensation compensation = computeTextCompensation(
        m_rotationAngle, m_flipHorizontal, m_flipVertical);
    badgeItem->setRotation(compensation.rotation);
    badgeItem->setMirror(compensation.mirrorX, compensation.mirrorY);
}

void PinWindow::compensateNewestStepBadgeIfNeeded(const AnnotationItem* previousLastItem) const
{
    if (!m_annotationLayer) {
        return;
    }

    const size_t currentCount = m_annotationLayer->itemCount();
    if (currentCount == 0) {
        return;
    }

    auto* newestItem = m_annotationLayer->itemAt(static_cast<int>(currentCount - 1));
    // AnnotationLayer may trim history at capacity, so count can stay unchanged
    // even when a new item is added. Compare last-item pointer instead.
    if (!newestItem || newestItem == previousLastItem) {
        return;
    }

    auto* newestBadge = dynamic_cast<StepBadgeAnnotation*>(newestItem);
    applyStepBadgeOrientationCompensation(newestBadge);
}

void PinWindow::setWatermarkSettings(const WatermarkRenderer::Settings& settings)
{
    m_watermarkSettings = settings;
    update();
}

void PinWindow::ensureTransformCacheValid() const
{
    // Check if cache needs to be rebuilt
    if (m_cachedRotation == m_rotationAngle &&
        m_cachedFlipH == m_flipHorizontal &&
        m_cachedFlipV == m_flipVertical &&
        !m_transformedCache.isNull()) {
        return;  // Cache is valid
    }

    // Rebuild cache
    if (m_rotationAngle != 0 || m_flipHorizontal || m_flipVertical) {
        QTransform transform;

        if (m_rotationAngle != 0) {
            transform.rotate(m_rotationAngle);
        }

        if (m_flipHorizontal || m_flipVertical) {
            qreal scaleX = m_flipHorizontal ? -1.0 : 1.0;
            qreal scaleY = m_flipVertical ? -1.0 : 1.0;
            transform.scale(scaleX, scaleY);
        }

        m_transformedCache = m_originalPixmap.transformed(transform, Qt::SmoothTransformation);
        m_transformedCache.setDevicePixelRatio(m_originalPixmap.devicePixelRatio());
    }
    else {
        m_transformedCache = m_originalPixmap;
    }

    m_cachedRotation = m_rotationAngle;
    m_cachedFlipH = m_flipHorizontal;
    m_cachedFlipV = m_flipVertical;
}

void PinWindow::onResizeFinished()
{
    if (m_pendingHighQualityUpdate && !m_isResizing) {
        // Perform high-quality scaling after resize is complete
        ensureTransformCacheValid();

        qreal dpr = m_transformedCache.devicePixelRatio();
        if (dpr <= 0.0) {
            dpr = 1.0;
        }
        QSize newDeviceSize = CoordinateHelper::toPhysical(size(), dpr);

        m_displayPixmap = m_transformedCache.scaled(newDeviceSize,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation);
        m_displayPixmap.setDevicePixelRatio(dpr);

        m_pendingHighQualityUpdate = false;
        update();
    }
}

int PinWindow::effectiveCornerRadius(const QSize& contentSize) const
{
    if (m_baseCornerRadius <= 0 || contentSize.isEmpty()) {
        return 0;
    }

    QSize baseLogicalSize;
    if (!m_transformedCache.isNull()) {
        baseLogicalSize = logicalSizeFromPixmap(m_transformedCache);
    }
    else {
        baseLogicalSize = logicalSizeFromPixmap(m_originalPixmap);
    }

    if (baseLogicalSize.isEmpty()) {
        return 0;
    }

    qreal scaleX = static_cast<qreal>(contentSize.width()) / baseLogicalSize.width();
    qreal scaleY = static_cast<qreal>(contentSize.height()) / baseLogicalSize.height();
    qreal scale = qMin(scaleX, scaleY);

    int radius = qRound(m_baseCornerRadius * scale);
    int maxRadius = qMin(contentSize.width(), contentSize.height()) / 2;
    return qMin(radius, maxRadius);
}

void PinWindow::updateSize()
{
    // Use cached transform (only rebuild if rotation/flip changed)
    ensureTransformCacheValid();

    // For 90/270 degree rotations, swap width and height
    const qreal dpr = m_transformedCache.devicePixelRatio() > 0.0
        ? m_transformedCache.devicePixelRatio()
        : 1.0;
    QSize transformedLogicalSize = logicalSizeFromPixmap(m_transformedCache);
    QSize newLogicalSize = transformedLogicalSize * m_zoomLevel;

    // Scale to device pixels for the actual pixmap
    QSize newDeviceSize = CoordinateHelper::toPhysical(newLogicalSize, dpr);
    Qt::TransformationMode mode = m_smoothing ? Qt::SmoothTransformation : Qt::FastTransformation;
    m_displayPixmap = m_transformedCache.scaled(newDeviceSize,
        Qt::KeepAspectRatio,
        mode);
    m_displayPixmap.setDevicePixelRatio(dpr);

    setFixedSize(newLogicalSize);
    update();
}

QPixmap PinWindow::getTransformedPixmap() const
{
    ensureTransformCacheValid();

    if (m_transformedCache.isNull()) {
        return m_originalPixmap;
    }
    return m_transformedCache;
}

QPixmap PinWindow::getExportPixmapCore(bool includeDisplayEffects) const
{
    // 1. Get current display size (includes zoom)
    QSize exportSize = m_displayPixmap.size();

    // 2. Get transformed pixmap (rotation/flip) and scale to current size
    QPixmap basePixmap = getTransformedPixmap();

    QPixmap scaledPixmap = basePixmap.scaled(
        exportSize,
        Qt::IgnoreAspectRatio,
        Qt::SmoothTransformation
    );
    scaledPixmap.setDevicePixelRatio(basePixmap.devicePixelRatio());

    if (includeDisplayEffects) {
        // 3. If opacity is adjusted, paint with opacity
        if (m_opacity < 1.0) {
            QPixmap resultPixmap(scaledPixmap.size());
            resultPixmap.setDevicePixelRatio(scaledPixmap.devicePixelRatio());
            resultPixmap.fill(Qt::transparent);

            QPainter painter(&resultPixmap);
            painter.setOpacity(m_opacity);
            painter.drawPixmap(0, 0, scaledPixmap);
            painter.end();

            scaledPixmap = resultPixmap;
        }

        // 4. Apply watermark
        return WatermarkRenderer::applyToPixmap(scaledPixmap, m_watermarkSettings);
    }

    return scaledPixmap;
}

QPixmap PinWindow::getExportPixmap() const
{
    return getExportPixmapCore(true);
}

void PinWindow::drawAnnotationsForExport(QPainter& painter, const QSize& logicalSize) const
{
    if (!m_annotationLayer || m_annotationLayer->isEmpty() || logicalSize.isEmpty()) {
        return;
    }

    painter.save();
    if (m_rotationAngle != 0 || m_flipHorizontal || m_flipVertical) {
        const QRectF pixmapRect(QPointF(0, 0), QSizeF(logicalSize));
        QTransform transform = buildPinWindowTransform(
            pixmapRect, m_rotationAngle, m_flipHorizontal, m_flipVertical);
        painter.setTransform(transform, true);
    }
    m_annotationLayer->draw(painter);
    painter.restore();
}

void PinWindow::createContextMenu()
{
    m_contextMenu = new QMenu(this);

    // Show Toolbar action
    m_showToolbarAction = m_contextMenu->addAction(tr("Show Toolbar"));
    m_showToolbarAction->setShortcut(QKeySequence(Qt::Key_Space));
    m_showToolbarAction->setCheckable(true);
    connect(m_showToolbarAction, &QAction::triggered, this, &PinWindow::toggleToolbar);

    // Show border option (placed right after Show Toolbar)
    m_showBorderAction = m_contextMenu->addAction(tr("Show Border"));
    m_showBorderAction->setCheckable(true);
    m_showBorderAction->setChecked(m_showBorder);
    connect(m_showBorderAction, &QAction::toggled, this, &PinWindow::setShowBorder);

    m_adjustRegionLayoutAction = m_contextMenu->addAction(tr("Adjust Region Layout"));
    connect(m_adjustRegionLayoutAction, &QAction::triggered, this, &PinWindow::enterRegionLayoutMode);

    m_contextMenu->addSeparator();

    QAction* copyAction = m_contextMenu->addAction(tr("Copy to Clipboard"));
    copyAction->setShortcut(QKeySequence::Copy);
    connect(copyAction, &QAction::triggered, this, &PinWindow::copyToClipboard);

    QAction* saveAction = m_contextMenu->addAction(tr("Save to file"));
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &PinWindow::saveToFile);

    QAction* openCacheFolderAction = m_contextMenu->addAction(tr("Open Cache Folder"));
    connect(openCacheFolderAction, &QAction::triggered, this, &PinWindow::openCacheFolder);

    QAction* beautifyAction = m_contextMenu->addAction(tr("Beautify"));
    connect(beautifyAction, &QAction::triggered, this, &PinWindow::showBeautifyPanel);

    if (PlatformFeatures::instance().isOCRAvailable()) {
        QAction* ocrAction = m_contextMenu->addAction(tr("Recognize Text"));
        connect(ocrAction, &QAction::triggered, this, &PinWindow::performOCR);
    }

    // Click-through option
    m_clickThroughAction = m_contextMenu->addAction(tr("Click-through"));
    m_clickThroughAction->setCheckable(true);
    m_clickThroughAction->setChecked(m_clickThrough);
    connect(m_clickThroughAction, &QAction::toggled, this, &PinWindow::setClickThrough);

    m_mergePinsAction = m_contextMenu->addAction(tr("Merge Pins"));
    connect(m_mergePinsAction, &QAction::triggered, this, &PinWindow::mergePinsFromContextMenu);

    m_contextMenu->addSeparator();

    // Watermark submenu
    QMenu* watermarkMenu = m_contextMenu->addMenu(tr("Watermark"));

    // Enable watermark action
    QAction* enableWatermarkAction = watermarkMenu->addAction(tr("Enable"));
    enableWatermarkAction->setCheckable(true);
    enableWatermarkAction->setChecked(m_watermarkSettings.enabled);
    connect(enableWatermarkAction, &QAction::toggled, this, [this](bool checked) {
        m_watermarkSettings.enabled = checked;
        update();
        });

    watermarkMenu->addSeparator();

    // Position submenu
    QMenu* positionMenu = watermarkMenu->addMenu(tr("Position"));
    QActionGroup* positionGroup = new QActionGroup(this);

    QAction* topLeftAction = positionMenu->addAction(tr("Top-Left"));
    topLeftAction->setCheckable(true);
    topLeftAction->setData(static_cast<int>(WatermarkRenderer::TopLeft));
    positionGroup->addAction(topLeftAction);

    QAction* topRightAction = positionMenu->addAction(tr("Top-Right"));
    topRightAction->setCheckable(true);
    topRightAction->setData(static_cast<int>(WatermarkRenderer::TopRight));
    positionGroup->addAction(topRightAction);

    QAction* bottomLeftAction = positionMenu->addAction(tr("Bottom-Left"));
    bottomLeftAction->setCheckable(true);
    bottomLeftAction->setData(static_cast<int>(WatermarkRenderer::BottomLeft));
    positionGroup->addAction(bottomLeftAction);

    QAction* bottomRightAction = positionMenu->addAction(tr("Bottom-Right"));
    bottomRightAction->setCheckable(true);
    bottomRightAction->setData(static_cast<int>(WatermarkRenderer::BottomRight));
    positionGroup->addAction(bottomRightAction);

    // Set current position
    for (QAction* action : positionGroup->actions()) {
        if (action->data().toInt() == static_cast<int>(m_watermarkSettings.position)) {
            action->setChecked(true);
            break;
        }
    }

    connect(positionGroup, &QActionGroup::triggered, this, [this](QAction* action) {
        m_watermarkSettings.position = static_cast<WatermarkRenderer::Position>(action->data().toInt());
        update();
        });

    // Zoom submenu
    QMenu* zoomMenu = m_contextMenu->addMenu(tr("Zoom"));

    // Preset zoom levels
    QAction* zoom33Action = zoomMenu->addAction(tr("33.3%"));
    connect(zoom33Action, &QAction::triggered, this, [this]() { setZoomLevel(1.0 / 3.0); });

    QAction* zoom50Action = zoomMenu->addAction(tr("50%"));
    connect(zoom50Action, &QAction::triggered, this, [this]() { setZoomLevel(0.5); });

    QAction* zoom67Action = zoomMenu->addAction(tr("66.7%"));
    connect(zoom67Action, &QAction::triggered, this, [this]() { setZoomLevel(2.0 / 3.0); });

    QAction* zoom100Action = zoomMenu->addAction(tr("100%"));
    connect(zoom100Action, &QAction::triggered, this, [this]() { setZoomLevel(1.0); });

    QAction* zoom200Action = zoomMenu->addAction(tr("200%"));
    connect(zoom200Action, &QAction::triggered, this, [this]() { setZoomLevel(2.0); });

    zoomMenu->addSeparator();

    // Current zoom level display (disabled, for display only)
    m_currentZoomAction = zoomMenu->addAction(QString("%1%").arg(qRound(m_zoomLevel * 100)));
    m_currentZoomAction->setEnabled(false);

    // Smoothing option
    QAction* smoothingAction = zoomMenu->addAction(tr("Smoothing"));
    smoothingAction->setCheckable(true);
    smoothingAction->setChecked(m_smoothing);
    connect(smoothingAction, &QAction::toggled, this, [this](bool checked) {
        m_smoothing = checked;
        updateSize();
        });

    // Image processing submenu
    QMenu* imageProcessingMenu = m_contextMenu->addMenu(tr("Image processing"));

    QAction* rotateLeftAction = imageProcessingMenu->addAction(tr("Rotate left"));
    connect(rotateLeftAction, &QAction::triggered, this, &PinWindow::rotateLeft);

    QAction* rotateRightAction = imageProcessingMenu->addAction(tr("Rotate right"));
    connect(rotateRightAction, &QAction::triggered, this, &PinWindow::rotateRight);

    QAction* horizontalFlipAction = imageProcessingMenu->addAction(tr("Horizontal flip"));
    connect(horizontalFlipAction, &QAction::triggered, this, &PinWindow::flipHorizontal);

    QAction* verticalFlipAction = imageProcessingMenu->addAction(tr("Vertical flip"));
    connect(verticalFlipAction, &QAction::triggered, this, &PinWindow::flipVertical);

    imageProcessingMenu->addSeparator();
    QAction* cropAction = imageProcessingMenu->addAction(tr("Crop"));
    connect(cropAction, &QAction::triggered, this, [this]() {
        if (!m_toolbar) {
            initializeAnnotationComponents();
        }
        showToolbar();
        handleToolbarToolSelected(static_cast<int>(ToolId::Crop));
    });

    QMenu* opacityMenu = imageProcessingMenu->addMenu(tr("Opacity"));
    const int opacityStepPercent = qRound(PinWindowSettingsManager::instance().loadOpacityStep() * 100);
    QAction* increaseOpacityAction = opacityMenu->addAction(
        tr("Increase by %1%").arg(opacityStepPercent));
    increaseOpacityAction->setShortcut(QKeySequence(Qt::Key_BracketRight));
    connect(increaseOpacityAction, &QAction::triggered, this, [this]() {
        adjustOpacityByStep(1);
        });

    QAction* decreaseOpacityAction = opacityMenu->addAction(
        tr("Decrease by %1%").arg(opacityStepPercent));
    decreaseOpacityAction->setShortcut(QKeySequence(Qt::Key_BracketLeft));
    connect(decreaseOpacityAction, &QAction::triggered, this, [this]() {
        adjustOpacityByStep(-1);
        });

    // Info submenu - displays image properties
    QSize logicalSize = logicalSizeFromPixmap(m_originalPixmap);
    QMenu* infoMenu = m_contextMenu->addMenu(
        QString("%1 × %2").arg(logicalSize.width()).arg(logicalSize.height()));

    // Copy all info action
    QAction* copyAllInfoAction = infoMenu->addAction(tr("Copy All"));
    connect(copyAllInfoAction, &QAction::triggered, this, &PinWindow::copyAllInfo);

    infoMenu->addSeparator();

    // Info items - clicking copies that value
    auto addInfoItem = [infoMenu](const QString& label, const QString& value) {
        QAction* action = infoMenu->addAction(QString("%1: %2").arg(label, value));
        QObject::connect(action, &QAction::triggered, [value]() {
            QGuiApplication::clipboard()->setText(value);
            });
        };

    addInfoItem(tr("Size"), QString("%1 × %2").arg(logicalSize.width()).arg(logicalSize.height()));
    addInfoItem(tr("Zoom"), QString("%1%").arg(qRound(m_zoomLevel * 100)));
    addInfoItem(tr("Rotation"), QString::fromUtf8("%1°").arg(m_rotationAngle));
    addInfoItem(tr("Opacity"), QString("%1%").arg(qRound(m_opacity * 100)));
    addInfoItem(tr("X-mirror"), m_flipHorizontal ? tr("Yes") : tr("No"));
    addInfoItem(tr("Y-mirror"), m_flipVertical ? tr("Yes") : tr("No"));

    // Live capture section - actions are updated dynamically in contextMenuEvent
    m_contextMenu->addSeparator();

    // Start/Stop Live action
    m_startLiveAction = m_contextMenu->addAction(tr("Start Live Update"));
    connect(m_startLiveAction, &QAction::triggered, this, [this]() {
        if (m_isLiveMode) {
            stopLiveCapture();
        }
        else {
            startLiveCapture();
        }
        });

    // Pause/Resume Live action
    m_pauseLiveAction = m_contextMenu->addAction(tr("Pause Live Update"));
    connect(m_pauseLiveAction, &QAction::triggered, this, [this]() {
        if (m_livePaused) {
            resumeLiveCapture();
        }
        else {
            pauseLiveCapture();
        }
        });

    // Frame rate submenu
    m_fpsMenu = m_contextMenu->addMenu(tr("Frame Rate"));
    QActionGroup* fpsGroup = new QActionGroup(this);
    for (int fps : {5, 10, 15, 30}) {
        QAction* fpsAction = m_fpsMenu->addAction(tr("%1 FPS").arg(fps));
        fpsAction->setCheckable(true);
        fpsAction->setData(fps);
        fpsGroup->addAction(fpsAction);
        if (fps == m_captureFrameRate) {
            fpsAction->setChecked(true);
        }
    }
    connect(fpsGroup, &QActionGroup::triggered, this, [this](QAction* action) {
        setLiveFrameRate(action->data().toInt());
        });

    m_contextMenu->addSeparator();

    QAction* closeAction = m_contextMenu->addAction(tr("Close"));
    closeAction->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(closeAction, &QAction::triggered, this, &PinWindow::close);

    QAction* closeAllPinsAction = m_contextMenu->addAction(tr("Close All Pins"));
    connect(closeAllPinsAction, &QAction::triggered, this, [this]() {
        if (m_pinWindowManager) {
            m_pinWindowManager->closeAllWindows();
        }
        });
}

int PinWindow::eligibleMergePinCount() const
{
    if (!m_pinWindowManager) {
        return 0;
    }

    int count = 0;
    for (PinWindow* window : m_pinWindowManager->windows()) {
        if (window && window->isVisible() && !window->isLiveMode()) {
            ++count;
        }
    }
    return count;
}

void PinWindow::mergePinsFromContextMenu()
{
    if (!m_pinWindowManager) {
        return;
    }

    QList<PinWindow*> mergeCandidates;
    mergeCandidates.reserve(m_pinWindowManager->windows().size());
    for (PinWindow* window : m_pinWindowManager->windows()) {
        if (!window || !window->isVisible() || window->isLiveMode()) {
            continue;
        }
        mergeCandidates.append(window);
    }

    if (mergeCandidates.size() > LayoutModeConstants::kMaxRegionCount) {
        GlobalToast::instance().showToast(GlobalToast::Info,
                                          tr("Merge Pins"),
                                          tr("Cannot merge more than %1 pins at once")
                                              .arg(LayoutModeConstants::kMaxRegionCount));
        return;
    }

    if (mergeCandidates.size() < 2) {
        GlobalToast::instance().showToast(GlobalToast::Info,
                                          tr("Merge Pins"),
                                          tr("Need at least 2 pins to merge"));
        return;
    }

    const PinMergeResult result = PinMergeHelper::merge(mergeCandidates);
    if (!result.success) {
        GlobalToast::instance().showToast(GlobalToast::Info,
                                          tr("Merge Pins"),
                                          result.errorMessage);
        return;
    }

    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        GlobalToast::instance().showToast(GlobalToast::Error,
                                          tr("Merge Pins"),
                                          tr("No primary screen available"));
        return;
    }

    const QRect screenGeometry = screen->availableGeometry();
    const QSize logicalSize = logicalSizeFromPixmap(result.composedPixmap);
    const QPoint position = screenGeometry.center() - QPoint(logicalSize.width() / 2, logicalSize.height() / 2);

    // Defer cache save until multi-region metadata is attached, otherwise the
    // initial cache snapshot misses the .snpr sidecar.
    PinWindow* mergedWindow = m_pinWindowManager->createPinWindow(result.composedPixmap, position, false);
    if (!mergedWindow) {
        GlobalToast::instance().showToast(GlobalToast::Error,
                                          tr("Merge Pins"),
                                          tr("Failed to create merged pin"));
        return;
    }

    mergedWindow->setMultiRegionData(result.regions);
    mergedWindow->saveToCacheAsync();
    m_pinWindowManager->closeWindows(result.mergedWindows);
}

void PinWindow::saveToFile()
{
    // Use FileSettingsManager for consistent path and filename format
    auto& fileSettings = FileSettingsManager::instance();
    QString savePath = fileSettings.loadScreenshotPath();
    QPixmap pixmapToSave = getExportPixmapWithAnnotations();
    const QSize logicalSize = logicalSizeFromPixmap(pixmapToSave);

    QScreen* exportScreen = m_sourceScreen.data();
    if (!exportScreen) {
        exportScreen = screen();
    }

    int monitorIndex = -1;
    if (exportScreen) {
        monitorIndex = QGuiApplication::screens().indexOf(exportScreen);
    }

    int regionIndex = -1;
    if (!m_storedRegions.isEmpty()) {
        regionIndex = m_storedRegions.first().index;
    }

    FilenameTemplateEngine::Context context;
    context.type = QStringLiteral("Screenshot");
    context.prefix = fileSettings.loadFilenamePrefix();
    context.width = logicalSize.width();
    context.height = logicalSize.height();
    context.monitor = monitorIndex >= 0 ? QString::number(monitorIndex) : QStringLiteral("unknown");
    context.windowTitle = QString();
    context.appName = QString();
    context.regionIndex = regionIndex;
    context.ext = QStringLiteral("png");
    context.dateFormat = fileSettings.loadDateFormat();
    context.outputDir = savePath;
    const QString templateValue = fileSettings.loadFilenameTemplate();

    // Check auto-save setting
    if (fileSettings.loadAutoSaveScreenshots()) {
        QString renderError;
        QString filePath = FilenameTemplateEngine::buildUniqueFilePath(
            savePath, templateValue, context, kMaxFileCollisionRetries, &renderError);

        ImageSaveUtils::Error saveError;
        if (ImageSaveUtils::savePixmapAtomically(pixmapToSave, filePath, QByteArray(), &saveError)) {
            emit saveCompleted(pixmapToSave, filePath);
        }
        else {
            if (!renderError.isEmpty()) {
                qWarning() << "PinWindow: template warning:" << renderError;
            }
            emit saveFailed(filePath, tr("Failed to save screenshot: %1").arg(saveErrorDetail(saveError)));
        }
        return;
    }

    // Show save dialog
    QString defaultName = FilenameTemplateEngine::buildUniqueFilePath(
        savePath, templateValue, context, 1);
    QString filePath = QFileDialog::getSaveFileName(
        this,
        tr("Save Screenshot"),
        defaultName,
        tr("PNG Image (*.png);;JPEG Image (*.jpg *.jpeg);;All Files (*)")
    );

    if (!filePath.isEmpty()) {
        ImageSaveUtils::Error saveError;
        if (ImageSaveUtils::savePixmapAtomically(pixmapToSave, filePath, QByteArray(), &saveError)) {
            emit saveRequested(pixmapToSave);
        } else {
            emit saveFailed(filePath, tr("Failed to save screenshot: %1").arg(saveErrorDetail(saveError)));
        }
    }
}

void PinWindow::copyToClipboard()
{
    QPixmap pixmapToCopy = getExportPixmapWithAnnotations();
    if (pixmapToCopy.isNull()) {
        m_uiIndicators->showToast(false, tr("Copy failed"));
        return;
    }
    QGuiApplication::clipboard()->setPixmap(pixmapToCopy);
    m_uiIndicators->showToast(true, tr("Copied to clipboard"));
}

void PinWindow::performOCR()
{
    if (m_ocrInProgress) {
        return;
    }

    // Lazy-create OCR manager on first use to improve initial window creation performance
    if (!m_ocrManager) {
        m_ocrManager = PlatformFeatures::instance().createOCRManager(this);
        if (m_ocrManager) {
            m_ocrManager->setRecognitionLanguages(OCRSettingsManager::instance().languages());
        }
    }

    if (!m_ocrManager) {
        return;
    }

    m_ocrInProgress = true;
    m_loadingSpinner->start();
    update();

    QPointer<PinWindow> safeThis = this;
    m_ocrManager->recognizeText(m_originalPixmap,
        [safeThis](const OCRResult& result) {
            if (safeThis) {
                safeThis->onOCRComplete(result);
            }
        });
}

void PinWindow::onOCRComplete(const OCRResult& result)
{
    m_ocrInProgress = false;
    m_loadingSpinner->stop();

    if (result.success && !result.text.isEmpty()) {
        // Check settings to determine behavior
        if (OCRSettingsManager::instance().behavior() == OCRBehavior::ShowEditor) {
            showOCRResultDialog(result);
            return;
        }

        // Default behavior: direct copy
        QGuiApplication::clipboard()->setText(result.text);
        QString msg = tr("Copied %1 characters").arg(result.text.length());
        m_uiIndicators->showToast(true, msg);
    }
    else {
        QString msg = result.error.isEmpty() ? tr("No text found") : result.error;
        m_uiIndicators->showToast(false, msg);
    }

    emit ocrCompleted(result.success && !result.text.isEmpty(),
                      result.success ? tr("Text copied") : result.error);
}

void PinWindow::showOCRResultDialog(const OCRResult& result)
{
    // Create non-modal dialog
    auto *dialog = new OCRResultDialog(this);
    dialog->setOCRResult(result);

    // Connect signals
    connect(dialog, &OCRResultDialog::textCopied, this, [this](const QString &copiedText) {
        qDebug() << "OCR text copied:" << copiedText.length() << "characters";

        // Show success toast
        m_uiIndicators->showToast(true, tr("Copied %1 characters").arg(copiedText.length()));
        emit ocrCompleted(true, tr("Text copied"));
    });

    // Note: PinWindow stays open after dialog closes (no close() call)

    // Show dialog centered on screen
    dialog->showAt();
}

void PinWindow::performQRCodeScan()
{
    if (m_qrCodeInProgress) {
        return;
    }

    // Lazy-create QR Code manager on first use
    if (!m_qrCodeManager) {
        m_qrCodeManager = new QRCodeManager(this);
    }

    if (!m_qrCodeManager) {
        return;
    }

    m_qrCodeInProgress = true;
    m_loadingSpinner->start();
    update();

    QPointer<PinWindow> safeThis = this;
    m_qrCodeManager->decode(m_originalPixmap,
        [safeThis](const QRDecodeResult& result) {
            if (safeThis) {
                safeThis->onQRCodeComplete(result.success, result.text, result.format, result.error);
            }
        });
}

void PinWindow::onQRCodeComplete(bool success, const QString& text, const QString& format, const QString& error)
{
    m_qrCodeInProgress = false;
    m_loadingSpinner->stop();

    if (success && !text.isEmpty()) {
        // Show result dialog
        auto *dialog = new QRCodeResultDialog(this);
        dialog->setResult(text, format, m_originalPixmap);

        // Connect signals
        connect(dialog, &QRCodeResultDialog::textCopied, this, [this](const QString &copiedText) {
            qDebug() << "QR Code text copied:" << copiedText.length() << "characters";

            // Show success toast
            QString msg = tr("Copied %1 characters").arg(copiedText.length());
            m_uiIndicators->showToast(true, msg);
        });

        // Show dialog centered on screen
        dialog->showAt();
    }
    else {
        // Show error toast
        QString msg = error.isEmpty() ? tr("No QR code found") : error;
        m_uiIndicators->showToast(false, msg);
    }
}

void PinWindow::updateOcrLanguages(const QStringList& languages)
{
    if (m_ocrManager) {
        m_ocrManager->setRecognitionLanguages(languages);
    }
}

void PinWindow::copyAllInfo()
{
    QSize logicalSize = logicalSizeFromPixmap(m_originalPixmap);
    QStringList info;
    info << tr("Size: %1 × %2").arg(logicalSize.width()).arg(logicalSize.height());
    info << tr("Zoom: %1%").arg(qRound(m_zoomLevel * 100));
    info << tr("Rotation: %1°").arg(m_rotationAngle);
    info << tr("Opacity: %1%").arg(qRound(m_opacity * 100));
    info << tr("X-mirror: %1").arg(m_flipHorizontal ? tr("Yes") : tr("No"));
    info << tr("Y-mirror: %1").arg(m_flipVertical ? tr("Yes") : tr("No"));

    QGuiApplication::clipboard()->setText(info.join("\n"));
}

QString PinWindow::cacheFolderPath()
{
    return PinHistoryStore::historyFolderPath();
}

void PinWindow::openCacheFolder()
{
    QString path = cacheFolderPath();

    QDir dir(path);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            qWarning() << "PinWindow: Failed to create cache folder:" << path;
            return;
        }
    }

    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void PinWindow::saveToCacheAsync()
{
    QPixmap pixmapToSave = m_originalPixmap;
    // QPixmap is GUI-thread bound on many platforms; convert before dispatching worker thread work.
    const QImage imageToSave = pixmapToSave.toImage();
    int maxCacheFiles = PinWindowSettingsManager::instance().loadMaxCacheFiles();
    const qreal sourceDpr = (pixmapToSave.devicePixelRatio() > 0.0)
        ? pixmapToSave.devicePixelRatio()
        : 1.0;

    // Prepare region metadata if multi-region data exists
    QByteArray regionMetadata;
    if (m_hasMultiRegionData && !m_storedRegions.isEmpty()) {
        regionMetadata = RegionLayoutManager::serializeRegions(m_storedRegions);
    }

    QThreadPool::globalInstance()->start([imageToSave, maxCacheFiles, regionMetadata, sourceDpr]() {
        QString path = cacheFolderPath();
        QDir dir(path);
        if (!dir.exists()) {
            if (!dir.mkpath(".")) {
                qWarning() << "PinWindow: Failed to create cache folder:" << path;
                return;
            }
        }

        QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz");
        QString filename = QString("cache_%1.png").arg(timestamp);
        QString filePath = dir.filePath(filename);

        ImageSaveUtils::Error saveError;
        if (ImageSaveUtils::saveImageAtomically(
                imageToSave, filePath, QByteArrayLiteral("PNG"), &saveError)) {
            // Persist DPR sidecar so history re-pin restores logical sizing on HiDPI displays.
            const QString dprMetadataPath = PinHistoryStore::dprMetadataPathForImage(filePath);
            QFile dprFile(dprMetadataPath);
            if (dprFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                const QByteArray dprText = QByteArray::number(sourceDpr, 'f', 4);
                dprFile.write(dprText);
                dprFile.close();
            }

            // Save region metadata sidecar file if available
            if (!regionMetadata.isEmpty()) {
                const QString metadataPath = PinHistoryStore::regionMetadataPathForImage(filePath);
                QFile metaFile(metadataPath);
                if (metaFile.open(QIODevice::WriteOnly)) {
                    metaFile.write(regionMetadata);
                    metaFile.close();
                }
            }

            // Clean up old cache files exceeding the limit
            QStringList filters;
            filters << "cache_*.png";
            QFileInfoList files = dir.entryInfoList(filters, QDir::Files, QDir::Time);

            // QDir::Time sorts by modification time, newest first
            while (files.size() > maxCacheFiles) {
                const QFileInfo& oldest = files.takeLast();
                QFile::remove(oldest.filePath());
                // Also remove associated metadata file
                QFile::remove(PinHistoryStore::regionMetadataPathForImage(oldest.filePath()));
                QFile::remove(PinHistoryStore::dprMetadataPathForImage(oldest.filePath()));
            }
        }
        else {
            qWarning() << "PinWindow: Failed to save to cache:" << filePath << saveErrorDetail(saveError);
        }
    });
}

void PinWindow::applyClickThroughState(bool enabled)
{
    if (m_clickThroughApplied == enabled) {
        return;
    }

    setWindowClickThrough(this, enabled);
    m_clickThroughApplied = enabled;
}

void PinWindow::updateClickThroughForCursor()
{
    if (!m_clickThrough) {
        return;
    }

    bool needsInput = m_isResizing;
    if (!needsInput) {
        const QPoint globalPos = QCursor::pos();
        const QPoint localPos = mapFromGlobal(globalPos);
        if (rect().contains(localPos)) {
            ResizeHandler::Edge edge = m_resizeHandler->getEdgeAt(localPos, size());
            needsInput = edge != ResizeHandler::Edge::None;
        }
    }

    applyClickThroughState(!needsInput);
}

void PinWindow::setClickThrough(bool enabled)
{
    if (m_clickThrough == enabled)
        return;

    m_clickThrough = enabled;

    if (m_clickThrough) {
        if (m_clickThroughHoverTimer) {
            m_clickThroughHoverTimer->start();
        }
        updateClickThroughForCursor();
    }
    else {
        if (m_clickThroughHoverTimer) {
            m_clickThroughHoverTimer->stop();
        }
        applyClickThroughState(false);
    }

    m_uiIndicators->showClickThroughIndicator(enabled);
    update(); // Trigger repaint for border change

    // Sync context menu action state
    if (m_clickThroughAction && m_clickThroughAction->isChecked() != enabled) {
        m_clickThroughAction->setChecked(enabled);
    }
}

void PinWindow::setShowBorder(bool enabled)
{
    if (m_showBorder == enabled)
        return;

    m_showBorder = enabled;
    update();

    // Sync context menu action state
    if (m_showBorderAction && m_showBorderAction->isChecked() != enabled) {
        m_showBorderAction->setChecked(enabled);
    }
}

void PinWindow::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::Antialiasing);

    QRect pixmapRect = rect();

    // Draw the screenshot (skip in layout mode - regions are drawn separately)
    if (!isRegionLayoutMode()) {
        painter.drawPixmap(pixmapRect, m_displayPixmap);
    } else {
        // Draw solid background for layout mode
        painter.fillRect(pixmapRect, QColor(40, 40, 40));
    }

    // Draw annotations with the same rotation/flip transform as the pixmap
    if (m_annotationLayer && !m_annotationLayer->isEmpty()) {
        painter.save();

        // Apply the same rotation/flip transform used for the pixmap
        // QPixmap::transformed() with rotate() rotates around origin (0,0),
        // so we need to match that transformation exactly
        if (m_rotationAngle != 0 || m_flipHorizontal || m_flipVertical) {
            QTransform transform = buildPinWindowTransform(
                pixmapRect, m_rotationAngle, m_flipHorizontal, m_flipVertical);
            painter.setTransform(transform, true);
        }

        m_annotationLayer->draw(painter);

        // Draw transformation gizmo for selected text annotation
        if (auto* textItem = getSelectedTextAnnotation()) {
            TransformationGizmo::draw(painter, textItem);
        }

        // Draw transformation gizmo for selected emoji sticker.
        if (auto* emojiItem = getSelectedEmojiStickerAnnotation()) {
            TransformationGizmo::draw(painter, emojiItem);
        }

        // Draw transformation gizmo for selected Arrow (in transformed space)
        if (auto* arrowItem = getSelectedArrowAnnotation()) {
            TransformationGizmo::draw(painter, arrowItem);
        }

        // Draw transformation gizmo for selected Polyline (in transformed space)
        if (auto* polylineItem = getSelectedPolylineAnnotation()) {
            TransformationGizmo::draw(painter, polylineItem);
        }

        painter.restore();
    }

    // Draw annotation preview (current stroke in progress)
    if (m_annotationMode && m_toolManager) {
        painter.save();

        // Apply the same transform to preview (matching annotation layer transform above)
        if (m_rotationAngle != 0 || m_flipHorizontal || m_flipVertical) {
            QTransform transform = buildPinWindowTransform(
                pixmapRect, m_rotationAngle, m_flipHorizontal, m_flipVertical);
            painter.setTransform(transform, true);
        }

        m_toolManager->drawCurrentPreview(painter);
        painter.restore();
    }

    // Draw border if enabled
    if (m_showBorder) {
        int cornerRadius = effectiveCornerRadius(size());
        // Define colors inline (previously in Constants.h but only used here)
        static const QColor kBorderIndigo{88, 86, 214, 200};
        static const QColor kBorderGreen{52, 199, 89, 200};
        static const QColor kBorderBlue{0, 122, 255, 200};
        
        if (m_clickThrough) {
            // Dashed indigo border for click-through mode
            painter.setPen(QPen(kBorderIndigo, 2, Qt::DashLine));
        }
        else if (m_annotationMode) {
            // Green border for annotation mode
            painter.setPen(QPen(kBorderGreen, 2));
        }
        else {
            // Blue border
            painter.setPen(QPen(kBorderBlue, 1.5));
        }
        painter.setBrush(Qt::NoBrush);
        if (cornerRadius > 0) {
            painter.drawRoundedRect(pixmapRect.adjusted(1, 1, -1, -1), cornerRadius, cornerRadius);
        }
        else {
            painter.drawRect(pixmapRect.adjusted(1, 1, -1, -1));
        }
    }

    // Draw watermark
    WatermarkRenderer::render(painter, pixmapRect, m_watermarkSettings);

    // Draw loading spinner when OCR or QR Code scan is in progress
    if (m_ocrInProgress || m_qrCodeInProgress) {
        QPoint center = pixmapRect.center();
        m_loadingSpinner->draw(painter, center);
    }

    // Draw live capture indicator
    if (m_isLiveMode) {
        painter.save();

        constexpr int dotRadius = kLiveIndicatorRadius;
        constexpr int margin = kLiveIndicatorMargin;
        QPointF dotCenter(margin + dotRadius, margin + dotRadius);

        // Pulsing effect based on time
        qreal pulse = kPulseBase + kPulseAmplitude * qSin(QDateTime::currentMSecsSinceEpoch() / kPulsePeriodMs);
        
        // Define live indicator colors inline
        static const QColor kLiveActive{255, 0, 0, 150};
        static const QColor kLivePaused{255, 165, 0};
        
        QColor dotColor = m_livePaused
            ? kLivePaused.darker(kPausedDarkerPercent)
            : QColor(kLiveActive.red(), kLiveActive.green(),
                kLiveActive.blue(), kMinAlpha + static_cast<int>(pulse * kAlphaRange));

        painter.setPen(Qt::NoPen);
        painter.setBrush(dotColor);
        painter.drawEllipse(dotCenter, dotRadius, dotRadius);

        painter.restore();
    }

    // Draw region layout mode overlay
    if (isRegionLayoutMode()) {
        qreal dpr = m_originalPixmap.devicePixelRatio();
        if (dpr <= 0.0) {
            dpr = 1.0;
        }
        const auto& regions = m_regionLayoutManager->regions();
        int selectedIdx = m_regionLayoutManager->selectedIndex();
        int hoveredIdx = m_regionLayoutManager->hoveredIndex();

        // Draw region images at their current positions
        for (const auto& region : regions) {
            QRect targetRect = region.rect;
            if (region.rect.size() != region.originalRect.size()) {
                // Scale the image if region was resized
                QImage scaled = region.image.scaled(
                    CoordinateHelper::toPhysical(region.rect.size(), dpr),
                    Qt::IgnoreAspectRatio,
                    Qt::SmoothTransformation);
                scaled.setDevicePixelRatio(dpr);
                painter.drawImage(targetRect.topLeft(), scaled);
            } else {
                QImage img = region.image;
                img.setDevicePixelRatio(dpr);
                painter.drawImage(targetRect.topLeft(), img);
            }
        }

        // Draw region borders, handles, and badges
        RegionLayoutRenderer::drawRegions(painter, regions, selectedIdx, hoveredIdx, dpr);

        // Draw confirm/cancel buttons
        QRect canvasBounds = m_regionLayoutManager->canvasBounds();
        RegionLayoutRenderer::drawConfirmCancelButtons(painter, canvasBounds, m_layoutModeMousePos);
    }
}

void PinWindow::enterEvent(QEnterEvent* event)
{
    if (m_annotationMode) {
        updateCursorForTool();
    }
    QWidget::enterEvent(event);
}

void PinWindow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_consumeNextToolRelease = false;

        // 0. Handle Region Layout Mode interactions
        if (isRegionLayoutMode()) {
            QPoint pos = event->pos();
            QRect canvasBounds = m_regionLayoutManager->canvasBounds();

            // Check confirm/cancel button clicks
            if (RegionLayoutRenderer::confirmButtonRect(canvasBounds).contains(pos)) {
                exitRegionLayoutMode(true);
                return;
            }
            if (RegionLayoutRenderer::cancelButtonRect(canvasBounds).contains(pos)) {
                exitRegionLayoutMode(false);
                return;
            }

            // Check for resize handle hit on selected region
            ResizeHandler::Edge handle = m_regionLayoutManager->hitTestHandle(pos);
            if (handle != ResizeHandler::Edge::None) {
                m_regionLayoutManager->startResize(handle, pos);
                return;
            }

            // Check for region hit
            int regionIdx = m_regionLayoutManager->hitTestRegion(pos);
            if (regionIdx >= 0) {
                m_regionLayoutManager->selectRegion(regionIdx);
                m_regionLayoutManager->startDrag(pos);
                return;
            }

            // Click on empty space - deselect
            m_regionLayoutManager->selectRegion(-1);
            return;
        }

        // 1. First handle inline text editing (finish/cancel when clicking outside)
        if (handleTextEditorPress(event->pos())) {
            return;
        }

        // 2. Handle gizmo interaction (scale/rotate selected text)
        if (m_annotationMode && handleGizmoPress(event->pos())) {
            return;
        }

        // 3. Handle clicking on existing text annotations
        if (m_annotationMode && handleTextAnnotationPress(event->pos())) {
            return;
        }

        // 3.5 Handle emoji sticker interaction before clearing selection.
        if (m_annotationMode && handleEmojiStickerAnnotationPress(event->pos())) {
            return;
        }

        // 4. Clear active annotation selection if clicking elsewhere.
        bool clearedSelectedEmojiInEmojiTool = false;
        if (m_annotationMode && m_annotationLayer && m_annotationLayer->selectedIndex() >= 0) {
            if (m_currentToolId == ToolId::EmojiSticker) {
                clearedSelectedEmojiInEmojiTool =
                    dynamic_cast<EmojiStickerAnnotation*>(m_annotationLayer->selectedItem()) != nullptr;
            }
            m_annotationLayer->clearSelection();
            update();
        }

        // 5. In annotation mode with a drawing tool
        if (m_annotationMode && isAnnotationTool(m_currentToolId)) {
            // Text tool: start new text editing
            if (m_currentToolId == ToolId::Text && m_textAnnotationEditor) {
                m_textAnnotationEditor->startEditing(event->pos(), rect(), m_annotationColor);
                update();
                return;
            }

            // Check for Arrow annotation interaction (original coords handled inside)
            if (handleArrowAnnotationPress(event->pos())) {
                return;
            }

            // Check for Polyline annotation interaction (original coords handled inside)
            if (handlePolylineAnnotationPress(event->pos())) {
                return;
            }

            // Other annotation tools route to ToolManager
            if (m_toolManager) {
                if (clearedSelectedEmojiInEmojiTool) {
                    // Keep this click for deselection only; do not place a new emoji on release.
                    m_consumeNextToolRelease = true;
                }
                // Transform display coordinates to original image coordinates
                // This is needed because annotations are stored in original space
                // but rendered with rotation/flip transforms applied
                m_toolManager->handleMousePress(mapToOriginalCoords(event->pos()), event->modifiers());
                update();
                return;
            }
        }

        ResizeHandler::Edge edge = m_resizeHandler->getEdgeAt(event->pos(), size());

        if (edge != ResizeHandler::Edge::None) {
            // Start resizing
            m_isResizing = true;
            m_resizeHandler->startResize(edge, event->globalPosition().toPoint(), size(), pos());
        }
        else {
            // Start dragging
            m_isDragging = true;
            m_dragStartPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
            CursorManager::instance().setDragStateForWidget(this, DragState::WidgetDrag);
        }
    }
}

void PinWindow::mouseMoveEvent(QMouseEvent* event)
{
    // Handle Region Layout Mode
    if (isRegionLayoutMode()) {
        QPoint pos = event->pos();
        m_layoutModeMousePos = pos;  // Store for hover effects

        if (m_regionLayoutManager->isDragging()) {
            m_regionLayoutManager->updateDrag(pos);
            return;
        }

        if (m_regionLayoutManager->isResizing()) {
            bool shiftHeld = event->modifiers() & Qt::ShiftModifier;
            m_regionLayoutManager->updateResize(pos, shiftHeld);
            return;
        }

        // Update hover state
        int hoveredRegion = m_regionLayoutManager->hitTestRegion(pos);
        m_regionLayoutManager->setHoveredIndex(hoveredRegion);

        // Update cursor based on what's under the mouse
        ResizeHandler::Edge handle = m_regionLayoutManager->hitTestHandle(pos);
        if (handle != ResizeHandler::Edge::None) {
            // Set resize cursor based on edge
            switch (handle) {
                case ResizeHandler::Edge::Left:
                case ResizeHandler::Edge::Right:
                    setCursor(Qt::SizeHorCursor);
                    break;
                case ResizeHandler::Edge::Top:
                case ResizeHandler::Edge::Bottom:
                    setCursor(Qt::SizeVerCursor);
                    break;
                case ResizeHandler::Edge::TopLeft:
                case ResizeHandler::Edge::BottomRight:
                    setCursor(Qt::SizeFDiagCursor);
                    break;
                case ResizeHandler::Edge::TopRight:
                case ResizeHandler::Edge::BottomLeft:
                    setCursor(Qt::SizeBDiagCursor);
                    break;
                default:
                    setCursor(Qt::ArrowCursor);
                    break;
            }
        } else if (hoveredRegion >= 0) {
            setCursor(Qt::SizeAllCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }

        update();
        return;
    }

    // Handle confirm mode text dragging
    if (m_textEditor && m_textEditor->isConfirmMode() && m_textEditor->isDragging()) {
        m_textEditor->handleMouseMove(event->pos());
        update();
        return;
    }

    // Handle TextAnnotationEditor transformation (rotate/scale)
    if (m_textAnnotationEditor && m_textAnnotationEditor->isTransforming()) {
        m_textAnnotationEditor->updateTransformation(mapToOriginalCoords(event->pos()));
        update();
        return;
    }

    // Handle TextAnnotationEditor dragging
    if (m_textAnnotationEditor && m_textAnnotationEditor->isDragging()) {
        m_textAnnotationEditor->updateDragging(mapToOriginalCoords(event->pos()));
        update();
        return;
    }

    // Handle Emoji sticker dragging/scaling
    if (m_isEmojiDragging || m_isEmojiScaling) {
        handleEmojiStickerAnnotationMove(event->pos());
        return;
    }

    // Handle Arrow dragging
    if (m_isArrowDragging) {
        handleArrowAnnotationMove(event->pos());
        return;
    }

    // Handle Polyline dragging
    if (m_isPolylineDragging) {
        handlePolylineAnnotationMove(event->pos());
        return;
    }

    // In annotation mode with active drawing, route to ToolManager
    if (m_annotationMode && m_toolManager && m_toolManager->isDrawing()) {
        m_toolManager->handleMouseMove(mapToOriginalCoords(event->pos()), event->modifiers());
        update();
        return;
    }

    if (m_isResizing) {
        QSize newSize;
        QPoint newPos;
        m_resizeHandler->updateResize(event->globalPosition().toPoint(), newSize, newPos);

        // Use cached transform (performance optimization)
        ensureTransformCacheValid();

        // Use transformed dimensions for zoom calculation
        QSize transformedLogicalSize = logicalSizeFromPixmap(m_transformedCache);
        qreal scaleX = static_cast<qreal>(newSize.width()) / transformedLogicalSize.width();
        qreal scaleY = static_cast<qreal>(newSize.height()) / transformedLogicalSize.height();
        m_zoomLevel = qMin(scaleX, scaleY);

        // Use FastTransformation during resize for responsiveness
        // High-quality scaling will be done after resize is complete
        qreal dpr = m_transformedCache.devicePixelRatio();
        if (dpr <= 0.0) {
            dpr = 1.0;
        }
        QSize newDeviceSize = CoordinateHelper::toPhysical(newSize, dpr);
        m_displayPixmap = m_transformedCache.scaled(newDeviceSize,
            Qt::KeepAspectRatio,
            Qt::FastTransformation);
        m_displayPixmap.setDevicePixelRatio(dpr);

        // Schedule high-quality update after resize ends
        m_pendingHighQualityUpdate = true;
        m_resizeFinishTimer->start(150);  // Resize debounce

        // Apply new size and position
        setFixedSize(newSize);
        move(newPos);
        syncCropHandlerImageSize();
        update();

        m_uiIndicators->showZoomIndicator(m_zoomLevel);

    }
    else if (m_isDragging) {
        move(event->globalPosition().toPoint() - m_dragStartPos);
    }
    else {
        auto& cm = CursorManager::instance();
        // Only update resize cursor when NOT in annotation mode
        // In annotation mode, keep the tool cursor (set by updateCursorForTool)
        if (!m_annotationMode) {
            ResizeHandler::Edge edge = m_resizeHandler->getEdgeAt(event->pos(), size());
            if (edge != ResizeHandler::Edge::None) {
                cm.setHoverTargetForWidget(this, HoverTarget::ResizeHandle, static_cast<int>(edge));
            } else {
                cm.setHoverTargetForWidget(this, HoverTarget::None);
            }
        } else {
            // In annotation mode, check for annotation hovers
            // updateAnnotationCursor uses state-driven API and handles cursor automatically
            updateAnnotationCursor(event->pos());
        }
    }
}

void PinWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        // Handle Region Layout Mode
        if (isRegionLayoutMode()) {
            if (m_regionLayoutManager->isDragging()) {
                m_regionLayoutManager->finishDrag();
            }
            if (m_regionLayoutManager->isResizing()) {
                m_regionLayoutManager->finishResize();
            }
            return;
        }

        // Handle confirm mode text dragging end
        if (m_textEditor && m_textEditor->isDragging()) {
            m_textEditor->handleMouseRelease(event->pos());
            update();
            return;
        }

        // Handle TextAnnotationEditor transformation/dragging end
        if (m_textAnnotationEditor) {
            if (m_textAnnotationEditor->isTransforming()) {
                m_textAnnotationEditor->finishTransformation();
                updateUndoRedoState();
                update();
                return;
            }
            if (m_textAnnotationEditor->isDragging()) {
                m_textAnnotationEditor->finishDragging();
                updateUndoRedoState();
                update();
                return;
            }
        }

        // Handle emoji sticker release
        if (handleEmojiStickerAnnotationRelease(event->pos())) {
            return;
        }

        // Handle Arrow release
        if (handleArrowAnnotationRelease(event->pos())) {
            return;
        }

        // Handle Polyline release
        if (handlePolylineAnnotationRelease(event->pos())) {
            return;
        }

        if (m_consumeNextToolRelease) {
            m_consumeNextToolRelease = false;
            return;
        }

        // In annotation mode, route to ToolManager
        if (m_annotationMode && m_toolManager) {
            // For drawing tools: only route if actively drawing
            // For single-click tools (Emoji, StepBadge): always route release
            bool isSingleClickTool = (m_currentToolId == ToolId::EmojiSticker ||
                m_currentToolId == ToolId::StepBadge);

            if (m_toolManager->isDrawing() || isSingleClickTool) {
                const AnnotationItem* previousLastItem = nullptr;
                if (m_annotationLayer && m_annotationLayer->itemCount() > 0) {
                    previousLastItem = m_annotationLayer->itemAt(
                        static_cast<int>(m_annotationLayer->itemCount() - 1));
                }
                m_toolManager->handleMouseRelease(mapToOriginalCoords(event->pos()), event->modifiers());
                if (m_currentToolId == ToolId::EmojiSticker) {
                    compensateNewestEmojiIfNeeded(previousLastItem);
                } else if (m_currentToolId == ToolId::StepBadge) {
                    compensateNewestStepBadgeIfNeeded(previousLastItem);
                }
                updateUndoRedoState();
                update();
                return;
            }
        }

        if (m_isResizing) {
            m_isResizing = false;
            m_resizeHandler->finishResize();
        }
        if (m_isDragging) {
            m_isDragging = false;
            CursorManager::instance().setDragStateForWidget(this, DragState::None);
            if (m_annotationMode) {
                // Refresh tool cursor after drag ends
                updateCursorForTool();
            }
        }
        if (m_clickThrough) {
            updateClickThroughForCursor();
        }
    }
}

void PinWindow::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        // In annotation mode with active drawing, forward to tool handler
        // (e.g., polyline mode uses double-click to finish drawing)
        if (m_annotationMode && m_toolManager && m_toolManager->isDrawing()) {
            m_toolManager->handleDoubleClick(event->pos());
            update();
            return;
        }

        // Check if double-click is on a text annotation (for re-editing)
        if (m_annotationMode && m_annotationLayer) {
            int hitIndex = m_annotationLayer->hitTestText(mapToOriginalCoords(event->pos()));
            if (hitIndex >= 0) {
                m_annotationLayer->setSelectedIndex(hitIndex);
                m_textAnnotationEditor->startReEditing(hitIndex, m_annotationColor);
                update();
                return;
            }
        }

        // Otherwise, close the window (default behavior)
        close();
    }
}

void PinWindow::wheelEvent(QWheelEvent* event)
{
    // Ctrl+Wheel = Opacity adjustment
    if (event->modifiers() & Qt::ControlModifier) {
        adjustOpacityByStep(event->angleDelta().y() > 0 ? 1 : -1);
        event->accept();
        return;
    }

    // Plain wheel = Zoom (anchored at top-left corner)
    const qreal zoomStep = PinWindowSettingsManager::instance().loadZoomStep();
    qreal oldZoom = m_zoomLevel;
    qreal newZoom = (event->angleDelta().y() > 0)
        ? m_zoomLevel + zoomStep
        : m_zoomLevel - zoomStep;
    newZoom = qBound(0.1, newZoom, 5.0);

    if (qFuzzyCompare(oldZoom, newZoom)) {
        event->accept();
        return;
    }

    // Keep top-left corner fixed - no position adjustment needed
    setZoomLevel(newZoom);

    m_uiIndicators->showZoomIndicator(m_zoomLevel);
    event->accept();
}

void PinWindow::contextMenuEvent(QContextMenuEvent* event)
{
    // Lazy-create context menu on first right-click to improve initial window creation performance
    if (!m_contextMenu) {
        createContextMenu();
    }

    // Update Show Toolbar checked state
    if (m_showToolbarAction) {
        m_showToolbarAction->setChecked(m_toolbarVisible);
    }

    if (m_adjustRegionLayoutAction) {
        const bool canAdjustLayout = m_hasMultiRegionData && !m_isLiveMode;
        m_adjustRegionLayoutAction->setVisible(canAdjustLayout);
        m_adjustRegionLayoutAction->setEnabled(canAdjustLayout);
    }

    if (m_mergePinsAction) {
        m_mergePinsAction->setEnabled(eligibleMergePinCount() >= 2);
    }

    // Update live mode menu items based on current state
    if (m_startLiveAction) {
        bool canStartLive = !m_sourceRegion.isEmpty() && m_sourceScreen;
        m_startLiveAction->setEnabled(canStartLive || m_isLiveMode);
        m_startLiveAction->setText(m_isLiveMode ? tr("Stop Live Update") : tr("Start Live Update"));
    }
    if (m_pauseLiveAction) {
        m_pauseLiveAction->setVisible(m_isLiveMode);
        m_pauseLiveAction->setText(m_livePaused ? tr("Resume Live Update") : tr("Pause Live Update"));
    }
    if (m_fpsMenu) {
        // Use menuAction() to control visibility - direct setVisible on QMenu causes positioning issues
        m_fpsMenu->menuAction()->setVisible(m_isLiveMode);
        // Update checked state for current frame rate
        for (QAction* action : m_fpsMenu->actions()) {
            action->setChecked(action->data().toInt() == m_captureFrameRate);
        }
    }

    // Use popup() instead of exec() to avoid blocking the event loop.
    // This prevents UI freeze when global hotkeys (like F2) are pressed
    // while the context menu is open.
    m_contextMenu->popup(event->globalPos());
}

void PinWindow::keyPressEvent(QKeyEvent* event)
{
    // Handle text editor state first (like RegionSelector)
    if (m_textEditor && m_textEditor->isEditing()) {
        if (event->key() == Qt::Key_Escape) {
            m_textEditor->cancelEditing();
            return;
        }
        if (m_textEditor->isConfirmMode()) {
            // In confirm mode: Enter finishes editing
            if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
                m_textEditor->finishEditing();  // Emits editingFinished signal
                return;
            }
        }
        else {
            // In typing mode: Ctrl+Enter or Shift+Enter enters confirm mode
            if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
                (event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
                if (!m_textEditor->textEdit()->toPlainText().trimmed().isEmpty()) {
                    m_textEditor->enterConfirmMode();
                }
                return;
            }
        }
        // Let other keys pass through to text editor
        return;
    }

    // Handle Region Layout Mode
    if (isRegionLayoutMode()) {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            exitRegionLayoutMode(true);  // Confirm
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Escape) {
            exitRegionLayoutMode(false);  // Cancel
            event->accept();
            return;
        }
        // Ignore other keys in layout mode
        return;
    }

    // Spacebar toggles toolbar
    if (event->key() == Qt::Key_Space) {
        toggleToolbar();
        event->accept();
        return;
    }

    // Escape handling
    if (event->key() == Qt::Key_Escape) {
        // Try to handle escape in tool first (e.g. finish polyline)
        if (m_toolManager && m_toolManager->handleEscape()) {
            event->accept();
            return;
        }

        if (m_toolbarVisible) {
            hideToolbar();
            event->accept();
            return;
        }
        close();
        return;
    }

    // Undo/Redo in annotation mode
    if (m_annotationMode && m_annotationLayer) {
        if (event->matches(QKeySequence::Undo)) {
            handleToolbarUndo();
            event->accept();
            return;
        }
        if (event->matches(QKeySequence::Redo)) {
            handleToolbarRedo();
            event->accept();
            return;
        }
    }

    // Live capture shortcuts
    // L - Toggle live mode (start/stop)
    if (event->key() == Qt::Key_L && !m_sourceRegion.isEmpty()) {
        if (m_isLiveMode) {
            stopLiveCapture();
        }
        else {
            startLiveCapture();
        }
        event->accept();
        return;
    }

    // P - Pause/Resume live capture (only when live mode is active)
    if (event->key() == Qt::Key_P && m_isLiveMode) {
        if (m_livePaused) {
            resumeLiveCapture();
        }
        else {
            pauseLiveCapture();
        }
        event->accept();
        return;
    }

    // Rotation and flip shortcuts
    if (event->key() == Qt::Key_1) {
        rotateRight();
        event->accept();
        return;
    }
    else if (event->key() == Qt::Key_2) {
        rotateLeft();
        event->accept();
        return;
    }
    else if (event->key() == Qt::Key_3) {
        flipHorizontal();
        event->accept();
        return;
    }
    else if (event->key() == Qt::Key_4) {
        flipVertical();
        event->accept();
        return;
    }
    else if (event->key() == Qt::Key_BracketRight) {
        adjustOpacityByStep(1);
        event->accept();
        return;
    }
    else if (event->key() == Qt::Key_BracketLeft) {
        adjustOpacityByStep(-1);
        event->accept();
        return;
    }

    if (event->matches(QKeySequence::Save)) {
        saveToFile();
    }
    else if (event->matches(QKeySequence::Copy)) {
        copyToClipboard();
    }
    else {
        QWidget::keyPressEvent(event);
    }
}

void PinWindow::closeEvent(QCloseEvent* event)
{
    emit closed(this);
    event->accept();
}

void PinWindow::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    updateToolbarPosition();
}

// ============================================================================
// Toolbar and Annotation Methods
// ============================================================================

void PinWindow::initializeAnnotationComponents()
{
    // Initialize annotation layer
    m_annotationLayer = new AnnotationLayer(this);
    connect(m_annotationLayer, &AnnotationLayer::changed,
        this, QOverload<>::of(&QWidget::update));

    // Initialize tool manager
    m_toolManager = new ToolManager(this);
    m_toolManager->registerDefaultHandlers();
    m_toolManager->setAnnotationLayer(m_annotationLayer);
    // Create shared pixmap for mosaic tool (explicit sharing to avoid memory duplication)
    m_sharedSourcePixmap = std::make_shared<const QPixmap>(m_originalPixmap);
    m_toolManager->setSourcePixmap(m_sharedSourcePixmap);  // Required for Mosaic tool

    // Set up crop tool callback
    if (auto* cropHandler = dynamic_cast<CropToolHandler*>(m_toolManager->handler(ToolId::Crop))) {
        cropHandler->setCropCallback([this](const QRect& rect) { applyCrop(rect); });
        cropHandler->setImageSize(cropToolImageSize());
    } else {
        qWarning() << "PinWindow: Failed to get CropToolHandler. Crop tool will not function.";
    }

    // Load saved annotation settings
    auto& annotationSettings = AnnotationSettingsManager::instance();
    m_annotationColor = annotationSettings.loadColor();
    m_annotationWidth = annotationSettings.loadWidth();
    m_stepBadgeSize = annotationSettings.loadStepBadgeSize();
    m_toolManager->setColor(m_annotationColor);
    m_toolManager->setWidth(m_annotationWidth);

    // Connect tool manager signals
    connect(m_toolManager, &ToolManager::needsRepaint,
        this, QOverload<>::of(&QWidget::update));

    // Tool manager is created lazily; bind it to the already-registered widget now.
    auto& cursorManager = CursorManager::instance();
    cursorManager.setToolManagerForWidget(this, m_toolManager);

    // Initialize text annotation editor components
    m_textEditor = new InlineTextEditor(this);
    m_textAnnotationEditor = new TextAnnotationEditor(this);
    m_textAnnotationEditor->setCoordinateMappers(
        [this](const QPointF& displayPos) { return mapToOriginalCoords(displayPos); },
        [this](const QPointF& originalPos) { return mapFromOriginalCoords(originalPos); });

    m_annotationContext = std::make_unique<AnnotationContext>(*this);
    m_annotationContext->setupTextAnnotationEditor(false, false);
    m_annotationContext->connectTextEditorSignals();

    // Initialize toolbar (not parented - separate floating window)
    m_toolbar = std::make_unique<WindowedToolbar>(nullptr);
    m_toolbar->setOCRAvailable(PlatformFeatures::instance().isOCRAvailable());

    // Connect toolbar signals
    connect(m_toolbar.get(), &WindowedToolbar::toolSelected,
        this, &PinWindow::handleToolbarToolSelected);
    connect(m_toolbar.get(), &WindowedToolbar::undoClicked,
        this, &PinWindow::handleToolbarUndo);
    connect(m_toolbar.get(), &WindowedToolbar::redoClicked,
        this, &PinWindow::handleToolbarRedo);
    connect(m_toolbar.get(), &WindowedToolbar::ocrClicked,
        this, &PinWindow::performOCR);
    connect(m_toolbar.get(), &WindowedToolbar::qrCodeClicked,
        this, &PinWindow::performQRCodeScan);
    connect(m_toolbar.get(), &WindowedToolbar::copyClicked,
        this, &PinWindow::copyToClipboard);
    connect(m_toolbar.get(), &WindowedToolbar::saveClicked,
        this, &PinWindow::saveToFile);
    connect(m_toolbar.get(), &WindowedToolbar::doneClicked,
        this, &PinWindow::hideToolbar);
    connect(m_toolbar.get(), &WindowedToolbar::cursorRestoreRequested,
        this, &PinWindow::updateCursorForTool);

    // Initialize sub-toolbar (not parented - separate floating window)
    m_subToolbar = std::make_unique<WindowedSubToolbar>(nullptr);

    // Connect sub-toolbar signals
    connect(m_subToolbar.get(), &WindowedSubToolbar::emojiSelected,
        this, &PinWindow::onEmojiSelected);
    connect(m_subToolbar.get(), &WindowedSubToolbar::stepBadgeSizeChanged,
        this, &PinWindow::onStepBadgeSizeChanged);
    connect(m_subToolbar.get(), &WindowedSubToolbar::shapeTypeChanged,
        this, &PinWindow::onShapeTypeChanged);
    connect(m_subToolbar.get(), &WindowedSubToolbar::shapeFillModeChanged,
        this, &PinWindow::onShapeFillModeChanged);
    connect(m_subToolbar.get(), &WindowedSubToolbar::autoBlurRequested,
        this, &PinWindow::onAutoBlurRequested);
    connect(m_subToolbar.get(), &WindowedSubToolbar::cursorRestoreRequested,
        this, &PinWindow::updateCursorForTool);

    // Initialize settings helper for text font dropdowns
    m_settingsHelper = new RegionSettingsHelper(this);
    m_settingsHelper->setParentWidget(m_subToolbar.get());
    connect(m_settingsHelper, &RegionSettingsHelper::fontSizeSelected,
        this, &PinWindow::onFontSizeSelected);
    connect(m_settingsHelper, &RegionSettingsHelper::fontFamilySelected,
        this, &PinWindow::onFontFamilySelected);

    // Connect TextAnnotationEditor to ToolOptionsPanel (must be after sub-toolbar creation)
    m_textAnnotationEditor->setColorAndWidthWidget(m_subToolbar->colorAndWidthWidget());

    // Centralized common ToolOptionsPanel signal wiring
    m_annotationContext->connectToolOptionsSignals();
    m_annotationContext->connectTextFormattingSignals();
    m_annotationContext->syncTextFormattingControls();

    // Sync initial width and color to sub-toolbar UI
    m_subToolbar->colorAndWidthWidget()->setCurrentWidth(m_annotationWidth);
    m_subToolbar->colorAndWidthWidget()->setCurrentColor(m_annotationColor);
}

void PinWindow::toggleToolbar()
{
    if (m_toolbarVisible) {
        hideToolbar();
    }
    else {
        showToolbar();
    }
}

void PinWindow::showToolbar()
{
    // Lazy initialization of annotation components
    if (!m_toolbar) {
        initializeAnnotationComponents();
    }

    // Set associated widgets for click-outside detection
    m_toolbar->setAssociatedWidgets(this, m_subToolbar.get());

    // Connect close request signal
    connect(m_toolbar.get(), &WindowedToolbar::closeRequested,
        this, &PinWindow::hideToolbar, Qt::UniqueConnection);

    m_toolbarVisible = true;
    updateUndoRedoState();
    m_toolbar->show();
    m_toolbar->raise();

    // Position AFTER show to ensure correct geometry on macOS
    updateToolbarPosition();
}

void PinWindow::hideToolbar()
{
    if (m_toolbar) {
        m_toolbar->hide();
    }
    m_toolbarVisible = false;
    exitAnnotationMode();
}

void PinWindow::updateToolbarPosition()
{
    if (m_toolbar && m_toolbarVisible) {
        m_toolbar->positionNear(frameGeometry());
        // Also update sub-toolbar position if visible
        updateSubToolbarPosition();
    }
}

void PinWindow::enterAnnotationMode()
{
    if (m_annotationMode) {
        return;
    }

    m_annotationMode = true;
    CursorManager::instance().clearAllForWidget(this);
    updateCursorForTool();
    update();
}

void PinWindow::updateCursorForTool()
{
    auto& cursorManager = CursorManager::instance();

    if (!m_annotationMode) {
        cursorManager.clearAllForWidget(this);
        return;
    }

    // Determine the appropriate cursor for the current tool
    QCursor toolCursor = Qt::CrossCursor;

    if (m_toolManager) {
        ToolId currentTool = m_toolManager->currentTool();

        // Special handling for mosaic tool
        // Use 2x width for mosaic cursor (UI shows half the actual drawing size)
        if (currentTool == ToolId::Mosaic) {
            int mosaicWidth = m_toolManager->width() * 2;
            toolCursor = CursorManager::createMosaicCursor(mosaicWidth);
        }
        else {
            // Use handler's cursor() method
            IToolHandler* handler = m_toolManager->currentHandler();
            if (handler) {
                toolCursor = handler->cursor();
            }
        }
    }

    // Use CursorManager so the tool cursor persists across hover/drag contexts
    cursorManager.pushCursorForWidget(this, CursorContext::Tool, toolCursor);
}

void PinWindow::exitAnnotationMode()
{
    if (!m_annotationMode) {
        return;
    }

    m_annotationMode = false;
    m_currentToolId = ToolId::Selection;
    CursorManager::instance().clearAllForWidget(this);

    if (m_toolbar) {
        m_toolbar->setActiveButton(-1);
    }

    // Hide sub-toolbar when exiting annotation mode
    hideSubToolbar();

    update();
}

void PinWindow::handleToolbarToolSelected(int toolId)
{
    if (toolId < 0 || toolId >= static_cast<int>(ToolId::Count)) {
        return;
    }
    ToolId tool = static_cast<ToolId>(toolId);

    // Check if same tool clicked - toggle sub-toolbar visibility (matches RegionSelector behavior)
    bool sameToolClicked = (m_currentToolId == tool && m_annotationMode);

    m_currentToolId = tool;

    if (m_toolManager) {
        m_toolManager->setCurrentTool(tool);
    }

    if (m_toolbar) {
        m_toolbar->setActiveButton(toolId);
    }

    if (isAnnotationTool(tool)) {
        if (!isActiveWindow()) {
            activateWindow();
            raise();
        }
        if (!hasFocus()) {
            setFocus(Qt::OtherFocusReason);
        }
        if (sameToolClicked && m_subToolbar && m_subToolbar->isVisible()) {
            // Same tool clicked while sub-toolbar visible - exit annotation mode entirely
            exitAnnotationMode();
            return;
        }

        enterAnnotationMode();
        // Update cursor for the new tool (enterAnnotationMode also calls this,
        // but we need to update when switching between annotation tools too)
        updateCursorForTool();

        // Show sub-toolbar for the selected tool
        if (m_subToolbar) {
            m_subToolbar->showForTool(toolId);
            updateSubToolbarPosition();
        }
    }
    else {
        exitAnnotationMode();
        hideSubToolbar();
    }
}

void PinWindow::handleToolbarUndo()
{
    const bool annotationCanUndo = m_annotationLayer && m_annotationLayer->canUndo();
    const bool cropCanUndo = canUndoCrop();

    if (annotationCanUndo && cropCanUndo) {
        const CropUndoEntry& latestCrop = m_cropUndoStack.constLast();
        const bool hasPostCropAnnotationUndo = !matchesCropAnnotationBoundary(latestCrop);

        if (hasPostCropAnnotationUndo) {
            m_annotationLayer->undo();
            updateUndoRedoState();
            update();
        } else {
            undoCrop();
        }
        return;
    }

    if (annotationCanUndo) {
        m_annotationLayer->undo();
        updateUndoRedoState();
        update();
    } else if (cropCanUndo) {
        undoCrop();
    }
}

void PinWindow::handleToolbarRedo()
{
    pruneInvalidCropRedoState();

    if (canRedoCrop()) {
        redoCrop();
        return;
    }

    if (m_annotationLayer && m_annotationLayer->canRedo()) {
        m_annotationLayer->redo();
        updateUndoRedoState();
        update();
    }
}

QSize PinWindow::cropToolImageSize() const
{
    const QSize displaySize = rect().size();
    if (displaySize.isEmpty()) {
        return QSize();
    }

    if (m_rotationAngle == 0 && !m_flipHorizontal && !m_flipVertical) {
        return displaySize;
    }

    const QRectF displayRect(QPointF(0, 0), QSizeF(displaySize));
    const QTransform inverse = buildPinWindowTransform(
        displayRect, m_rotationAngle, m_flipHorizontal, m_flipVertical).inverted();
    const QRectF mapped = inverse.mapRect(displayRect);

    return QSize(qMax(1, qRound(mapped.width())), qMax(1, qRound(mapped.height())));
}

void PinWindow::syncCropHandlerImageSize()
{
    if (!m_toolManager) {
        return;
    }

    if (auto* cropHandler = dynamic_cast<CropToolHandler*>(m_toolManager->handler(ToolId::Crop))) {
        cropHandler->setImageSize(cropToolImageSize());
    }
}

void PinWindow::applyCrop(const QRect& cropRect)
{
    if (cropRect.isEmpty()) return;

    qreal dpr = m_originalPixmap.devicePixelRatio();
    if (dpr <= 0.0) dpr = 1.0;

    // Crop tool coordinates follow the same space used by annotation tools.
    // This space tracks display size (including zoom) and orientation mapping.
    const QSize toolImageSize = cropToolImageSize();
    if (!toolImageSize.isValid() || toolImageSize.isEmpty()) {
        qWarning() << "PinWindow::applyCrop: invalid crop tool image size:" << toolImageSize;
        return;
    }

    const QRect clampedToolRect = clampCropRectToImagePixels(cropRect, toolImageSize);
    if (clampedToolRect.isEmpty()) {
        qWarning() << "PinWindow::applyCrop: crop rect does not intersect tool bounds."
                   << "cropRect:" << cropRect << "toolImageSize:" << toolImageSize;
        return;
    }

    const QSize sourceLogicalSize = logicalSizeFromPixmap(m_originalPixmap);
    if (sourceLogicalSize.isEmpty()) {
        qWarning() << "PinWindow::applyCrop: source logical size is empty.";
        return;
    }

    const qreal xScale = static_cast<qreal>(sourceLogicalSize.width()) / toolImageSize.width();
    const qreal yScale = static_cast<qreal>(sourceLogicalSize.height()) / toolImageSize.height();

    const qreal srcLeftF = clampedToolRect.left() * xScale;
    const qreal srcTopF = clampedToolRect.top() * yScale;
    const qreal srcRightF = (clampedToolRect.left() + clampedToolRect.width()) * xScale;
    const qreal srcBottomF = (clampedToolRect.top() + clampedToolRect.height()) * yScale;

    QRect sourceLogicalRect(
        qFloor(srcLeftF),
        qFloor(srcTopF),
        qCeil(srcRightF) - qFloor(srcLeftF),
        qCeil(srcBottomF) - qFloor(srcTopF));

    sourceLogicalRect = sourceLogicalRect.intersected(QRect(QPoint(0, 0), sourceLogicalSize));
    if (sourceLogicalRect.isEmpty()) {
        qWarning() << "PinWindow::applyCrop: source logical rect is empty after scaling."
                   << "toolRect:" << clampedToolRect << "sourceLogicalSize:" << sourceLogicalSize;
        return;
    }
    const QPoint sourceOffset = sourceLogicalRect.topLeft();
    // Annotations are authored in tool/display space, so keep a matching offset for translation.
    const QPoint annotationOffsetDisplay(
        qRound(static_cast<qreal>(sourceOffset.x()) * toolImageSize.width() / sourceLogicalSize.width()),
        qRound(static_cast<qreal>(sourceOffset.y()) * toolImageSize.height() / sourceLogicalSize.height()));

    // Convert logical crop rect to a covering device rect for QPixmap::copy.
    const QRect deviceRect = CoordinateHelper::toPhysicalCoveringRect(sourceLogicalRect, dpr);

    // Validate device rect against actual pixmap bounds
    QRect pixmapBounds(0, 0, m_originalPixmap.width(), m_originalPixmap.height());
    QRect clampedDeviceRect = deviceRect.intersected(pixmapBounds);
    if (clampedDeviceRect.isEmpty()) {
        qWarning() << "PinWindow::applyCrop: crop rect does not intersect pixmap bounds."
                    << "deviceRect:" << deviceRect << "pixmapSize:" << m_originalPixmap.size();
        return;
    }

    QPixmap cropped = m_originalPixmap.copy(clampedDeviceRect);
    if (cropped.isNull()) {
        qWarning() << "PinWindow::applyCrop: QPixmap::copy returned null for rect:" << clampedDeviceRect;
        return;
    }

    // Save current state for undo AFTER validation succeeds
    CropUndoEntry entry;
    entry.pixmap = m_originalPixmap;
    entry.rotationAngle = m_rotationAngle;
    entry.flipH = m_flipHorizontal;
    entry.flipV = m_flipVertical;
    entry.storedRegions = m_storedRegions;
    entry.hasMultiRegionData = m_hasMultiRegionData;
    entry.annotationOffsetDisplay = annotationOffsetDisplay;

    // Apply the cropped pixmap
    m_originalPixmap = cropped;
    m_originalPixmap.setDevicePixelRatio(dpr);

    // Cropping changes the canvas bounds/origin, so stored multi-region layout
    // metadata no longer matches the image.
    m_storedRegions.clear();
    m_hasMultiRegionData = false;
    entry.croppedPixmap = m_originalPixmap;
    entry.croppedRotationAngle = m_rotationAngle;
    entry.croppedFlipH = m_flipHorizontal;
    entry.croppedFlipV = m_flipVertical;
    entry.croppedStoredRegions = m_storedRegions;
    entry.croppedHasMultiRegionData = m_hasMultiRegionData;

    // Update shared pixmap for mosaic tool
    m_sharedSourcePixmap = std::make_shared<const QPixmap>(m_originalPixmap);
    if (m_toolManager) {
        m_toolManager->setSourcePixmap(m_sharedSourcePixmap);
    }

    // Invalidate transform cache
    m_cachedRotation = -1;

    // Translate annotations in display/tool coordinates (same space as tool input).
    if (m_annotationLayer) {
        const std::size_t annotationCount = m_annotationLayer->itemCount();
        if (annotationCount > 0) {
            entry.annotationTopItem =
                m_annotationLayer->itemAt(static_cast<int>(annotationCount - 1));
        }
        m_annotationLayer->translateAll(QPointF(-annotationOffsetDisplay.x(), -annotationOffsetDisplay.y()));
        refreshAllMosaicSources(m_annotationLayer, m_sharedSourcePixmap);
    }

    m_cropRedoStack.clear();
    m_cropUndoStack.append(std::move(entry));
    if (m_cropUndoStack.size() > kMaxCropUndoSize) {
        m_cropUndoStack.removeFirst();
    }

    // Exit crop mode and switch to Selection
    if (m_toolbar) {
        m_toolbar->setActiveButton(static_cast<int>(ToolId::Selection));
    }
    m_currentToolId = ToolId::Selection;
    if (m_toolManager) {
        m_toolManager->setCurrentTool(ToolId::Selection);
    }
    exitAnnotationMode();

    // Update display
    updateSize();
    updateToolbarPosition();
    syncCropHandlerImageSize();
    updateUndoRedoState();

    // Save to cache
    saveToCacheAsync();
}

void PinWindow::undoCrop()
{
    if (m_cropUndoStack.isEmpty()) return;

    // Restore previous state
    CropUndoEntry entry = m_cropUndoStack.takeLast();
    m_originalPixmap = entry.pixmap;
    m_rotationAngle = entry.rotationAngle;
    m_flipHorizontal = entry.flipH;
    m_flipVertical = entry.flipV;
    m_storedRegions = entry.storedRegions;
    m_hasMultiRegionData = entry.hasMultiRegionData;

    // Update shared pixmap
    m_sharedSourcePixmap = std::make_shared<const QPixmap>(m_originalPixmap);
    if (m_toolManager) {
        m_toolManager->setSourcePixmap(m_sharedSourcePixmap);
    }
    if (m_annotationLayer) {
        m_annotationLayer->translateAll(QPointF(entry.annotationOffsetDisplay.x(),
                                                entry.annotationOffsetDisplay.y()));
        refreshAllMosaicSources(m_annotationLayer, m_sharedSourcePixmap);
    }
    m_cropRedoStack.append(entry);
    if (m_cropRedoStack.size() > kMaxCropUndoSize) {
        m_cropRedoStack.removeFirst();
    }

    // Invalidate transform cache
    m_cachedRotation = -1;

    // Update display
    updateSize();
    updateToolbarPosition();
    syncCropHandlerImageSize();
    updateUndoRedoState();

    // Save to cache
    saveToCacheAsync();
}

void PinWindow::redoCrop()
{
    if (m_cropRedoStack.isEmpty()) return;

    CropUndoEntry entry = m_cropRedoStack.takeLast();
    m_originalPixmap = entry.croppedPixmap;
    m_rotationAngle = entry.croppedRotationAngle;
    m_flipHorizontal = entry.croppedFlipH;
    m_flipVertical = entry.croppedFlipV;
    m_storedRegions = entry.croppedStoredRegions;
    m_hasMultiRegionData = entry.croppedHasMultiRegionData;

    m_sharedSourcePixmap = std::make_shared<const QPixmap>(m_originalPixmap);
    if (m_toolManager) {
        m_toolManager->setSourcePixmap(m_sharedSourcePixmap);
    }
    if (m_annotationLayer) {
        m_annotationLayer->translateAll(QPointF(-entry.annotationOffsetDisplay.x(),
                                                -entry.annotationOffsetDisplay.y()));
        refreshAllMosaicSources(m_annotationLayer, m_sharedSourcePixmap);
    }

    m_cropUndoStack.append(entry);
    if (m_cropUndoStack.size() > kMaxCropUndoSize) {
        m_cropUndoStack.removeFirst();
    }

    m_cachedRotation = -1;

    updateSize();
    updateToolbarPosition();
    syncCropHandlerImageSize();
    updateUndoRedoState();

    saveToCacheAsync();
}

bool PinWindow::canUndoCrop() const
{
    return !m_cropUndoStack.isEmpty();
}

bool PinWindow::canRedoCrop() const
{
    if (m_cropRedoStack.isEmpty()) {
        return false;
    }
    if (!m_annotationLayer) {
        return true;
    }

    return matchesCropAnnotationBoundary(m_cropRedoStack.constLast());
}

void PinWindow::clearCropUndoHistory()
{
    if (m_cropUndoStack.isEmpty() && m_cropRedoStack.isEmpty()) {
        return;
    }

    m_cropUndoStack.clear();
    m_cropRedoStack.clear();
    updateUndoRedoState();
}

bool PinWindow::matchesCropAnnotationBoundary(const CropUndoEntry& entry) const
{
    if (!m_annotationLayer) {
        return true;
    }

    const std::size_t currentCount = m_annotationLayer->itemCount();
    const AnnotationItem* currentTopItem = (currentCount > 0)
        ? m_annotationLayer->itemAt(static_cast<int>(currentCount - 1))
        : nullptr;

    return currentTopItem == entry.annotationTopItem;
}

void PinWindow::pruneInvalidCropRedoState()
{
    if (m_cropRedoStack.isEmpty() || !m_annotationLayer) {
        return;
    }

    if (matchesCropAnnotationBoundary(m_cropRedoStack.constLast())) {
        return;
    }

    // If no annotation redo steps can bring us back to the crop boundary,
    // this crop redo branch has been invalidated by a new edit path.
    if (!m_annotationLayer->canRedo()) {
        m_cropRedoStack.clear();
    }
}

void PinWindow::updateUndoRedoState()
{
    if (m_annotationLayer) {
        pruneInvalidCropRedoState();
    }

    if (m_toolbar && m_annotationLayer) {
        bool canUndo = m_annotationLayer->canUndo() || canUndoCrop();
        bool canRedo = m_annotationLayer->canRedo() || canRedoCrop();
        m_toolbar->setCanUndo(canUndo);
        m_toolbar->setCanRedo(canRedo);
    }
}

bool PinWindow::isAnnotationTool(ToolId toolId) const
{
    return ToolTraits::isAnnotationTool(toolId);
}

QPixmap PinWindow::getExportPixmapWithAnnotations() const
{
    QPixmap result = getExportPixmap();
    if (result.isNull()) {
        return result;
    }

    // Draw annotations onto the exported pixmap
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    drawAnnotationsForExport(painter, logicalSizeFromPixmap(result));
    painter.end();

    return result;
}

QPixmap PinWindow::exportPixmapForMerge() const
{
    // Merge output is reopened in a PinWindow, so avoid baking display effects
    // (opacity/watermark) that the destination window can apply again.
    QPixmap result;
    if (isRegionLayoutMode() && m_regionLayoutManager) {
        qreal dpr = m_displayPixmap.devicePixelRatio();
        if (dpr <= 0.0) {
            dpr = 1.0;
        }
        result = m_regionLayoutManager->recomposeImage(dpr);
    }
    if (result.isNull()) {
        result = getExportPixmapCore(false);
    }
    if (result.isNull()) {
        return result;
    }

    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    drawAnnotationsForExport(painter, logicalSizeFromPixmap(result));
    painter.end();

    return result;
}

void PinWindow::updateSubToolbarPosition()
{
    // Only position if sub-toolbar is visible
    if (m_subToolbar && m_subToolbar->isVisible() && m_toolbar) {
        QRect toolbarGeom = m_toolbar->frameGeometry();
        m_subToolbar->positionBelow(toolbarGeom);
    }
}

void PinWindow::hideSubToolbar()
{
    if (m_subToolbar) {
        m_subToolbar->hide();
    }
}

QWidget* PinWindow::annotationHostWidget() const
{
    return const_cast<PinWindow*>(this);
}

AnnotationLayer* PinWindow::annotationLayerForContext() const
{
    return m_annotationLayer;
}

ToolOptionsPanel* PinWindow::toolOptionsPanelForContext() const
{
    return m_subToolbar ? m_subToolbar->colorAndWidthWidget() : nullptr;
}

InlineTextEditor* PinWindow::inlineTextEditorForContext() const
{
    return m_textEditor;
}

TextAnnotationEditor* PinWindow::textAnnotationEditorForContext() const
{
    return m_textAnnotationEditor;
}

void PinWindow::onContextColorSelected(const QColor& color)
{
    onColorSelected(color);
}

void PinWindow::onContextMoreColorsRequested()
{
    onMoreColorsRequested();
}

void PinWindow::onContextLineWidthChanged(int width)
{
    onWidthChanged(width);
}

void PinWindow::onContextArrowStyleChanged(LineEndStyle style)
{
    onArrowStyleChanged(style);
}

void PinWindow::onContextLineStyleChanged(LineStyle style)
{
    onLineStyleChanged(style);
}

void PinWindow::onContextFontSizeDropdownRequested(const QPoint& pos)
{
    onFontSizeDropdownRequested(pos);
}

void PinWindow::onContextFontFamilyDropdownRequested(const QPoint& pos)
{
    onFontFamilyDropdownRequested(pos);
}

void PinWindow::onContextTextEditingFinished(const QString& text, const QPoint& position)
{
    bool createdNew = m_textAnnotationEditor->finishEditing(text, position, m_annotationColor);
    if (createdNew) {
        applyTextOrientationCompensation(
            getSelectedTextAnnotation(),
            mapToOriginalCoords(QPointF(position)));
    }
    updateUndoRedoState();
    update();
}

void PinWindow::onContextTextEditingCancelled()
{
    if (m_textAnnotationEditor) {
        m_textAnnotationEditor->cancelEditing();
    }
    update();
}

void PinWindow::onColorSelected(const QColor& color)
{
    m_annotationColor = color;
    if (m_toolManager) {
        m_toolManager->setColor(color);
    }
    // Save to settings
    AnnotationSettingsManager::instance().saveColor(color);
}

void PinWindow::onWidthChanged(int width)
{
    m_annotationWidth = width;
    if (m_toolManager) {
        m_toolManager->setWidth(width);
    }
    // Update cursor if Mosaic tool is active
    if (m_annotationMode && m_currentToolId == ToolId::Mosaic) {
        updateCursorForTool();
    }
    // Save to settings
    AnnotationSettingsManager::instance().saveWidth(width);
}

void PinWindow::onEmojiSelected(const QString& emoji)
{
    // Set emoji in handler - user clicks canvas to place it (like RegionSelector)
    if (m_toolManager) {
        auto* handler = dynamic_cast<EmojiStickerToolHandler*>(
            m_toolManager->handler(ToolId::EmojiSticker));
        if (handler) {
            handler->setCurrentEmoji(emoji);
        }
    }
}

void PinWindow::onStepBadgeSizeChanged(StepBadgeSize size)
{
    m_stepBadgeSize = size;
    // Save to settings - StepBadgeToolHandler reads from AnnotationSettingsManager
    AnnotationSettingsManager::instance().saveStepBadgeSize(size);
}

void PinWindow::onShapeTypeChanged(ShapeType type)
{
    if (m_toolManager) {
        m_toolManager->setShapeType(static_cast<int>(type));
    }
}

void PinWindow::onShapeFillModeChanged(ShapeFillMode mode)
{
    if (m_toolManager) {
        m_toolManager->setShapeFillMode(static_cast<int>(mode));
    }
}

void PinWindow::onArrowStyleChanged(LineEndStyle style)
{
    if (m_toolManager) {
        m_toolManager->setArrowStyle(style);
    }
}

void PinWindow::onLineStyleChanged(LineStyle style)
{
    if (m_toolManager) {
        m_toolManager->setLineStyle(style);
    }
}

void PinWindow::onFontSizeDropdownRequested(const QPoint& pos)
{
    if (m_annotationContext && m_settingsHelper) {
        m_annotationContext->showTextFontSizeDropdown(*m_settingsHelper, pos);
    }
}

void PinWindow::onFontFamilyDropdownRequested(const QPoint& pos)
{
    if (m_annotationContext && m_settingsHelper) {
        m_annotationContext->showTextFontFamilyDropdown(*m_settingsHelper, pos);
    }
}

void PinWindow::onFontSizeSelected(int size)
{
    if (m_annotationContext) {
        m_annotationContext->applyTextFontSize(size);
    }
}

void PinWindow::onFontFamilySelected(const QString& family)
{
    if (m_annotationContext) {
        m_annotationContext->applyTextFontFamily(family);
    }
}

void PinWindow::onAutoBlurRequested()
{
    if (m_autoBlurInProgress) {
        return;
    }

    // Lazy initialize AutoBlurManager
    if (!m_autoBlurManager) {
        m_autoBlurManager = new AutoBlurManager(this);
    }

    // Initialize face detector (lazy load Haar cascade)
    if (!m_autoBlurManager->isInitialized()) {
        if (!m_autoBlurManager->initialize()) {
            return;  // Failed to initialize (cascade file not found)
        }
    }

    m_autoBlurInProgress = true;

    // Reload latest settings before detection
    m_autoBlurManager->setOptions(AutoBlurSettingsManager::instance().load());

    // Capture the exact source snapshot used for detection.
    // Annotation source and coordinate conversion must use the same snapshot.
    QPixmap detectionPixmap = m_displayPixmap;
    QImage image = detectionPixmap.toImage();
    qreal dpr = detectionPixmap.devicePixelRatio();

    // Run detection asynchronously to avoid blocking UI thread
    auto* watcher = new QFutureWatcher<AutoBlurManager::DetectionResult>(this);
    m_autoBlurWatcher = watcher;
    connect(watcher, &QFutureWatcher<AutoBlurManager::DetectionResult>::finished,
            this, [this, watcher, detectionPixmap, dpr]() {
        auto result = watcher->result();
        m_autoBlurWatcher = nullptr;
        watcher->deleteLater();
        m_autoBlurInProgress = false;

        if (!result.success || result.faceRegions.isEmpty()) {
            return;
        }

        // Use unified settings from AutoBlurSettingsManager
        const auto opts = m_autoBlurManager->options();
        int blockSize = AutoBlurManager::intensityToBlockSize(opts.blurIntensity);

        auto blurType = (opts.blurType == AutoBlurSettingsManager::BlurType::Gaussian)
            ? MosaicRectAnnotation::BlurType::Gaussian
            : MosaicRectAnnotation::BlurType::Pixelate;

        // Create shared pixmap for all face annotations using the same snapshot
        // that was analyzed by face detection.
        auto sharedDisplayPixmap = std::make_shared<const QPixmap>(detectionPixmap);

        for (const QRect& faceRect : result.faceRegions) {
            QRect logicalRect = CoordinateHelper::toLogical(faceRect, dpr);

            auto mosaic = std::make_unique<MosaicRectAnnotation>(
                logicalRect, sharedDisplayPixmap, blockSize, blurType
            );
            m_annotationLayer->addItem(std::move(mosaic));
        }

        updateUndoRedoState();
        update();
    });

    // detect() is thread-safe: FaceDetector::detect() only reads the image,
    // and cv::CascadeClassifier::detectMultiScale is const after initialization.
    AutoBlurManager* mgr = m_autoBlurManager;
    watcher->setFuture(QtConcurrent::run([mgr, image]() {
        return mgr->detect(image);
    }));
}

void PinWindow::onMoreColorsRequested()
{
    m_subToolbar->colorAndWidthWidget()->setCurrentColor(m_annotationColor);

    AnnotationContext::showColorPickerDialog(
        this,
        m_colorPickerDialog,
        m_annotationColor,
        frameGeometry().center(),
        [this](const QColor& color) {
            onColorSelected(color);
            if (m_subToolbar) {
                m_subToolbar->colorAndWidthWidget()->setCurrentColor(color);
            }
        });
}

// ============================================================================
// Text Annotation Helper Methods
// ============================================================================

TextBoxAnnotation* PinWindow::getSelectedTextAnnotation()
{
    if (!m_annotationLayer) return nullptr;
    int idx = m_annotationLayer->selectedIndex();
    if (idx < 0) return nullptr;
    return dynamic_cast<TextBoxAnnotation*>(m_annotationLayer->itemAt(idx));
}

EmojiStickerAnnotation* PinWindow::getSelectedEmojiStickerAnnotation()
{
    if (!m_annotationLayer) return nullptr;
    int idx = m_annotationLayer->selectedIndex();
    if (idx < 0) return nullptr;
    return dynamic_cast<EmojiStickerAnnotation*>(m_annotationLayer->itemAt(idx));
}

bool PinWindow::handleEmojiStickerAnnotationPress(const QPoint& pos)
{
    if (!m_annotationLayer) {
        return false;
    }

    auto& cursorManager = CursorManager::instance();
    QPoint mappedPos = mapToOriginalCoords(pos);

    // First prefer handles/body on an already-selected emoji.
    if (auto* emojiItem = getSelectedEmojiStickerAnnotation()) {
        GizmoHandle handle = TransformationGizmo::hitTest(emojiItem, mappedPos);
        if (handle != GizmoHandle::None) {
            if (handle == GizmoHandle::Body) {
                m_isEmojiDragging = true;
                m_emojiStartPos = mappedPos;
                cursorManager.setInputStateForWidget(this, InputState::Moving);
            } else {
                m_isEmojiScaling = true;
                m_emojiDragHandle = handle;
                m_emojiStartCenter = emojiItem->center();
                m_emojiStartScale = emojiItem->scale();
                QPointF delta = QPointF(mappedPos) - m_emojiStartCenter;
                m_emojiStartDistance = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
                cursorManager.setHoverTargetForWidget(
                    this,
                    HoverTarget::GizmoHandle,
                    static_cast<int>(handle));
            }
            update();
            return true;
        }
    }

    int hitIndex = m_annotationLayer->hitTestEmojiSticker(mappedPos);
    if (hitIndex < 0) {
        return false;
    }

    m_annotationLayer->setSelectedIndex(hitIndex);
    if (auto* emojiItem = getSelectedEmojiStickerAnnotation()) {
        GizmoHandle handle = TransformationGizmo::hitTest(emojiItem, mappedPos);
        if (handle == GizmoHandle::Body || handle == GizmoHandle::None) {
            m_isEmojiDragging = true;
            m_emojiStartPos = mappedPos;
            cursorManager.setInputStateForWidget(this, InputState::Moving);
        } else {
            m_isEmojiScaling = true;
            m_emojiDragHandle = handle;
            m_emojiStartCenter = emojiItem->center();
            m_emojiStartScale = emojiItem->scale();
            QPointF delta = QPointF(mappedPos) - m_emojiStartCenter;
            m_emojiStartDistance = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
            cursorManager.setHoverTargetForWidget(
                this,
                HoverTarget::GizmoHandle,
                static_cast<int>(handle));
        }
    }

    update();
    return true;
}

bool PinWindow::handleEmojiStickerAnnotationMove(const QPoint& pos)
{
    QPoint mappedPos = mapToOriginalCoords(pos);
    if ((!m_isEmojiDragging && !m_isEmojiScaling) || m_annotationLayer->selectedIndex() < 0) {
        return false;
    }

    auto* emojiItem = getSelectedEmojiStickerAnnotation();
    if (!emojiItem) {
        return false;
    }

    if (m_isEmojiDragging) {
        QPoint delta = mappedPos - m_emojiStartPos;
        emojiItem->moveBy(delta);
        m_emojiStartPos = mappedPos;
    } else if (m_emojiStartDistance > 0.0) {
        QPointF delta = QPointF(mappedPos) - m_emojiStartCenter;
        qreal currentDistance = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
        qreal scaleFactor = currentDistance / m_emojiStartDistance;
        emojiItem->setScale(m_emojiStartScale * scaleFactor);
    } else {
        return false;
    }

    m_annotationLayer->invalidateCache();
    update();
    return true;
}

bool PinWindow::handleEmojiStickerAnnotationRelease(const QPoint& pos)
{
    Q_UNUSED(pos);
    if (!m_isEmojiDragging && !m_isEmojiScaling) {
        return false;
    }

    m_isEmojiDragging = false;
    m_isEmojiScaling = false;
    m_emojiDragHandle = GizmoHandle::None;
    m_emojiStartDistance = 0.0;
    CursorManager::instance().setInputStateForWidget(this, InputState::Idle);
    update();
    return true;
}

bool PinWindow::handleTextEditorPress(const QPoint& pos)
{
    if (!m_textEditor || !m_textEditor->isEditing()) {
        return false;
    }

    if (m_textEditor->isConfirmMode()) {
        if (!m_textEditor->contains(pos)) {
            m_textEditor->finishEditing();
            return false;
        }
        m_textEditor->handleMousePress(pos);
        return true;
    }

    // In typing mode
    if (!m_textEditor->contains(pos)) {
        if (!m_textEditor->textEdit()->toPlainText().trimmed().isEmpty()) {
            m_textEditor->finishEditing();
        }
        else {
            m_textEditor->cancelEditing();
        }
        return false;
    }

    return true;
}

bool PinWindow::handleTextAnnotationPress(const QPoint& pos)
{
    if (!m_annotationLayer) return false;

    QPoint mappedPos = mapToOriginalCoords(pos);
    int hitIndex = m_annotationLayer->hitTestText(mappedPos);
    if (hitIndex < 0) return false;

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    // Double-click on ANY text annotation triggers re-edit (no need to be pre-selected)
    if (m_textAnnotationEditor->isDoubleClick(mappedPos, now)) {
        m_annotationLayer->setSelectedIndex(hitIndex);
        m_textAnnotationEditor->startReEditing(hitIndex, m_annotationColor);
        m_textAnnotationEditor->recordClick(QPoint(), 0);
        return true;
    }
    m_textAnnotationEditor->recordClick(mappedPos, now);

    // Single click: select and start dragging
    m_annotationLayer->setSelectedIndex(hitIndex);
    if (auto* textItem = getSelectedTextAnnotation()) {
        GizmoHandle handle = TransformationGizmo::hitTest(textItem, mappedPos);
        if (handle == GizmoHandle::Body || handle == GizmoHandle::None) {
            m_textAnnotationEditor->startDragging(mappedPos);
        }
        else {
            m_textAnnotationEditor->startTransformation(mappedPos, handle);
        }
    }

    update();
    return true;
}

bool PinWindow::handleGizmoPress(const QPoint& pos)
{
    auto* textItem = getSelectedTextAnnotation();
    if (!textItem) return false;

    QPoint mappedPos = mapToOriginalCoords(pos);
    GizmoHandle handle = TransformationGizmo::hitTest(textItem, mappedPos);
    if (handle == GizmoHandle::None) return false;

    if (handle == GizmoHandle::Body) {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (m_textAnnotationEditor->isDoubleClick(mappedPos, now)) {
            m_textAnnotationEditor->startReEditing(
                m_annotationLayer->selectedIndex(), m_annotationColor);
            m_textAnnotationEditor->recordClick(QPoint(), 0);
            return true;
        }
        m_textAnnotationEditor->recordClick(mappedPos, now);
        m_textAnnotationEditor->startDragging(mappedPos);
    }
    else {
        m_textAnnotationEditor->startTransformation(mappedPos, handle);
    }

    update();
    return true;
}

// ============================================================================
// Live Capture Mode
// ============================================================================

void PinWindow::setSourceRegion(const QRect& region, QScreen* screen)
{
    m_sourceRegion = region;
    m_sourceScreen = screen;
}

void PinWindow::startLiveCapture()
{
    QScreen* sourceScreen = m_sourceScreen.data();
    if (m_isLiveMode || m_sourceRegion.isEmpty() || !sourceScreen) {
        qWarning() << "Cannot start live capture: invalid state or no source region";
        return;
    }

    // Validate screen still exists
    if (!QGuiApplication::screens().contains(sourceScreen)) {
        qWarning() << "Source screen no longer available";
        return;
    }

    m_isLiveMode = true;

    // Create capture engine
    m_captureEngine = ICaptureEngine::createBestEngine(this);
    m_captureEngine->setRegion(m_sourceRegion, sourceScreen);
    m_captureEngine->setFrameRate(m_captureFrameRate);

    // Exclude self from capture to avoid infinite recursion
#ifdef Q_OS_MACOS
    m_captureEngine->setExcludedWindows({ winId() });
#endif
    setWindowExcludedFromCapture(this, true);

    if (!m_captureEngine->start()) {
        qWarning() << "Failed to start live capture engine";
        stopLiveCapture();
        return;
    }

    // Timer-driven polling
    m_captureTimer = new QTimer(this);
    connect(m_captureTimer, &QTimer::timeout, this, &PinWindow::updateLiveFrame);
    m_captureTimer->start(1000 / m_captureFrameRate);

    // Pulsing indicator animation timer
    m_liveIndicatorTimer = new QTimer(this);
    connect(m_liveIndicatorTimer, &QTimer::timeout, this, QOverload<>::of(&QWidget::update));
    m_liveIndicatorTimer->start(50);  // ~20fps for indicator animation
    update();
}

void PinWindow::stopLiveCapture()
{
    if (m_captureTimer) {
        m_captureTimer->stop();
        if (!m_isDestructing) {
            m_captureTimer->deleteLater();
        }
        m_captureTimer = nullptr;
    }

    if (m_liveIndicatorTimer) {
        m_liveIndicatorTimer->stop();
        if (!m_isDestructing) {
            m_liveIndicatorTimer->deleteLater();
        }
        m_liveIndicatorTimer = nullptr;
    }

    if (m_captureEngine) {
        m_captureEngine->stop();
        // During PinWindow destruction, QObject parent-child cleanup will delete this.
        if (!m_isDestructing) {
            m_captureEngine->deleteLater();
        }
        m_captureEngine = nullptr;
    }

    setWindowExcludedFromCapture(this, false);
    m_isLiveMode = false;
    m_livePaused = false;
    update();
}

void PinWindow::pauseLiveCapture()
{
    if (!m_isLiveMode || m_livePaused) return;

    m_livePaused = true;
    if (m_captureTimer) {
        m_captureTimer->stop();
    }
    update();
}

void PinWindow::resumeLiveCapture()
{
    if (!m_isLiveMode || !m_livePaused) return;

    m_livePaused = false;
    if (m_captureTimer) {
        m_captureTimer->start(1000 / m_captureFrameRate);
    }
    update();
}

void PinWindow::setLiveFrameRate(int fps)
{
    m_captureFrameRate = qBound(1, fps, 60);

    if (m_captureEngine) {
        m_captureEngine->setFrameRate(m_captureFrameRate);
    }

    if (m_captureTimer && m_captureTimer->isActive()) {
        m_captureTimer->setInterval(1000 / m_captureFrameRate);
    }
}

void PinWindow::updateLiveFrame()
{
    if (!m_captureEngine || !m_isLiveMode || m_livePaused) {
        return;
    }

    QScreen* sourceScreen = m_sourceScreen.data();
    if (!sourceScreen || !QGuiApplication::screens().contains(sourceScreen)) {
        qWarning() << "Source screen disconnected during live capture";
        stopLiveCapture();
        return;
    }

    QImage frame = m_captureEngine->captureFrame();
    if (!frame.isNull()) {
        m_originalPixmap = QPixmap::fromImage(frame);
        m_originalPixmap.setDevicePixelRatio(sourceScreen->devicePixelRatio());
        clearCropUndoHistory();

        // Update shared pixmap for mosaic tool to use latest frame
        m_sharedSourcePixmap = std::make_shared<const QPixmap>(m_originalPixmap);
        if (m_toolManager) {
            m_toolManager->setSourcePixmap(m_sharedSourcePixmap);
        }

        // Invalidate transform cache
        m_cachedRotation = -1;

        // Update display
        updateSize();
    }
}

// ============================================================================
// Arrow and Polyline Handling
// ============================================================================

ArrowAnnotation* PinWindow::getSelectedArrowAnnotation()
{
    if (!m_annotationLayer) return nullptr;
    int index = m_annotationLayer->selectedIndex();
    if (index >= 0) {
        return dynamic_cast<ArrowAnnotation*>(m_annotationLayer->itemAt(index));
    }
    return nullptr;
}

bool PinWindow::handleArrowAnnotationPress(const QPoint& pos)
{
    // Use mapped position (Original Coords)
    QPoint mappedPos = mapToOriginalCoords(pos);

    if (auto* arrowItem = getSelectedArrowAnnotation()) {
        GizmoHandle handle = TransformationGizmo::hitTest(arrowItem, mappedPos);
        if (handle != GizmoHandle::None) {
             m_isArrowDragging = true;
             m_arrowDragHandle = handle;
             m_annotationDragStartPos = mappedPos;
             // Set appropriate cursor based on handle if needed
             update(); 
             return true;
        }
    }

    // Check hit test for unselected items
    int hitIndex = m_annotationLayer->hitTestArrow(mappedPos);
    if (hitIndex >= 0) {
        m_annotationLayer->setSelectedIndex(hitIndex);
        m_isArrowDragging = true;
        m_arrowDragHandle = GizmoHandle::Body; // Default to body drag on selection
        m_annotationDragStartPos = mappedPos;
        
        // Refine potential handle hit
        if (auto* arrowItem = getSelectedArrowAnnotation()) {
             GizmoHandle handle = TransformationGizmo::hitTest(arrowItem, mappedPos);
             if (handle != GizmoHandle::None) {
                 m_arrowDragHandle = handle;
             }
        }
        
        update();
        return true;
    }
    return false;
}

bool PinWindow::handleArrowAnnotationMove(const QPoint& pos)
{
    // Use mapped position (Original Coords)
    QPoint mappedPos = mapToOriginalCoords(pos);

    if (m_isArrowDragging && m_annotationLayer->selectedIndex() >= 0) {
        auto* arrowItem = getSelectedArrowAnnotation();
        if (arrowItem) {
            if (m_arrowDragHandle == GizmoHandle::Body) {
                QPoint delta = mappedPos - m_annotationDragStartPos;
                arrowItem->moveBy(delta);
                m_annotationDragStartPos = mappedPos;
            } else if (m_arrowDragHandle == GizmoHandle::ArrowStart) {
                arrowItem->setStart(mappedPos);
            } else if (m_arrowDragHandle == GizmoHandle::ArrowEnd) {
                arrowItem->setEnd(mappedPos);
            } else if (m_arrowDragHandle == GizmoHandle::ArrowControl) {
                // User drags the curve midpoint (t=0.5), calculate actual Bézier control point
                // P1 = 2*Mid - 0.5*(Start + End)
                QPointF start = arrowItem->start();
                QPointF end = arrowItem->end();
                QPointF newControl = 2.0 * QPointF(mappedPos) - 0.5 * (start + end);
                arrowItem->setControlPoint(newControl.toPoint());
            }
            m_annotationLayer->invalidateCache();
            update();
            return true;
        }
    }
    return false;
}

bool PinWindow::handleArrowAnnotationRelease(const QPoint& pos)
{
    Q_UNUSED(pos);
    if (m_isArrowDragging) {
        m_isArrowDragging = false;
        m_arrowDragHandle = GizmoHandle::None;
        updateCursorForTool(); // Restore tool cursor
        update();
        return true;
    }
    return false;
}

PolylineAnnotation* PinWindow::getSelectedPolylineAnnotation()
{
    if (!m_annotationLayer) return nullptr;
    int index = m_annotationLayer->selectedIndex();
    if (index >= 0) {
        return dynamic_cast<PolylineAnnotation*>(m_annotationLayer->itemAt(index));
    }
    return nullptr;
}

bool PinWindow::handlePolylineAnnotationPress(const QPoint& pos)
{
    // Use mapped position (Original Coords)
    QPoint mappedPos = mapToOriginalCoords(pos);

    if (auto* polylineItem = getSelectedPolylineAnnotation()) {
        int vertexIndex = TransformationGizmo::hitTestVertex(polylineItem, mappedPos);
        if (vertexIndex >= 0) {
            // Vertex hit
            m_isPolylineDragging = true;
            m_activePolylineVertexIndex = vertexIndex;
            m_annotationDragStartPos = mappedPos;
            update();
            return true;
        } else if (vertexIndex == -1) {
             // Body hit
             m_isPolylineDragging = true;
             m_activePolylineVertexIndex = -1;
             m_annotationDragStartPos = mappedPos;
             update();
             return true;
        }
    }

    // Check hit test for unselected items
    int hitIndex = m_annotationLayer->hitTestPolyline(mappedPos);
    if (hitIndex >= 0) {
        m_annotationLayer->setSelectedIndex(hitIndex);
        m_isPolylineDragging = true;
        m_activePolylineVertexIndex = -1; // Default to body drag
        m_annotationDragStartPos = mappedPos;

        // Check if we actually hit a vertex though
        if (auto* polylineItem = getSelectedPolylineAnnotation()) {
             int vertexIndex = TransformationGizmo::hitTestVertex(polylineItem, mappedPos);
             if (vertexIndex >= 0) {
                 m_activePolylineVertexIndex = vertexIndex;
             }
        }
        
        update();
        return true;
    }
    return false;
}

bool PinWindow::handlePolylineAnnotationMove(const QPoint& pos)
{
    // Use mapped position (Original Coords)
    QPoint mappedPos = mapToOriginalCoords(pos);

    if (m_isPolylineDragging && m_annotationLayer->selectedIndex() >= 0) {
        auto* polylineItem = getSelectedPolylineAnnotation();
        if (polylineItem) {
             if (m_activePolylineVertexIndex >= 0) {
                 // Move specific vertex
                 polylineItem->setPoint(m_activePolylineVertexIndex, mappedPos);
             } else {
                 // Move entire polyline
                 QPoint delta = mappedPos - m_annotationDragStartPos;
                 polylineItem->moveBy(delta);
                 m_annotationDragStartPos = mappedPos;
             }
             m_annotationLayer->invalidateCache();
             update();
             return true;
        }
    }
    return false;
}

bool PinWindow::handlePolylineAnnotationRelease(const QPoint& pos)
{
    Q_UNUSED(pos);
    if (m_isPolylineDragging) {
        m_isPolylineDragging = false;
        m_activePolylineVertexIndex = -1;
        updateCursorForTool();
        update();
        return true;
    }
    return false;
}

void PinWindow::updateAnnotationCursor(const QPoint& pos)
{
    // Map to original coords for hit testing
    QPoint mappedPos = mapToOriginalCoords(pos);
    auto& cursorManager = CursorManager::instance();

    // Check selected text annotation's gizmo handles first (highest priority)
    if (auto* textItem = getSelectedTextAnnotation()) {
        GizmoHandle handle = TransformationGizmo::hitTest(textItem, mappedPos);
        if (handle != GizmoHandle::None) {
            cursorManager.setHoverTargetForWidget(this, HoverTarget::GizmoHandle, static_cast<int>(handle));
            return;
        }
    }

    // Use unified hit testing from CursorManager for other annotations
    auto result = CursorManager::hitTestAnnotations(
        mappedPos,
        m_annotationLayer,
        getSelectedEmojiStickerAnnotation(),
        getSelectedArrowAnnotation(),
        getSelectedPolylineAnnotation(),
        m_currentToolId != ToolId::EmojiSticker
    );

    if (result.hit) {
        cursorManager.setHoverTargetForWidget(this, result.target, result.handleIndex);
    } else {
        // No annotation hit - clear hover target to let tool cursor show
        cursorManager.setHoverTargetForWidget(this, HoverTarget::None);
    }
}

// ============================================================================
// Region Layout Mode
// ============================================================================

void PinWindow::setMultiRegionData(const QVector<LayoutRegion>& regions)
{
    m_storedRegions = regions;
    m_hasMultiRegionData = !regions.isEmpty() && regions.size() <= LayoutModeConstants::kMaxRegionCount;
}

void PinWindow::prepareForMerge()
{
    if (isRegionLayoutMode()) {
        exitRegionLayoutMode(false);
    }

    if (m_annotationMode) {
        exitAnnotationMode();
    }
}

void PinWindow::enterRegionLayoutMode()
{
    if (!m_hasMultiRegionData || m_isLiveMode) {
        return;
    }

    // Exit annotation mode if active
    if (m_annotationMode) {
        exitAnnotationMode();
    }

    // Hide toolbar if visible
    if (m_toolbarVisible) {
        hideToolbar();
    }

    // Create layout manager if needed
    if (!m_regionLayoutManager) {
        m_regionLayoutManager = new RegionLayoutManager(this);

        // Connect signals
        connect(m_regionLayoutManager, &RegionLayoutManager::layoutChanged,
                this, QOverload<>::of(&QWidget::update));
        connect(m_regionLayoutManager, &RegionLayoutManager::canvasSizeChanged,
                this, [this](const QSize& newSize) {
                    QSize logicalSize = newSize;
                    setFixedSize(logicalSize);
                });
    }

    // Enter layout mode with stored regions
    QSize canvasSize = logicalSizeFromPixmap(m_originalPixmap);
    m_regionLayoutManager->enterLayoutMode(m_storedRegions, canvasSize);

    // Bind annotations if annotation layer exists
    if (m_annotationLayer) {
        m_regionLayoutManager->bindAnnotations(m_annotationLayer);
    }

    update();
}

void PinWindow::exitRegionLayoutMode(bool apply)
{
    if (!m_regionLayoutManager || !m_regionLayoutManager->isActive()) {
        return;
    }

    if (apply) {
        // Update annotation positions before recomposing
        if (m_annotationLayer) {
            m_regionLayoutManager->updateAnnotationPositions();
        }

        // Recompose the image
        qreal dpr = m_originalPixmap.devicePixelRatio();
        QPixmap newPixmap = m_regionLayoutManager->recomposeImage(dpr);

        if (!newPixmap.isNull()) {
            // Calculate the bounds offset used in recomposition
            QRect bounds;
            for (const auto& region : m_regionLayoutManager->regions()) {
                if (bounds.isNull()) {
                    bounds = region.rect;
                } else {
                    bounds = bounds.united(region.rect);
                }
            }

            // Store regions with normalized coordinates (relative to new canvas origin)
            m_storedRegions = m_regionLayoutManager->regions();
            for (auto& region : m_storedRegions) {
                region.rect.translate(-bounds.topLeft());
                region.originalRect = region.rect;  // Reset original to match current
            }

            m_originalPixmap = newPixmap;
            m_displayPixmap = newPixmap;
            clearCropUndoHistory();

            // Invalidate transform cache
            m_cachedRotation = -1;
            m_cachedFlipH = false;
            m_cachedFlipV = false;

            // Update window size
            QSize logicalSize = logicalSizeFromPixmap(m_displayPixmap);
            setFixedSize(logicalSize);
            syncCropHandlerImageSize();

            // Invalidate annotation layer cache
            if (m_annotationLayer) {
                m_annotationLayer->invalidateCache();
            }

            // Save to cache
            saveToCacheAsync();
        }
    } else {
        // Cancel - restore annotations
        if (m_annotationLayer) {
            m_regionLayoutManager->restoreAnnotations(m_annotationLayer);
        }

        // Layout mode can temporarily resize the window via canvasSizeChanged.
        // On cancel, restore the pre-layout display size.
        const QSize restoredSize = logicalSizeFromPixmap(m_displayPixmap);
        if (restoredSize.isValid() && !restoredSize.isEmpty()) {
            setFixedSize(restoredSize);
        }
    }

    m_regionLayoutManager->exitLayoutMode(apply);
    update();
}

bool PinWindow::isRegionLayoutMode() const
{
    return m_regionLayoutManager && m_regionLayoutManager->isActive();
}

// ============================================================================
// Beautify
// ============================================================================

void PinWindow::showBeautifyPanel()
{
    if (!m_beautifyPanel) {
        m_beautifyPanel = std::make_unique<BeautifyPanel>(nullptr);
        m_beautifyPanel->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        m_beautifyPanel->setAttribute(Qt::WA_DeleteOnClose, false);

        connect(m_beautifyPanel.get(), &BeautifyPanel::copyRequested,
                this, &PinWindow::onBeautifyCopy);
        connect(m_beautifyPanel.get(), &BeautifyPanel::saveRequested,
                this, &PinWindow::onBeautifySave);
        connect(m_beautifyPanel.get(), &BeautifyPanel::closeRequested,
                m_beautifyPanel.get(), &QWidget::hide);
    }

    m_beautifyPanel->setSourcePixmap(getExportPixmapWithAnnotations());
    m_beautifyPanel->setSettings(BeautifySettingsManager::instance().loadSettings());

    // Position panel next to the pin window
    QPoint panelPos = mapToGlobal(QPoint(width() + 8, 0));
    m_beautifyPanel->move(panelPos);
    m_beautifyPanel->show();
    m_beautifyPanel->raise();
}

void PinWindow::onBeautifyCopy(const BeautifySettings& settings)
{
    BeautifySettingsManager::instance().saveSettings(settings);
    QPixmap source = getExportPixmapWithAnnotations();
    if (source.isNull()) {
        m_uiIndicators->showToast(false, tr("Copy failed"));
        return;
    }
    QPixmap result = BeautifyRenderer::applyToPixmap(source, settings);
    if (result.isNull()) {
        m_uiIndicators->showToast(false, tr("Beautify rendering failed"));
        return;
    }
    QGuiApplication::clipboard()->setPixmap(result);
    m_uiIndicators->showToast(true, tr("Beautified image copied"));
}

void PinWindow::onBeautifySave(const BeautifySettings& settings)
{
    BeautifySettingsManager::instance().saveSettings(settings);
    QPixmap source = getExportPixmapWithAnnotations();
    if (source.isNull()) {
        emit saveFailed(QString(), tr("No image available to save"));
        return;
    }
    QPixmap result = BeautifyRenderer::applyToPixmap(source, settings);
    if (result.isNull()) {
        emit saveFailed(QString(), tr("Beautify rendering failed"));
        return;
    }

    auto& fileSettings = FileSettingsManager::instance();
    QString savePath = fileSettings.loadScreenshotPath();

    FilenameTemplateEngine::Context context;
    context.type = QStringLiteral("Screenshot");
    context.prefix = fileSettings.loadFilenamePrefix();
    context.width = result.width();
    context.height = result.height();
    context.ext = QStringLiteral("png");
    context.dateFormat = fileSettings.loadDateFormat();
    context.outputDir = savePath;
    const QString templateValue = fileSettings.loadFilenameTemplate();

    if (fileSettings.loadAutoSaveScreenshots()) {
        QString renderError;
        QString filePath = FilenameTemplateEngine::buildUniqueFilePath(
            savePath, templateValue, context, kMaxFileCollisionRetries, &renderError);
        if (!renderError.isEmpty()) {
            qWarning() << "PinWindow: beautify template warning:" << renderError;
        }
        ImageSaveUtils::Error saveError;
        if (ImageSaveUtils::savePixmapAtomically(result, filePath, QByteArray(), &saveError)) {
            emit saveCompleted(result, filePath);
        } else {
            emit saveFailed(
                filePath,
                tr("Failed to save beautified screenshot: %1").arg(saveErrorDetail(saveError)));
        }
    } else {
        QString defaultName = FilenameTemplateEngine::buildUniqueFilePath(
            savePath, templateValue, context, 1);
        QString filePath = QFileDialog::getSaveFileName(
            this, tr("Save Beautified Screenshot"), defaultName,
            tr("PNG Image (*.png);;JPEG Image (*.jpg *.jpeg);;All Files (*)"));
        if (!filePath.isEmpty()) {
            ImageSaveUtils::Error saveError;
            if (ImageSaveUtils::savePixmapAtomically(result, filePath, QByteArray(), &saveError)) {
                emit saveCompleted(result, filePath);
            } else {
                emit saveFailed(
                    filePath,
                    tr("Failed to save beautified screenshot: %1").arg(saveErrorDetail(saveError)));
            }
        }
    }
}
