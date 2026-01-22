#include "PinWindow.h"
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
#include "toolbar/WindowedToolbar.h"
#include "toolbar/WindowedSubToolbar.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/EmojiStickerAnnotation.h"
#include "annotations/MosaicRectAnnotation.h"
#include "detection/AutoBlurManager.h"
#include "tools/ToolManager.h"
#include "tools/IToolHandler.h"
#include "tools/handlers/EmojiStickerToolHandler.h"
#include "settings/AnnotationSettingsManager.h"
#include "settings/FileSettingsManager.h"
#include "settings/PinWindowSettingsManager.h"
#include "settings/OCRSettingsManager.h"
#include "OCRResultDialog.h"
#include "InlineTextEditor.h"
#include "region/TextAnnotationEditor.h"
#include "toolbar/ToolOptionsPanel.h"
#include "annotations/TextBoxAnnotation.h"
#include "TransformationGizmo.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/PolylineAnnotation.h"
#include "utils/CoordinateHelper.h"



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
#include <QStandardPaths>
#include <QDateTime>
#include <QToolTip>
#include <QTextEdit>
#include <QLabel>
#include <QTimer>
#include <QPointer>
#include <QTransform>
#include <QDebug>
#include <QActionGroup>
#include <QDesktopServices>
#include <QUrl>
#include <QThreadPool>

namespace {
    // Full opacity constant for pixel alpha check
    constexpr int kPixelFullOpacity = 255;
    
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
}  // namespace

PinWindow::PinWindow(const QPixmap& screenshot, const QPoint& position, QWidget* parent)
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
    QSize logicalSize = m_displayPixmap.size() / m_displayPixmap.devicePixelRatio();
    setFixedSize(logicalSize);

    // Cache the base corner radius so pin window chrome matches rounded captures.
    qreal baseDpr = m_originalPixmap.devicePixelRatio();
    if (baseDpr <= 0.0) {
        baseDpr = 1.0;
    }
    QSize baseLogicalSize = m_originalPixmap.size() / baseDpr;
    int maxRadius = qMin(baseLogicalSize.width(), baseLogicalSize.height()) / 2;
    m_baseCornerRadius = qMin(AnnotationSettingsManager::instance().loadCornerRadius(), maxRadius);
    if (m_baseCornerRadius > 0 && !hasTransparentCornerPixels(m_originalPixmap)) {
        m_baseCornerRadius = 0;
    }

    // Load watermark settings (used by context menu when created lazily)
    m_watermarkSettings = WatermarkSettingsManager::instance().load();

    // Context menu is now created lazily in contextMenuEvent() to improve initial performance

    // Auto-save to cache folder asynchronously
    saveToCacheAsync();

    // OCR manager is now created lazily in performOCR() to improve initial performance

    // Enable mouse tracking for resize cursor
    setMouseTracking(true);

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
    // Clean up cursor state before destruction (ensures Drag context is cleared)
    CursorManager::instance().clearAllForWidget(this);

    // Stop live capture if active
    if (m_isLiveMode) {
        stopLiveCapture();
    }

    // Clean up floating windows (not parented to this window)
    if (m_toolbar) {
        m_toolbar->close();
        delete m_toolbar;
        m_toolbar = nullptr;
    }
    if (m_subToolbar) {
        m_subToolbar->close();
        delete m_subToolbar;
        m_subToolbar = nullptr;
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
}

void PinWindow::setOpacity(qreal opacity)
{
    m_opacity = qBound(0.1, opacity, 1.0);
    setWindowOpacity(m_opacity);
}

void PinWindow::rotateRight()
{
    m_rotationAngle = (m_rotationAngle + 90) % 360;
    updateSize();
}

void PinWindow::rotateLeft()
{
    m_rotationAngle = (m_rotationAngle + 270) % 360;  // +270 is same as -90
    updateSize();
}

void PinWindow::flipHorizontal()
{
    m_flipHorizontal = !m_flipHorizontal;
    updateSize();
}

void PinWindow::flipVertical()
{
    m_flipVertical = !m_flipVertical;
    updateSize();
}

QPoint PinWindow::mapToOriginalCoords(const QPoint& displayPos) const
{
    // No transformation needed if no rotation or flip applied
    if (m_rotationAngle == 0 && !m_flipHorizontal && !m_flipVertical) {
        return displayPos;
    }

    // Build the same forward transform used in paintEvent for annotations
    // Then invert it to map display coordinates back to original coordinates
    QRectF pixmapRect = rect();

    QTransform transform;

    // Step 1: Apply rotation with proper translation (same as paintEvent)
    if (m_rotationAngle == 90) {
        transform.translate(pixmapRect.width(), 0);
        transform.rotate(90);
    }
    else if (m_rotationAngle == 180) {
        transform.translate(pixmapRect.width(), pixmapRect.height());
        transform.rotate(180);
    }
    else if (m_rotationAngle == 270) {
        transform.translate(0, pixmapRect.height());
        transform.rotate(270);
    }

    // Step 2: Apply flip around the center (same as paintEvent)
    if (m_flipHorizontal || m_flipVertical) {
        QPointF center(pixmapRect.width() / 2.0, pixmapRect.height() / 2.0);
        transform.translate(center.x(), center.y());
        if (m_flipHorizontal) transform.scale(-1, 1);
        if (m_flipVertical) transform.scale(1, -1);
        transform.translate(-center.x(), -center.y());
    }

    // Invert the transform to map from display coords to original coords
    QTransform inverse = transform.inverted();
    return inverse.map(displayPos);
}

void PinWindow::setWatermarkSettings(const WatermarkRenderer::Settings& settings)
{
    m_watermarkSettings = settings;
    update();
}

void PinWindow::ensureTransformCacheValid()
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

        QSize newDeviceSize = size() * m_transformedCache.devicePixelRatio();

        m_displayPixmap = m_transformedCache.scaled(newDeviceSize,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation);
        m_displayPixmap.setDevicePixelRatio(m_transformedCache.devicePixelRatio());

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
        qreal dpr = m_transformedCache.devicePixelRatio();
        if (dpr <= 0.0) {
            dpr = 1.0;
        }
        baseLogicalSize = m_transformedCache.size() / dpr;
    }
    else {
        qreal dpr = m_originalPixmap.devicePixelRatio();
        if (dpr <= 0.0) {
            dpr = 1.0;
        }
        baseLogicalSize = m_originalPixmap.size() / dpr;
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
    QSize transformedLogicalSize = m_transformedCache.size() / m_transformedCache.devicePixelRatio();
    QSize newLogicalSize = transformedLogicalSize * m_zoomLevel;

    // Scale to device pixels for the actual pixmap
    QSize newDeviceSize = newLogicalSize * m_transformedCache.devicePixelRatio();
    Qt::TransformationMode mode = m_smoothing ? Qt::SmoothTransformation : Qt::FastTransformation;
    m_displayPixmap = m_transformedCache.scaled(newDeviceSize,
        Qt::KeepAspectRatio,
        mode);
    m_displayPixmap.setDevicePixelRatio(m_transformedCache.devicePixelRatio());

    setFixedSize(newLogicalSize);
    update();
}

QPixmap PinWindow::getTransformedPixmap() const
{
    if (m_rotationAngle == 0 && !m_flipHorizontal && !m_flipVertical) {
        return m_originalPixmap;
    }

    QTransform transform;

    if (m_rotationAngle != 0) {
        transform.rotate(m_rotationAngle);
    }

    if (m_flipHorizontal || m_flipVertical) {
        qreal scaleX = m_flipHorizontal ? -1.0 : 1.0;
        qreal scaleY = m_flipVertical ? -1.0 : 1.0;
        transform.scale(scaleX, scaleY);
    }

    QPixmap transformed = m_originalPixmap.transformed(transform, Qt::SmoothTransformation);
    transformed.setDevicePixelRatio(m_originalPixmap.devicePixelRatio());
    return transformed;
}

QPixmap PinWindow::getExportPixmap() const
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

void PinWindow::createContextMenu()
{
    m_contextMenu = new QMenu(this);

    // Show Toolbar action
    m_showToolbarAction = m_contextMenu->addAction("Show Toolbar");
    m_showToolbarAction->setShortcut(QKeySequence(Qt::Key_Space));
    m_showToolbarAction->setCheckable(true);
    connect(m_showToolbarAction, &QAction::triggered, this, &PinWindow::toggleToolbar);

    // Show border option (placed right after Show Toolbar)
    m_showBorderAction = m_contextMenu->addAction("Show Border");
    m_showBorderAction->setCheckable(true);
    m_showBorderAction->setChecked(m_showBorder);
    connect(m_showBorderAction, &QAction::toggled, this, &PinWindow::setShowBorder);

    m_contextMenu->addSeparator();

    QAction* copyAction = m_contextMenu->addAction("Copy to Clipboard");
    copyAction->setShortcut(QKeySequence::Copy);
    connect(copyAction, &QAction::triggered, this, &PinWindow::copyToClipboard);

    QAction* saveAction = m_contextMenu->addAction("Save to file");
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &PinWindow::saveToFile);

    QAction* openCacheFolderAction = m_contextMenu->addAction(tr("Open Cache Folder"));
    connect(openCacheFolderAction, &QAction::triggered, this, &PinWindow::openCacheFolder);

    if (PlatformFeatures::instance().isOCRAvailable()) {
        QAction* ocrAction = m_contextMenu->addAction("OCR Text Recognition");
        connect(ocrAction, &QAction::triggered, this, &PinWindow::performOCR);
    }

    m_contextMenu->addSeparator();

    // Watermark submenu
    QMenu* watermarkMenu = m_contextMenu->addMenu("Watermark");

    // Enable watermark action
    QAction* enableWatermarkAction = watermarkMenu->addAction("Enable");
    enableWatermarkAction->setCheckable(true);
    enableWatermarkAction->setChecked(m_watermarkSettings.enabled);
    connect(enableWatermarkAction, &QAction::toggled, this, [this](bool checked) {
        m_watermarkSettings.enabled = checked;
        update();
        });

    watermarkMenu->addSeparator();

    // Position submenu
    QMenu* positionMenu = watermarkMenu->addMenu("Position");
    QActionGroup* positionGroup = new QActionGroup(this);

    QAction* topLeftAction = positionMenu->addAction("Top-Left");
    topLeftAction->setCheckable(true);
    topLeftAction->setData(static_cast<int>(WatermarkRenderer::TopLeft));
    positionGroup->addAction(topLeftAction);

    QAction* topRightAction = positionMenu->addAction("Top-Right");
    topRightAction->setCheckable(true);
    topRightAction->setData(static_cast<int>(WatermarkRenderer::TopRight));
    positionGroup->addAction(topRightAction);

    QAction* bottomLeftAction = positionMenu->addAction("Bottom-Left");
    bottomLeftAction->setCheckable(true);
    bottomLeftAction->setData(static_cast<int>(WatermarkRenderer::BottomLeft));
    positionGroup->addAction(bottomLeftAction);

    QAction* bottomRightAction = positionMenu->addAction("Bottom-Right");
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
    QMenu* zoomMenu = m_contextMenu->addMenu("Zoom");

    // Preset zoom levels
    QAction* zoom33Action = zoomMenu->addAction("33.3%");
    connect(zoom33Action, &QAction::triggered, this, [this]() { setZoomLevel(1.0 / 3.0); });

    QAction* zoom50Action = zoomMenu->addAction("50%");
    connect(zoom50Action, &QAction::triggered, this, [this]() { setZoomLevel(0.5); });

    QAction* zoom67Action = zoomMenu->addAction("66.7%");
    connect(zoom67Action, &QAction::triggered, this, [this]() { setZoomLevel(2.0 / 3.0); });

    QAction* zoom100Action = zoomMenu->addAction("100%");
    connect(zoom100Action, &QAction::triggered, this, [this]() { setZoomLevel(1.0); });

    QAction* zoom200Action = zoomMenu->addAction("200%");
    connect(zoom200Action, &QAction::triggered, this, [this]() { setZoomLevel(2.0); });

    zoomMenu->addSeparator();

    // Current zoom level display (disabled, for display only)
    m_currentZoomAction = zoomMenu->addAction(QString("%1%").arg(qRound(m_zoomLevel * 100)));
    m_currentZoomAction->setEnabled(false);

    // Smoothing option
    QAction* smoothingAction = zoomMenu->addAction("Smoothing");
    smoothingAction->setCheckable(true);
    smoothingAction->setChecked(m_smoothing);
    connect(smoothingAction, &QAction::toggled, this, [this](bool checked) {
        m_smoothing = checked;
        updateSize();
        });

    // Image processing submenu
    QMenu* imageProcessingMenu = m_contextMenu->addMenu("Image processing");

    QAction* rotateLeftAction = imageProcessingMenu->addAction("Rotate left");
    connect(rotateLeftAction, &QAction::triggered, this, &PinWindow::rotateLeft);

    QAction* rotateRightAction = imageProcessingMenu->addAction("Rotate right");
    connect(rotateRightAction, &QAction::triggered, this, &PinWindow::rotateRight);

    QAction* horizontalFlipAction = imageProcessingMenu->addAction("Horizontal flip");
    connect(horizontalFlipAction, &QAction::triggered, this, &PinWindow::flipHorizontal);

    QAction* verticalFlipAction = imageProcessingMenu->addAction("Vertical flip");
    connect(verticalFlipAction, &QAction::triggered, this, &PinWindow::flipVertical);

    // Info submenu - displays image properties
    QMenu* infoMenu = m_contextMenu->addMenu(
        QString("%1 × %2").arg(m_originalPixmap.width()).arg(m_originalPixmap.height()));

    // Copy all info action
    QAction* copyAllInfoAction = infoMenu->addAction("Copy All");
    connect(copyAllInfoAction, &QAction::triggered, this, &PinWindow::copyAllInfo);

    infoMenu->addSeparator();

    // Info items - clicking copies that value
    auto addInfoItem = [infoMenu](const QString& label, const QString& value) {
        QAction* action = infoMenu->addAction(QString("%1: %2").arg(label, value));
        QObject::connect(action, &QAction::triggered, [value]() {
            QGuiApplication::clipboard()->setText(value);
            });
        };

    addInfoItem("Size", QString("%1 × %2").arg(m_originalPixmap.width()).arg(m_originalPixmap.height()));
    addInfoItem("Zoom", QString("%1%").arg(qRound(m_zoomLevel * 100)));
    addInfoItem("Rotation", QString::fromUtf8("%1°").arg(m_rotationAngle));
    addInfoItem("Opacity", QString("%1%").arg(qRound(m_opacity * 100)));
    addInfoItem("X-mirror", m_flipHorizontal ? "Yes" : "No");
    addInfoItem("Y-mirror", m_flipVertical ? "Yes" : "No");

    // Click-through option
    m_clickThroughAction = m_contextMenu->addAction("Click-through");
    m_clickThroughAction->setCheckable(true);
    m_clickThroughAction->setChecked(m_clickThrough);
    connect(m_clickThroughAction, &QAction::toggled, this, &PinWindow::setClickThrough);

    // Live capture section - actions are updated dynamically in contextMenuEvent
    m_contextMenu->addSeparator();

    // Start/Stop Live action
    m_startLiveAction = m_contextMenu->addAction("Start Live Update");
    connect(m_startLiveAction, &QAction::triggered, this, [this]() {
        if (m_isLiveMode) {
            stopLiveCapture();
        }
        else {
            startLiveCapture();
        }
        });

    // Pause/Resume Live action
    m_pauseLiveAction = m_contextMenu->addAction("Pause Live Update");
    connect(m_pauseLiveAction, &QAction::triggered, this, [this]() {
        if (m_livePaused) {
            resumeLiveCapture();
        }
        else {
            pauseLiveCapture();
        }
        });

    // Frame rate submenu
    m_fpsMenu = m_contextMenu->addMenu("Frame Rate");
    QActionGroup* fpsGroup = new QActionGroup(this);
    for (int fps : {5, 10, 15, 30}) {
        QAction* fpsAction = m_fpsMenu->addAction(QString("%1 FPS").arg(fps));
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

    QAction* closeAction = m_contextMenu->addAction("Close");
    closeAction->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(closeAction, &QAction::triggered, this, &PinWindow::close);

    QAction* closeAllPinsAction = m_contextMenu->addAction("Close All Pins");
    connect(closeAllPinsAction, &QAction::triggered, this, [this]() {
        if (m_pinWindowManager) {
            m_pinWindowManager->closeAllWindows();
        }
        });
}

void PinWindow::saveToFile()
{
    // Use FileSettingsManager for consistent path and filename format
    auto& fileSettings = FileSettingsManager::instance();
    QString savePath = fileSettings.loadScreenshotPath();
    QString dateFormat = fileSettings.loadDateFormat();
    QString prefix = fileSettings.loadFilenamePrefix();
    QString timestamp = QDateTime::currentDateTime().toString(dateFormat);

    QString filename;
    if (prefix.isEmpty()) {
        filename = QString("Screenshot_%1.png").arg(timestamp);
    }
    else {
        filename = QString("%1_Screenshot_%2.png").arg(prefix).arg(timestamp);
    }

    QPixmap pixmapToSave = getExportPixmapWithAnnotations();

    // Check auto-save setting
    if (fileSettings.loadAutoSaveScreenshots()) {
        QString filePath = QDir(savePath).filePath(filename);

        // Handle file collision (add counter if file exists)
        if (QFile::exists(filePath)) {
            QString baseName = QFileInfo(filePath).completeBaseName();
            QString ext = QFileInfo(filePath).suffix();
            int counter = 1;
            while (QFile::exists(filePath) && counter < kMaxFileCollisionRetries) {
                filePath = QDir(savePath).filePath(QString("%1_%2.%3").arg(baseName).arg(counter).arg(ext));
                counter++;
            }
        }

        if (pixmapToSave.save(filePath)) {
            emit saveCompleted(pixmapToSave, filePath);
        }
        else {
            emit saveFailed(filePath, tr("Failed to save screenshot"));
        }
        return;
    }

    // Show save dialog
    QString defaultName = QDir(savePath).filePath(filename);
    QString filePath = QFileDialog::getSaveFileName(
        this,
        "Save Screenshot",
        defaultName,
        "PNG Image (*.png);;JPEG Image (*.jpg *.jpeg);;All Files (*)"
    );

    if (!filePath.isEmpty()) {
        if (pixmapToSave.save(filePath)) {
            emit saveRequested(pixmapToSave);
        }
    }
}

void PinWindow::copyToClipboard()
{
    QPixmap pixmapToCopy = getExportPixmapWithAnnotations();
    QGuiApplication::clipboard()->setPixmap(pixmapToCopy);
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
        [safeThis](bool success, const QString& text, const QString& error) {
            if (safeThis) {
                safeThis->onOCRComplete(success, text, error);
            }
        });
}

void PinWindow::onOCRComplete(bool success, const QString& text, const QString& error)
{
    m_ocrInProgress = false;
    m_loadingSpinner->stop();

    if (success && !text.isEmpty()) {
        // Check settings to determine behavior
        if (OCRSettingsManager::instance().behavior() == OCRBehavior::ShowEditor) {
            showOCRResultDialog(text);
            return;
        }

        // Default behavior: direct copy
        QGuiApplication::clipboard()->setText(text);
        QString msg = tr("Copied %1 characters").arg(text.length());
        m_uiIndicators->showOCRToast(true, msg);
    }
    else {
        QString msg = error.isEmpty() ? tr("No text found") : error;
        m_uiIndicators->showOCRToast(false, msg);
    }

    emit ocrCompleted(success && !text.isEmpty(),
                      success ? tr("Text copied") : error);
}

void PinWindow::showOCRResultDialog(const QString& text)
{
    // Create non-modal dialog
    auto *dialog = new OCRResultDialog(this);
    dialog->setResultText(text);

    // Connect signals
    connect(dialog, &OCRResultDialog::textCopied, this, [this](const QString &copiedText) {
        qDebug() << "OCR text copied:" << copiedText.length() << "characters";

        // Show success toast
        m_uiIndicators->showOCRToast(true, tr("Copied %1 characters").arg(copiedText.length()));
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
            m_uiIndicators->showOCRToast(true, msg);
        });

        // Show dialog centered on screen
        dialog->showAt();
    }
    else {
        // Show error toast
        QString msg = error.isEmpty() ? tr("No QR code found") : error;
        m_uiIndicators->showOCRToast(false, msg);
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
    QStringList info;
    info << QString("Size: %1 × %2").arg(m_originalPixmap.width()).arg(m_originalPixmap.height());
    info << QString("Zoom: %1%").arg(qRound(m_zoomLevel * 100));
    info << QString::fromUtf8("Rotation: %1°").arg(m_rotationAngle);
    info << QString("Opacity: %1%").arg(qRound(m_opacity * 100));
    info << QString("X-mirror: %1").arg(m_flipHorizontal ? "Yes" : "No");
    info << QString("Y-mirror: %1").arg(m_flipVertical ? "Yes" : "No");

    QGuiApplication::clipboard()->setText(info.join("\n"));
}

QString PinWindow::cacheFolderPath()
{
    QString basePath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(basePath).filePath("pinwindow_history");
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
    int maxCacheFiles = PinWindowSettingsManager::instance().loadMaxCacheFiles();

    QThreadPool::globalInstance()->start([pixmapToSave, maxCacheFiles]() {
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

        if (pixmapToSave.save(filePath)) {
            // Clean up old cache files exceeding the limit
            QStringList filters;
            filters << "cache_*.png";
            QFileInfoList files = dir.entryInfoList(filters, QDir::Files, QDir::Time);

            // QDir::Time sorts by modification time, newest first
            while (files.size() > maxCacheFiles) {
                const QFileInfo& oldest = files.takeLast();
                QFile::remove(oldest.filePath());
            }
        }
        else {
            qWarning() << "PinWindow: Failed to save to cache:" << filePath;
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

    // Draw the screenshot
    painter.drawPixmap(pixmapRect, m_displayPixmap);

    // Draw annotations with the same rotation/flip transform as the pixmap
    if (m_annotationLayer && !m_annotationLayer->isEmpty()) {
        painter.save();

        // Apply the same rotation/flip transform used for the pixmap
        // QPixmap::transformed() with rotate() rotates around origin (0,0),
        // so we need to match that transformation exactly
        if (m_rotationAngle != 0 || m_flipHorizontal || m_flipVertical) {
            // Get original (unrotated) dimensions - annotations are stored in this space
            qreal origW = pixmapRect.width();
            qreal origH = pixmapRect.height();
            if (m_rotationAngle == 90 || m_rotationAngle == 270) {
                // Current dimensions are swapped, so swap back to get original
                std::swap(origW, origH);
            }

            // Build transform matching QPixmap::transformed() behavior
            // The transform order is: rotate first, then flip (matching ensureTransformCacheValid)
            QTransform transform;

            // Step 1: Apply rotation with proper translation to keep in positive coords
            if (m_rotationAngle == 90) {
                transform.translate(pixmapRect.width(), 0);
                transform.rotate(90);
            }
            else if (m_rotationAngle == 180) {
                transform.translate(pixmapRect.width(), pixmapRect.height());
                transform.rotate(180);
            }
            else if (m_rotationAngle == 270) {
                transform.translate(0, pixmapRect.height());
                transform.rotate(270);
            }

            // Step 2: Apply flip around the center of current (post-rotation) rect
            if (m_flipHorizontal || m_flipVertical) {
                QPointF center(pixmapRect.width() / 2.0, pixmapRect.height() / 2.0);
                transform.translate(center.x(), center.y());
                if (m_flipHorizontal) transform.scale(-1, 1);
                if (m_flipVertical) transform.scale(1, -1);
                transform.translate(-center.x(), -center.y());
            }

            painter.setTransform(transform, true);
        }

        m_annotationLayer->draw(painter);

        // Draw transformation gizmo for selected text annotation
        if (auto* textItem = getSelectedTextAnnotation()) {
            TransformationGizmo::draw(painter, textItem);
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
            QTransform transform;

            // Step 1: Apply rotation with proper translation to keep in positive coords
            if (m_rotationAngle == 90) {
                transform.translate(pixmapRect.width(), 0);
                transform.rotate(90);
            }
            else if (m_rotationAngle == 180) {
                transform.translate(pixmapRect.width(), pixmapRect.height());
                transform.rotate(180);
            }
            else if (m_rotationAngle == 270) {
                transform.translate(0, pixmapRect.height());
                transform.rotate(270);
            }

            // Step 2: Apply flip around the center of current (post-rotation) rect
            if (m_flipHorizontal || m_flipVertical) {
                QPointF center(pixmapRect.width() / 2.0, pixmapRect.height() / 2.0);
                transform.translate(center.x(), center.y());
                if (m_flipHorizontal) transform.scale(-1, 1);
                if (m_flipVertical) transform.scale(1, -1);
                transform.translate(-center.x(), -center.y());
            }

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

        // 4. Clear text annotation selection if clicking elsewhere in annotation mode
        if (m_annotationMode && m_annotationLayer && m_annotationLayer->selectedIndex() >= 0) {
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
    // Handle confirm mode text dragging
    if (m_textEditor && m_textEditor->isConfirmMode() && m_textEditor->isDragging()) {
        m_textEditor->handleMouseMove(event->pos());
        update();
        return;
    }

    // Handle TextAnnotationEditor transformation (rotate/scale)
    if (m_textAnnotationEditor && m_textAnnotationEditor->isTransforming()) {
        m_textAnnotationEditor->updateTransformation(event->pos());
        update();
        return;
    }

    // Handle TextAnnotationEditor dragging
    if (m_textAnnotationEditor && m_textAnnotationEditor->isDragging()) {
        m_textAnnotationEditor->updateDragging(event->pos());
        update();
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
        QSize transformedLogicalSize = m_transformedCache.size() / m_transformedCache.devicePixelRatio();
        qreal scaleX = static_cast<qreal>(newSize.width()) / transformedLogicalSize.width();
        qreal scaleY = static_cast<qreal>(newSize.height()) / transformedLogicalSize.height();
        m_zoomLevel = qMin(scaleX, scaleY);

        // Use FastTransformation during resize for responsiveness
        // High-quality scaling will be done after resize is complete
        QSize newDeviceSize = newSize * m_transformedCache.devicePixelRatio();
        m_displayPixmap = m_transformedCache.scaled(newDeviceSize,
            Qt::KeepAspectRatio,
            Qt::FastTransformation);
        m_displayPixmap.setDevicePixelRatio(m_transformedCache.devicePixelRatio());

        // Schedule high-quality update after resize ends
        m_pendingHighQualityUpdate = true;
        m_resizeFinishTimer->start(150);  // Resize debounce

        // Apply new size and position
        setFixedSize(newSize);
        move(newPos);
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

        // Handle Arrow release
        if (handleArrowAnnotationRelease(event->pos())) {
            return;
        }

        // Handle Polyline release
        if (handlePolylineAnnotationRelease(event->pos())) {
            return;
        }

        // In annotation mode, route to ToolManager
        if (m_annotationMode && m_toolManager) {
            // For drawing tools: only route if actively drawing
            // For single-click tools (Emoji, StepBadge): always route release
            bool isSingleClickTool = (m_currentToolId == ToolId::EmojiSticker ||
                m_currentToolId == ToolId::StepBadge);

            if (m_toolManager->isDrawing() || isSingleClickTool) {
                m_toolManager->handleMouseRelease(mapToOriginalCoords(event->pos()), event->modifiers());
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
            int hitIndex = m_annotationLayer->hitTestText(event->pos());
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
        const qreal opacityStep = PinWindowSettingsManager::instance().loadOpacityStep();
        qreal oldOpacity = m_opacity;
        qreal newOpacity = (event->angleDelta().y() > 0)
            ? m_opacity + opacityStep
            : m_opacity - opacityStep;
        newOpacity = qBound(0.1, newOpacity, 1.0);

        if (qFuzzyCompare(oldOpacity, newOpacity)) {
            event->accept();
            return;
        }

        setOpacity(newOpacity);
        m_uiIndicators->showOpacityIndicator(m_opacity);
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
    }
    else if (event->key() == Qt::Key_2) {
        rotateLeft();
    }
    else if (event->key() == Qt::Key_3) {
        flipHorizontal();
    }
    else if (event->key() == Qt::Key_4) {
        flipVertical();
    }
    else if (event->matches(QKeySequence::Save)) {
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

    // Initialize cursor manager for centralized cursor handling (like RegionSelector)
    auto& cursorManager = CursorManager::instance();
    cursorManager.registerWidget(this, m_toolManager);

    // Initialize text annotation editor components
    m_textEditor = new InlineTextEditor(this);
    m_textAnnotationEditor = new TextAnnotationEditor(this);
    m_textAnnotationEditor->setAnnotationLayer(m_annotationLayer);
    m_textAnnotationEditor->setTextEditor(m_textEditor);
    m_textAnnotationEditor->setParentWidget(this);

    // Connect text editor signals
    connect(m_textEditor, &InlineTextEditor::editingFinished,
        this, [this](const QString& text, const QPoint& position) {
            m_textAnnotationEditor->finishEditing(text, position, m_annotationColor);
            updateUndoRedoState();
            update();
        });
    connect(m_textEditor, &InlineTextEditor::editingCancelled,
        this, [this]() {
            m_textAnnotationEditor->cancelEditing();
            update();
        });

    // Initialize toolbar (not parented - separate floating window)
    m_toolbar = new WindowedToolbar(nullptr);
    m_toolbar->setOCRAvailable(PlatformFeatures::instance().isOCRAvailable());

    // Connect toolbar signals
    connect(m_toolbar, &WindowedToolbar::toolSelected,
        this, &PinWindow::handleToolbarToolSelected);
    connect(m_toolbar, &WindowedToolbar::undoClicked,
        this, &PinWindow::handleToolbarUndo);
    connect(m_toolbar, &WindowedToolbar::redoClicked,
        this, &PinWindow::handleToolbarRedo);
    connect(m_toolbar, &WindowedToolbar::ocrClicked,
        this, &PinWindow::performOCR);
    connect(m_toolbar, &WindowedToolbar::qrCodeClicked,
        this, &PinWindow::performQRCodeScan);
    connect(m_toolbar, &WindowedToolbar::copyClicked,
        this, &PinWindow::copyToClipboard);
    connect(m_toolbar, &WindowedToolbar::saveClicked,
        this, &PinWindow::saveToFile);
    connect(m_toolbar, &WindowedToolbar::doneClicked,
        this, &PinWindow::hideToolbar);
    connect(m_toolbar, &WindowedToolbar::cursorRestoreRequested,
        this, &PinWindow::updateCursorForTool);

    // Initialize sub-toolbar (not parented - separate floating window)
    m_subToolbar = new WindowedSubToolbar(nullptr);

    // Connect sub-toolbar signals
    connect(m_subToolbar, &WindowedSubToolbar::colorSelected,
        this, &PinWindow::onColorSelected);
    connect(m_subToolbar, &WindowedSubToolbar::widthChanged,
        this, &PinWindow::onWidthChanged);
    connect(m_subToolbar, &WindowedSubToolbar::emojiSelected,
        this, &PinWindow::onEmojiSelected);
    connect(m_subToolbar, &WindowedSubToolbar::stepBadgeSizeChanged,
        this, &PinWindow::onStepBadgeSizeChanged);
    connect(m_subToolbar, &WindowedSubToolbar::shapeTypeChanged,
        this, &PinWindow::onShapeTypeChanged);
    connect(m_subToolbar, &WindowedSubToolbar::shapeFillModeChanged,
        this, &PinWindow::onShapeFillModeChanged);
    connect(m_subToolbar, &WindowedSubToolbar::arrowStyleChanged,
        this, &PinWindow::onArrowStyleChanged);
    connect(m_subToolbar, &WindowedSubToolbar::lineStyleChanged,
        this, &PinWindow::onLineStyleChanged);
    connect(m_subToolbar, &WindowedSubToolbar::fontSizeDropdownRequested,
        this, &PinWindow::onFontSizeDropdownRequested);
    connect(m_subToolbar, &WindowedSubToolbar::fontFamilyDropdownRequested,
        this, &PinWindow::onFontFamilyDropdownRequested);
    connect(m_subToolbar, &WindowedSubToolbar::autoBlurRequested,
        this, &PinWindow::onAutoBlurRequested);
    connect(m_subToolbar, &WindowedSubToolbar::cursorRestoreRequested,
        this, &PinWindow::updateCursorForTool);

    // Connect TextAnnotationEditor to ToolOptionsPanel (must be after sub-toolbar creation)
    m_textAnnotationEditor->setColorAndWidthWidget(m_subToolbar->colorAndWidthWidget());

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
    m_toolbar->setAssociatedWidgets(this, m_subToolbar);

    // Connect close request signal
    connect(m_toolbar, &WindowedToolbar::closeRequested,
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
    // Map toolbar button ID to ToolId
    ToolId tool = ToolId::Selection;

    switch (toolId) {
    case WindowedToolbar::ButtonPencil:
        tool = ToolId::Pencil;
        break;
    case WindowedToolbar::ButtonMarker:
        tool = ToolId::Marker;
        break;
    case WindowedToolbar::ButtonArrow:
        tool = ToolId::Arrow;
        break;
    case WindowedToolbar::ButtonShape:
        tool = ToolId::Shape;
        break;
    case WindowedToolbar::ButtonText:
        tool = ToolId::Text;
        break;
    case WindowedToolbar::ButtonMosaic:
        tool = ToolId::Mosaic;
        break;
    case WindowedToolbar::ButtonEraser:
        tool = ToolId::Eraser;
        break;
    case WindowedToolbar::ButtonStepBadge:
        tool = ToolId::StepBadge;
        // StepBadgeToolHandler reads size from AnnotationSettingsManager, no setWidth needed
        break;
    case WindowedToolbar::ButtonEmoji:
        tool = ToolId::EmojiSticker;
        break;
    default:
        break;
    }

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
    if (m_annotationLayer && m_annotationLayer->canUndo()) {
        m_annotationLayer->undo();
        updateUndoRedoState();
        update();
    }
}

void PinWindow::handleToolbarRedo()
{
    if (m_annotationLayer && m_annotationLayer->canRedo()) {
        m_annotationLayer->redo();
        updateUndoRedoState();
        update();
    }
}

void PinWindow::updateUndoRedoState()
{
    if (m_toolbar && m_annotationLayer) {
        m_toolbar->setCanUndo(m_annotationLayer->canUndo());
        m_toolbar->setCanRedo(m_annotationLayer->canRedo());
    }
}

bool PinWindow::isAnnotationTool(ToolId toolId) const
{
    switch (toolId) {
    case ToolId::Pencil:
    case ToolId::Marker:
    case ToolId::Arrow:
    case ToolId::Shape:
    case ToolId::Text:
    case ToolId::Mosaic:
    case ToolId::Eraser:
    case ToolId::StepBadge:
    case ToolId::EmojiSticker:
        return true;
    default:
        return false;
    }
}

QPixmap PinWindow::getExportPixmapWithAnnotations() const
{
    QPixmap result = getExportPixmap();

    // Draw annotations onto the exported pixmap
    if (m_annotationLayer && !m_annotationLayer->isEmpty()) {
        QPainter painter(&result);
        painter.setRenderHint(QPainter::Antialiasing);
        m_annotationLayer->draw(painter);
        painter.end();
    }

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
    if (!m_subToolbar) return;

    QMenu menu;
    QVector<int> sizes = { 8, 10, 12, 14, 16, 18, 20, 24, 28, 32, 36, 48, 72 };
    for (int size : sizes) {
        QAction* action = menu.addAction(QString::number(size));
        connect(action, &QAction::triggered, this, [this, size]() {
            if (m_subToolbar) {
                m_subToolbar->colorAndWidthWidget()->setFontSize(size);
            }
            });
    }
    menu.exec(m_subToolbar->mapToGlobal(pos));
}

void PinWindow::onFontFamilyDropdownRequested(const QPoint& pos)
{
    if (!m_subToolbar) return;

    QMenu menu;
    QStringList families = { "Arial", "Helvetica", "Times New Roman", "Georgia",
                            "Courier New", "Verdana", "Tahoma" };
    for (const QString& family : families) {
        QAction* action = menu.addAction(family);
        connect(action, &QAction::triggered, this, [this, family]() {
            if (m_subToolbar) {
                m_subToolbar->colorAndWidthWidget()->setFontFamily(family);
            }
            });
    }
    menu.exec(m_subToolbar->mapToGlobal(pos));
}

void PinWindow::onAutoBlurRequested()
{
    // Lazy initialize AutoBlurManager
    if (!m_autoBlurManager) {
        m_autoBlurManager = new AutoBlurManager(this);
    }

    // Get current display image
    QImage image = m_displayPixmap.toImage();
    qreal dpr = m_displayPixmap.devicePixelRatio();

    // Detect faces
    auto result = m_autoBlurManager->detect(image);

    if (!result.success || result.faceRegions.isEmpty()) {
        return;
    }

    // Create shared pixmap for all face annotations (explicit sharing to avoid memory duplication)
    auto sharedDisplayPixmap = std::make_shared<const QPixmap>(m_displayPixmap);

    // Create mosaic annotations for each detected face
    for (const QRect& faceRect : result.faceRegions) {
        // Convert from device pixels to logical pixels
        QRect logicalRect = CoordinateHelper::toLogical(faceRect, dpr);

        auto mosaic = std::make_unique<MosaicRectAnnotation>(
            logicalRect,
            sharedDisplayPixmap,
            kMosaicBlockSize,
            MosaicRectAnnotation::BlurType::Pixelate
        );
        m_annotationLayer->addItem(std::move(mosaic));
    }

    updateUndoRedoState();
    update();
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

    int hitIndex = m_annotationLayer->hitTestText(pos);
    if (hitIndex < 0) return false;

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    // Double-click on ANY text annotation triggers re-edit (no need to be pre-selected)
    if (m_textAnnotationEditor->isDoubleClick(pos, now)) {
        m_annotationLayer->setSelectedIndex(hitIndex);
        m_textAnnotationEditor->startReEditing(hitIndex, m_annotationColor);
        m_textAnnotationEditor->recordClick(QPoint(), 0);
        return true;
    }
    m_textAnnotationEditor->recordClick(pos, now);

    // Single click: select and start dragging
    m_annotationLayer->setSelectedIndex(hitIndex);
    if (auto* textItem = getSelectedTextAnnotation()) {
        GizmoHandle handle = TransformationGizmo::hitTest(textItem, pos);
        if (handle == GizmoHandle::Body || handle == GizmoHandle::None) {
            m_textAnnotationEditor->startDragging(pos);
        }
        else {
            m_textAnnotationEditor->startTransformation(pos, handle);
        }
    }

    update();
    return true;
}

bool PinWindow::handleGizmoPress(const QPoint& pos)
{
    auto* textItem = getSelectedTextAnnotation();
    if (!textItem) return false;

    GizmoHandle handle = TransformationGizmo::hitTest(textItem, pos);
    if (handle == GizmoHandle::None) return false;

    if (handle == GizmoHandle::Body) {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (m_textAnnotationEditor->isDoubleClick(pos, now)) {
            m_textAnnotationEditor->startReEditing(
                m_annotationLayer->selectedIndex(), m_annotationColor);
            m_textAnnotationEditor->recordClick(QPoint(), 0);
            return true;
        }
        m_textAnnotationEditor->recordClick(pos, now);
        m_textAnnotationEditor->startDragging(pos);
    }
    else {
        m_textAnnotationEditor->startTransformation(pos, handle);
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
    if (m_isLiveMode || m_sourceRegion.isEmpty() || !m_sourceScreen) {
        qWarning() << "Cannot start live capture: invalid state or no source region";
        return;
    }

    // Validate screen still exists
    if (!QGuiApplication::screens().contains(m_sourceScreen)) {
        qWarning() << "Source screen no longer available";
        return;
    }

    m_isLiveMode = true;

    // Create capture engine
    m_captureEngine = ICaptureEngine::createBestEngine(this);
    m_captureEngine->setRegion(m_sourceRegion, m_sourceScreen);
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
        delete m_captureTimer;
        m_captureTimer = nullptr;
    }

    if (m_liveIndicatorTimer) {
        m_liveIndicatorTimer->stop();
        delete m_liveIndicatorTimer;
        m_liveIndicatorTimer = nullptr;
    }

    if (m_captureEngine) {
        m_captureEngine->stop();
        m_captureEngine->deleteLater();
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

    if (!m_sourceScreen || !QGuiApplication::screens().contains(m_sourceScreen)) {
        qWarning() << "Source screen disconnected during live capture";
        stopLiveCapture();
        return;
    }

    QImage frame = m_captureEngine->captureFrame();
    if (!frame.isNull()) {
        m_originalPixmap = QPixmap::fromImage(frame);
        m_originalPixmap.setDevicePixelRatio(m_sourceScreen->devicePixelRatio());

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
        getSelectedArrowAnnotation(),
        getSelectedPolylineAnnotation()
    );

    if (result.hit) {
        cursorManager.setHoverTargetForWidget(this, result.target, result.handleIndex);
    } else {
        // No annotation hit - clear hover target to let tool cursor show
        cursorManager.setHoverTargetForWidget(this, HoverTarget::None);
    }
}
