#include "PinWindow.h"
#include "PinWindowManager.h"
#include "OCRManager.h"
#include "PlatformFeatures.h"
#include "WatermarkRenderer.h"
#include "cursor/CursorManager.h"
#include "platform/WindowLevel.h"
#include "pinwindow/ResizeHandler.h"
#include "pinwindow/UIIndicators.h"
#include "settings/AnnotationSettingsManager.h"
#include "settings/PinWindowSettingsManager.h"

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
#include <QFileDialog>
#include <QGuiApplication>
#include <QClipboard>
#include <QCursor>
#include <QStandardPaths>
#include <QDateTime>
#include <QToolTip>
#include <QLabel>
#include <QTimer>
#include <QPointer>
#include <QTransform>
#include <QDebug>
#include <QActionGroup>

namespace {
bool hasTransparentCornerPixels(const QPixmap &pixmap)
{
    if (pixmap.isNull()) {
        return false;
    }

    QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) {
        return false;
    }

    int width = image.width();
    int height = image.height();
    if (width <= 0 || height <= 0) {
        return false;
    }

    const auto alphaAt = [&image](int x, int y) {
        return qAlpha(image.pixel(x, y));
    };

    return alphaAt(0, 0) < 255 ||
           alphaAt(width - 1, 0) < 255 ||
           alphaAt(0, height - 1) < 255 ||
           alphaAt(width - 1, height - 1) < 255;
}
}  // namespace

PinWindow::PinWindow(const QPixmap &screenshot, const QPoint &position, QWidget *parent)
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

    createContextMenu();

    // Initialize OCR manager if available on this platform
    m_ocrManager = PlatformFeatures::instance().createOCRManager(this);

    // Enable mouse tracking for resize cursor
    setMouseTracking(true);

    // Initialize components
    m_resizeHandler = new ResizeHandler(0, kMinSize, this);
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

    // Load watermark settings
    m_watermarkSettings = WatermarkRenderer::loadSettings();

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
    m_clickThroughHoverTimer->setInterval(50);
    connect(m_clickThroughHoverTimer, &QTimer::timeout, this, &PinWindow::updateClickThroughForCursor);

    // Initialize resize throttle timer
    m_resizeThrottleTimer.start();

    qDebug() << "PinWindow: Created with size" << m_displayPixmap.size()
             << "requested position" << position
             << "actual position" << pos();
}

PinWindow::~PinWindow()
{
    qDebug() << "PinWindow: Destroyed";
}

void PinWindow::setPinWindowManager(PinWindowManager *manager)
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
    qDebug() << "PinWindow: Rotated right, angle now" << m_rotationAngle;
}

void PinWindow::rotateLeft()
{
    m_rotationAngle = (m_rotationAngle + 270) % 360;  // +270 is same as -90
    updateSize();
    qDebug() << "PinWindow: Rotated left, angle now" << m_rotationAngle;
}

void PinWindow::flipHorizontal()
{
    m_flipHorizontal = !m_flipHorizontal;
    updateSize();
    qDebug() << "PinWindow: Flipped horizontal, now" << m_flipHorizontal;
}

void PinWindow::flipVertical()
{
    m_flipVertical = !m_flipVertical;
    updateSize();
    qDebug() << "PinWindow: Flipped vertical, now" << m_flipVertical;
}

void PinWindow::setWatermarkSettings(const WatermarkRenderer::Settings &settings)
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
    } else {
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

int PinWindow::effectiveCornerRadius(const QSize &contentSize) const
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
    } else {
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

    qDebug() << "PinWindow: Zoom level" << m_zoomLevel << "rotation" << m_rotationAngle
             << "flipH" << m_flipHorizontal << "flipV" << m_flipVertical
             << "logical size" << newLogicalSize;
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
    qDebug() << "PinWindow::getExportPixmap called";
    qDebug() << "  m_watermarkSettings.enabled:" << m_watermarkSettings.enabled;
    qDebug() << "  m_watermarkSettings.imagePath:" << m_watermarkSettings.imagePath;

    // 1. Get current display size (includes zoom)
    QSize exportSize = m_displayPixmap.size();
    qDebug() << "  exportSize:" << exportSize;

    // 2. Get transformed pixmap (rotation/flip) and scale to current size
    QPixmap basePixmap = getTransformedPixmap();
    qDebug() << "  basePixmap size:" << basePixmap.size();

    QPixmap scaledPixmap = basePixmap.scaled(
        exportSize,
        Qt::IgnoreAspectRatio,
        Qt::SmoothTransformation
    );
    scaledPixmap.setDevicePixelRatio(basePixmap.devicePixelRatio());
    qDebug() << "  scaledPixmap size:" << scaledPixmap.size();

    // 3. If opacity is adjusted, paint with opacity
    if (m_opacity < 1.0) {
        qDebug() << "  Applying opacity:" << m_opacity;
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
    qDebug() << "  Calling WatermarkRenderer::applyToPixmap";
    return WatermarkRenderer::applyToPixmap(scaledPixmap, m_watermarkSettings);
}

void PinWindow::createContextMenu()
{
    m_contextMenu = new QMenu(this);

    QAction *copyAction = m_contextMenu->addAction("Copy to Clipboard");
    copyAction->setShortcut(QKeySequence::Copy);
    connect(copyAction, &QAction::triggered, this, &PinWindow::copyToClipboard);

    QAction *saveAction = m_contextMenu->addAction("Save to file");
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &PinWindow::saveToFile);

    if (PlatformFeatures::instance().isOCRAvailable()) {
        QAction *ocrAction = m_contextMenu->addAction("OCR Text Recognition");
        connect(ocrAction, &QAction::triggered, this, &PinWindow::performOCR);
    }

    m_contextMenu->addSeparator();

    // Watermark submenu
    QMenu *watermarkMenu = m_contextMenu->addMenu("Watermark");

    // Enable watermark action
    QAction *enableWatermarkAction = watermarkMenu->addAction("Enable");
    enableWatermarkAction->setCheckable(true);
    enableWatermarkAction->setChecked(m_watermarkSettings.enabled);
    connect(enableWatermarkAction, &QAction::toggled, this, [this](bool checked) {
        m_watermarkSettings.enabled = checked;
        update();
    });

    watermarkMenu->addSeparator();

    // Position submenu
    QMenu *positionMenu = watermarkMenu->addMenu("Position");
    QActionGroup *positionGroup = new QActionGroup(this);

    QAction *topLeftAction = positionMenu->addAction("Top-Left");
    topLeftAction->setCheckable(true);
    topLeftAction->setData(static_cast<int>(WatermarkRenderer::TopLeft));
    positionGroup->addAction(topLeftAction);

    QAction *topRightAction = positionMenu->addAction("Top-Right");
    topRightAction->setCheckable(true);
    topRightAction->setData(static_cast<int>(WatermarkRenderer::TopRight));
    positionGroup->addAction(topRightAction);

    QAction *bottomLeftAction = positionMenu->addAction("Bottom-Left");
    bottomLeftAction->setCheckable(true);
    bottomLeftAction->setData(static_cast<int>(WatermarkRenderer::BottomLeft));
    positionGroup->addAction(bottomLeftAction);

    QAction *bottomRightAction = positionMenu->addAction("Bottom-Right");
    bottomRightAction->setCheckable(true);
    bottomRightAction->setData(static_cast<int>(WatermarkRenderer::BottomRight));
    positionGroup->addAction(bottomRightAction);

    // Set current position
    for (QAction *action : positionGroup->actions()) {
        if (action->data().toInt() == static_cast<int>(m_watermarkSettings.position)) {
            action->setChecked(true);
            break;
        }
    }

    connect(positionGroup, &QActionGroup::triggered, this, [this](QAction *action) {
        m_watermarkSettings.position = static_cast<WatermarkRenderer::Position>(action->data().toInt());
        update();
    });

    // Zoom submenu
    QMenu *zoomMenu = m_contextMenu->addMenu("Zoom");

    // Preset zoom levels
    QAction *zoom33Action = zoomMenu->addAction("33.3%");
    connect(zoom33Action, &QAction::triggered, this, [this]() { setZoomLevel(1.0 / 3.0); });

    QAction *zoom50Action = zoomMenu->addAction("50%");
    connect(zoom50Action, &QAction::triggered, this, [this]() { setZoomLevel(0.5); });

    QAction *zoom67Action = zoomMenu->addAction("66.7%");
    connect(zoom67Action, &QAction::triggered, this, [this]() { setZoomLevel(2.0 / 3.0); });

    QAction *zoom100Action = zoomMenu->addAction("100%");
    connect(zoom100Action, &QAction::triggered, this, [this]() { setZoomLevel(1.0); });

    QAction *zoom200Action = zoomMenu->addAction("200%");
    connect(zoom200Action, &QAction::triggered, this, [this]() { setZoomLevel(2.0); });

    zoomMenu->addSeparator();

    // Current zoom level display (disabled, for display only)
    m_currentZoomAction = zoomMenu->addAction(QString("%1%").arg(qRound(m_zoomLevel * 100)));
    m_currentZoomAction->setEnabled(false);

    // Smoothing option
    QAction *smoothingAction = zoomMenu->addAction("Smoothing");
    smoothingAction->setCheckable(true);
    smoothingAction->setChecked(m_smoothing);
    connect(smoothingAction, &QAction::toggled, this, [this](bool checked) {
        m_smoothing = checked;
        updateSize();
    });

    // Image processing submenu
    QMenu *imageProcessingMenu = m_contextMenu->addMenu("Image processing");

    QAction *rotateLeftAction = imageProcessingMenu->addAction("Rotate left");
    connect(rotateLeftAction, &QAction::triggered, this, &PinWindow::rotateLeft);

    QAction *rotateRightAction = imageProcessingMenu->addAction("Rotate right");
    connect(rotateRightAction, &QAction::triggered, this, &PinWindow::rotateRight);

    QAction *horizontalFlipAction = imageProcessingMenu->addAction("Horizontal flip");
    connect(horizontalFlipAction, &QAction::triggered, this, &PinWindow::flipHorizontal);

    QAction *verticalFlipAction = imageProcessingMenu->addAction("Vertical flip");
    connect(verticalFlipAction, &QAction::triggered, this, &PinWindow::flipVertical);

    // Info submenu - displays image properties
    QMenu *infoMenu = m_contextMenu->addMenu(
        QString("%1 × %2").arg(m_originalPixmap.width()).arg(m_originalPixmap.height()));

    // Copy all info action
    QAction *copyAllInfoAction = infoMenu->addAction("Copy All");
    connect(copyAllInfoAction, &QAction::triggered, this, &PinWindow::copyAllInfo);

    infoMenu->addSeparator();

    // Info items - clicking copies that value
    auto addInfoItem = [infoMenu](const QString &label, const QString &value) {
        QAction *action = infoMenu->addAction(QString("%1: %2").arg(label, value));
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

    // Show border option
    m_showBorderAction = m_contextMenu->addAction("Show Border");
    m_showBorderAction->setCheckable(true);
    m_showBorderAction->setChecked(m_showBorder);
    connect(m_showBorderAction, &QAction::toggled, this, &PinWindow::setShowBorder);

    // Click-through option
    m_clickThroughAction = m_contextMenu->addAction("Click-through");
    m_clickThroughAction->setCheckable(true);
    m_clickThroughAction->setChecked(m_clickThrough);
    connect(m_clickThroughAction, &QAction::toggled, this, &PinWindow::setClickThrough);

    m_contextMenu->addSeparator();

    QAction *closeAction = m_contextMenu->addAction("Close");
    closeAction->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(closeAction, &QAction::triggered, this, &PinWindow::close);

    QAction *closeAllPinsAction = m_contextMenu->addAction("Close All Pins");
    connect(closeAllPinsAction, &QAction::triggered, this, [this]() {
        if (m_pinWindowManager) {
            m_pinWindowManager->closeAllWindows();
        }
    });
}

void PinWindow::saveToFile()
{
    QString picturesPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString defaultName = QString("%1/Screenshot_%2.png").arg(picturesPath).arg(timestamp);

    QString filePath = QFileDialog::getSaveFileName(
        this,
        "Save Screenshot",
        defaultName,
        "PNG Image (*.png);;JPEG Image (*.jpg *.jpeg);;All Files (*)"
    );

    if (!filePath.isEmpty()) {
        QPixmap pixmapToSave = getExportPixmap();
        if (pixmapToSave.save(filePath)) {
            qDebug() << "PinWindow: Saved to" << filePath;
            emit saveRequested(pixmapToSave);
        } else {
            qDebug() << "PinWindow: Failed to save to" << filePath;
        }
    }
}

void PinWindow::copyToClipboard()
{
    QPixmap pixmapToCopy = getExportPixmap();
    QGuiApplication::clipboard()->setPixmap(pixmapToCopy);
    qDebug() << "PinWindow: Copied to clipboard";
}

void PinWindow::performOCR()
{
    if (!m_ocrManager || m_ocrInProgress) {
        if (!m_ocrManager) {
            qDebug() << "PinWindow: OCR not available on this platform";
        }
        return;
    }

    m_ocrInProgress = true;
    m_loadingSpinner->start();
    update();
    qDebug() << "PinWindow: Starting OCR recognition...";

    QPointer<PinWindow> safeThis = this;
    m_ocrManager->recognizeText(m_originalPixmap,
        [safeThis](bool success, const QString &text, const QString &error) {
            if (safeThis) {
                safeThis->onOCRComplete(success, text, error);
            } else {
                qDebug() << "PinWindow: OCR completed but window was destroyed, ignoring result";
            }
        });
}

void PinWindow::onOCRComplete(bool success, const QString &text, const QString &error)
{
    m_ocrInProgress = false;
    m_loadingSpinner->stop();

    QString msg;
    if (success && !text.isEmpty()) {
        QGuiApplication::clipboard()->setText(text);
        qDebug() << "PinWindow: OCR complete, copied" << text.length() << "characters to clipboard";
        msg = tr("Copied %1 characters").arg(text.length());
    } else {
        msg = error.isEmpty() ? tr("No text found") : error;
        qDebug() << "PinWindow: OCR failed:" << msg;
    }

    m_uiIndicators->showOCRToast(success && !text.isEmpty(), msg);
    emit ocrCompleted(success && !text.isEmpty(), msg);
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
    } else {
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

void PinWindow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::Antialiasing);

    QRect pixmapRect = rect();

    // Draw the screenshot
    painter.drawPixmap(pixmapRect, m_displayPixmap);

    // Draw border if enabled
    if (m_showBorder) {
        int cornerRadius = effectiveCornerRadius(size());
        if (m_clickThrough) {
            // Dashed indigo border for click-through mode
            painter.setPen(QPen(QColor(88, 86, 214, 200), 2, Qt::DashLine));
        } else {
            // Blue border
            painter.setPen(QPen(QColor(0, 122, 255, 200), 1.5));
        }
        painter.setBrush(Qt::NoBrush);
        if (cornerRadius > 0) {
            painter.drawRoundedRect(pixmapRect.adjusted(1, 1, -1, -1), cornerRadius, cornerRadius);
        } else {
            painter.drawRect(pixmapRect.adjusted(1, 1, -1, -1));
        }
    }

    // Draw watermark
    WatermarkRenderer::render(painter, pixmapRect, m_watermarkSettings);

    // Draw loading spinner when OCR is in progress
    if (m_ocrInProgress) {
        QPoint center = pixmapRect.center();
        m_loadingSpinner->draw(painter, center);
    }
}

void PinWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        ResizeHandler::Edge edge = m_resizeHandler->getEdgeAt(event->pos(), size());

        if (edge != ResizeHandler::Edge::None) {
            // Start resizing
            m_isResizing = true;
            m_resizeHandler->startResize(edge, event->globalPosition().toPoint(), size(), pos());
        } else {
            // Start dragging
            m_isDragging = true;
            m_dragStartPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
            setCursor(Qt::ClosedHandCursor);
        }
    }
}

void PinWindow::mouseMoveEvent(QMouseEvent *event)
{
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
        m_resizeFinishTimer->start(150);  // Debounce: wait 150ms after last resize

        // Apply new size and position
        setFixedSize(newSize);
        move(newPos);
        update();

        m_uiIndicators->showZoomIndicator(m_zoomLevel);

    } else if (m_isDragging) {
        move(event->globalPosition().toPoint() - m_dragStartPos);
    } else {
        // Update cursor based on position using centralized CursorManager
        ResizeHandler::Edge edge = m_resizeHandler->getEdgeAt(event->pos(), size());
        setCursor(CursorManager::cursorForEdge(static_cast<int>(edge)));
    }
}

void PinWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_isResizing) {
            m_isResizing = false;
            m_resizeHandler->finishResize();
        }
        if (m_isDragging) {
            m_isDragging = false;
            setCursor(Qt::ArrowCursor);
        }
        if (m_clickThrough) {
            updateClickThroughForCursor();
        }
    }
}

void PinWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        close();
    }
}

void PinWindow::wheelEvent(QWheelEvent *event)
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

void PinWindow::contextMenuEvent(QContextMenuEvent *event)
{
    // Use popup() instead of exec() to avoid blocking the event loop.
    // This prevents UI freeze when global hotkeys (like F2) are pressed
    // while the context menu is open.
    m_contextMenu->popup(event->globalPos());
}

void PinWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
    } else if (event->key() == Qt::Key_1) {
        rotateRight();
    } else if (event->key() == Qt::Key_2) {
        rotateLeft();
    } else if (event->key() == Qt::Key_3) {
        flipHorizontal();
    } else if (event->key() == Qt::Key_4) {
        flipVertical();
    } else if (event->matches(QKeySequence::Save)) {
        saveToFile();
    } else if (event->matches(QKeySequence::Copy)) {
        copyToClipboard();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void PinWindow::closeEvent(QCloseEvent *event)
{
    emit closed(this);
    event->accept();
}

void PinWindow::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
}
