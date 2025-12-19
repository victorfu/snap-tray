#include "PinWindow.h"

#ifdef Q_OS_MACOS
#include "OCRManager.h"
#endif

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QContextMenuEvent>
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

PinWindow::PinWindow(const QPixmap &screenshot, const QPoint &position, QWidget *parent)
    : QWidget(parent)
    , m_originalPixmap(screenshot)
    , m_zoomLevel(1.0)
    , m_isDragging(false)
    , m_contextMenu(nullptr)
    , m_resizeEdge(ResizeEdge::None)
    , m_isResizing(false)
#ifdef Q_OS_MACOS
    , m_ocrManager(nullptr)
    , m_ocrInProgress(false)
#endif
    , m_rotationAngle(0)
    , m_flipHorizontal(false)
    , m_flipVertical(false)
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

    // Initial size matches screenshot plus shadow margins (use logical size for HiDPI)
    m_displayPixmap = m_originalPixmap;
    QSize logicalSize = m_displayPixmap.size() / m_displayPixmap.devicePixelRatio();
    QSize windowSize = logicalSize + QSize(kShadowMargin * 2, kShadowMargin * 2);
    setFixedSize(windowSize);

    createContextMenu();

#ifdef Q_OS_MACOS
    // Initialize OCR manager if available (macOS only)
    if (OCRManager::isAvailable()) {
        m_ocrManager = new OCRManager(this);
    }
#endif

    // Enable mouse tracking for resize cursor
    setMouseTracking(true);

    // Zoom indicator label
    m_zoomLabel = new QLabel(this);
    m_zoomLabel->setStyleSheet(
        "QLabel {"
        "  background-color: rgba(0, 0, 0, 180);"
        "  color: white;"
        "  padding: 4px 8px;"
        "  border-radius: 4px;"
        "  font-size: 12px;"
        "}"
    );
    m_zoomLabel->hide();

    m_zoomLabelTimer = new QTimer(this);
    m_zoomLabelTimer->setSingleShot(true);
    connect(m_zoomLabelTimer, &QTimer::timeout, m_zoomLabel, &QLabel::hide);

    // Must show() first, then move() to get correct positioning on macOS
    // Moving before show() can result in incorrect window placement
    // Offset position by shadow margin so the pixmap content appears at the expected location
    show();
    move(position - QPoint(kShadowMargin, kShadowMargin));

    qDebug() << "PinWindow: Created with size" << m_displayPixmap.size()
             << "requested position" << position
             << "actual position" << pos();
}

PinWindow::~PinWindow()
{
    qDebug() << "PinWindow: Destroyed";
}

void PinWindow::setZoomLevel(qreal zoom)
{
    m_zoomLevel = qBound(0.1, zoom, 5.0);
    updateSize();
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

void PinWindow::updateSize()
{
    // Calculate logical size accounting for devicePixelRatio
    QSize logicalOriginalSize = m_originalPixmap.size() / m_originalPixmap.devicePixelRatio();

    // Apply rotation and flip transforms if needed
    QPixmap transformedPixmap = m_originalPixmap;
    if (m_rotationAngle != 0 || m_flipHorizontal || m_flipVertical) {
        QTransform transform;

        // Apply rotation first
        if (m_rotationAngle != 0) {
            transform.rotate(m_rotationAngle);
        }

        // Apply flip transformations
        // scale(-1, 1) for horizontal flip, scale(1, -1) for vertical flip
        if (m_flipHorizontal || m_flipVertical) {
            qreal scaleX = m_flipHorizontal ? -1.0 : 1.0;
            qreal scaleY = m_flipVertical ? -1.0 : 1.0;
            transform.scale(scaleX, scaleY);
        }

        transformedPixmap = m_originalPixmap.transformed(transform, Qt::SmoothTransformation);
        transformedPixmap.setDevicePixelRatio(m_originalPixmap.devicePixelRatio());
    }

    // For 90/270 degree rotations, swap width and height
    QSize transformedLogicalSize = transformedPixmap.size() / transformedPixmap.devicePixelRatio();
    QSize newLogicalSize = transformedLogicalSize * m_zoomLevel;

    // Scale to device pixels for the actual pixmap
    QSize newDeviceSize = newLogicalSize * transformedPixmap.devicePixelRatio();
    m_displayPixmap = transformedPixmap.scaled(newDeviceSize,
                                               Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation);
    m_displayPixmap.setDevicePixelRatio(transformedPixmap.devicePixelRatio());

    // Add shadow margins to window size
    QSize windowSize = newLogicalSize + QSize(kShadowMargin * 2, kShadowMargin * 2);
    setFixedSize(windowSize);
    update();

    qDebug() << "PinWindow: Zoom level" << m_zoomLevel << "rotation" << m_rotationAngle
             << "flipH" << m_flipHorizontal << "flipV" << m_flipVertical
             << "logical size" << newLogicalSize;
}

void PinWindow::createContextMenu()
{
    m_contextMenu = new QMenu(this);

#ifdef Q_OS_MACOS
    QAction *ocrAction = m_contextMenu->addAction("OCR Text Recognition");
    connect(ocrAction, &QAction::triggered, this, &PinWindow::performOCR);
    m_contextMenu->addSeparator();
#endif

    QAction *saveAction = m_contextMenu->addAction("Save...");
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &PinWindow::saveToFile);

    QAction *copyAction = m_contextMenu->addAction("Copy to Clipboard");
    copyAction->setShortcut(QKeySequence::Copy);
    connect(copyAction, &QAction::triggered, this, &PinWindow::copyToClipboard);

    m_contextMenu->addSeparator();

    QAction *closeAction = m_contextMenu->addAction("Close");
    closeAction->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(closeAction, &QAction::triggered, this, &PinWindow::close);
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
        if (m_originalPixmap.save(filePath)) {
            qDebug() << "PinWindow: Saved to" << filePath;
        } else {
            qDebug() << "PinWindow: Failed to save to" << filePath;
        }
    }

    emit saveRequested(m_originalPixmap);
}

void PinWindow::copyToClipboard()
{
    QGuiApplication::clipboard()->setPixmap(m_originalPixmap);
    qDebug() << "PinWindow: Copied to clipboard";
}

#ifdef Q_OS_MACOS
void PinWindow::performOCR()
{
    if (!m_ocrManager || m_ocrInProgress) {
        if (!m_ocrManager) {
            qDebug() << "PinWindow: OCR not available (requires macOS 10.15+)";
        }
        return;
    }

    m_ocrInProgress = true;
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

    if (success && !text.isEmpty()) {
        QGuiApplication::clipboard()->setText(text);
        qDebug() << "PinWindow: OCR complete, copied" << text.length() << "characters to clipboard";
        QToolTip::showText(QCursor::pos(),
            tr("OCR: Copied %1 characters to clipboard").arg(text.length()),
            this, QRect(), 2000);
    } else {
        QString msg = error.isEmpty() ? tr("No text found") : error;
        qDebug() << "PinWindow: OCR failed:" << msg;
        QToolTip::showText(QCursor::pos(),
            tr("OCR: %1").arg(msg),
            this, QRect(), 2000);
    }
}
#endif

PinWindow::ResizeEdge PinWindow::getResizeEdge(const QPoint &pos) const
{
    int margin = kResizeMargin + kShadowMargin;

    bool onLeft = pos.x() < margin;
    bool onRight = pos.x() > width() - margin;
    bool onTop = pos.y() < margin;
    bool onBottom = pos.y() > height() - margin;

    if (onTop && onLeft) return ResizeEdge::TopLeft;
    if (onTop && onRight) return ResizeEdge::TopRight;
    if (onBottom && onLeft) return ResizeEdge::BottomLeft;
    if (onBottom && onRight) return ResizeEdge::BottomRight;
    if (onLeft) return ResizeEdge::Left;
    if (onRight) return ResizeEdge::Right;
    if (onTop) return ResizeEdge::Top;
    if (onBottom) return ResizeEdge::Bottom;

    return ResizeEdge::None;
}

void PinWindow::updateCursorForEdge(ResizeEdge edge)
{
    switch (edge) {
        case ResizeEdge::Left:
        case ResizeEdge::Right:
            setCursor(Qt::SizeHorCursor);
            break;
        case ResizeEdge::Top:
        case ResizeEdge::Bottom:
            setCursor(Qt::SizeVerCursor);
            break;
        case ResizeEdge::TopLeft:
        case ResizeEdge::BottomRight:
            setCursor(Qt::SizeFDiagCursor);
            break;
        case ResizeEdge::TopRight:
        case ResizeEdge::BottomLeft:
            setCursor(Qt::SizeBDiagCursor);
            break;
        default:
            setCursor(Qt::ArrowCursor);
            break;
    }
}

void PinWindow::showZoomIndicator()
{
    m_zoomLabel->setText(QString("%1%").arg(qRound(m_zoomLevel * 100)));
    m_zoomLabel->adjustSize();
    // Position at bottom-right corner
    m_zoomLabel->move(width() - kShadowMargin - m_zoomLabel->width() - 8,
                      height() - kShadowMargin - m_zoomLabel->height() - 8);
    m_zoomLabel->show();
    m_zoomLabel->raise();
    m_zoomLabelTimer->start(1500);
}

void PinWindow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::Antialiasing);

    // Calculate the pixmap rectangle (excluding shadow margins)
    QRect pixmapRect(kShadowMargin, kShadowMargin,
                     width() - kShadowMargin * 2,
                     height() - kShadowMargin * 2);

    // Draw soft gradient shadow effect
    painter.setPen(Qt::NoPen);
    for (int i = kShadowMargin; i >= 1; --i) {
        int alpha = 30 * (kShadowMargin - i + 1) / kShadowMargin;
        painter.setBrush(QColor(0, 0, 0, alpha));
        painter.drawRoundedRect(pixmapRect.adjusted(-i, -i, i, i), 2, 2);
    }

    // Draw the screenshot at the margin offset
    painter.drawPixmap(pixmapRect, m_displayPixmap);

    // Draw subtle border with slight glow effect (blue accent)
    painter.setPen(QPen(QColor(0, 120, 215, 200), 1.5));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(pixmapRect);
}

void PinWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        ResizeEdge edge = getResizeEdge(event->pos());

        if (edge != ResizeEdge::None) {
            // Start resizing
            m_isResizing = true;
            m_resizeEdge = edge;
            m_resizeStartPos = event->globalPosition().toPoint();
            m_resizeStartSize = size();
            m_resizeStartWindowPos = pos();
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
        QPoint delta = event->globalPosition().toPoint() - m_resizeStartPos;
        QSize newSize = m_resizeStartSize;
        QPoint newPos = m_resizeStartWindowPos;

        // Calculate new size based on which edge is being dragged
        switch (m_resizeEdge) {
            case ResizeEdge::Right:
                newSize.setWidth(m_resizeStartSize.width() + delta.x());
                break;
            case ResizeEdge::Bottom:
                newSize.setHeight(m_resizeStartSize.height() + delta.y());
                break;
            case ResizeEdge::Left:
                newSize.setWidth(m_resizeStartSize.width() - delta.x());
                newPos.setX(m_resizeStartWindowPos.x() + delta.x());
                break;
            case ResizeEdge::Top:
                newSize.setHeight(m_resizeStartSize.height() - delta.y());
                newPos.setY(m_resizeStartWindowPos.y() + delta.y());
                break;
            case ResizeEdge::TopLeft:
                newSize.setWidth(m_resizeStartSize.width() - delta.x());
                newSize.setHeight(m_resizeStartSize.height() - delta.y());
                newPos.setX(m_resizeStartWindowPos.x() + delta.x());
                newPos.setY(m_resizeStartWindowPos.y() + delta.y());
                break;
            case ResizeEdge::TopRight:
                newSize.setWidth(m_resizeStartSize.width() + delta.x());
                newSize.setHeight(m_resizeStartSize.height() - delta.y());
                newPos.setY(m_resizeStartWindowPos.y() + delta.y());
                break;
            case ResizeEdge::BottomLeft:
                newSize.setWidth(m_resizeStartSize.width() - delta.x());
                newSize.setHeight(m_resizeStartSize.height() + delta.y());
                newPos.setX(m_resizeStartWindowPos.x() + delta.x());
                break;
            case ResizeEdge::BottomRight:
                newSize.setWidth(m_resizeStartSize.width() + delta.x());
                newSize.setHeight(m_resizeStartSize.height() + delta.y());
                break;
            default:
                break;
        }

        // Enforce minimum size
        int minWindowSize = kMinSize + kShadowMargin * 2;
        if (newSize.width() < minWindowSize) {
            if (m_resizeEdge == ResizeEdge::Left ||
                m_resizeEdge == ResizeEdge::TopLeft ||
                m_resizeEdge == ResizeEdge::BottomLeft) {
                newPos.setX(m_resizeStartWindowPos.x() + m_resizeStartSize.width() - minWindowSize);
            }
            newSize.setWidth(minWindowSize);
        }
        if (newSize.height() < minWindowSize) {
            if (m_resizeEdge == ResizeEdge::Top ||
                m_resizeEdge == ResizeEdge::TopLeft ||
                m_resizeEdge == ResizeEdge::TopRight) {
                newPos.setY(m_resizeStartWindowPos.y() + m_resizeStartSize.height() - minWindowSize);
            }
            newSize.setHeight(minWindowSize);
        }

        // Update zoom level based on new size
        QSize contentSize = newSize - QSize(kShadowMargin * 2, kShadowMargin * 2);

        // Apply rotation and flip transforms if needed (same as updateSize)
        QPixmap transformedPixmap = m_originalPixmap;
        if (m_rotationAngle != 0 || m_flipHorizontal || m_flipVertical) {
            QTransform transform;
            if (m_rotationAngle != 0) {
                transform.rotate(m_rotationAngle);
            }
            if (m_flipHorizontal || m_flipVertical) {
                qreal flipScaleX = m_flipHorizontal ? -1.0 : 1.0;
                qreal flipScaleY = m_flipVertical ? -1.0 : 1.0;
                transform.scale(flipScaleX, flipScaleY);
            }
            transformedPixmap = m_originalPixmap.transformed(transform, Qt::SmoothTransformation);
            transformedPixmap.setDevicePixelRatio(m_originalPixmap.devicePixelRatio());
        }

        // Use transformed dimensions for zoom calculation
        QSize transformedLogicalSize = transformedPixmap.size() / transformedPixmap.devicePixelRatio();
        qreal scaleX = static_cast<qreal>(contentSize.width()) / transformedLogicalSize.width();
        qreal scaleY = static_cast<qreal>(contentSize.height()) / transformedLogicalSize.height();
        m_zoomLevel = qMin(scaleX, scaleY);

        // Scale transformed pixmap (not original)
        QSize newDeviceSize = contentSize * transformedPixmap.devicePixelRatio();
        m_displayPixmap = transformedPixmap.scaled(newDeviceSize,
                                                   Qt::KeepAspectRatio,
                                                   Qt::SmoothTransformation);
        m_displayPixmap.setDevicePixelRatio(transformedPixmap.devicePixelRatio());

        // Apply new size and position
        setFixedSize(newSize);
        move(newPos);
        update();

        showZoomIndicator();

    } else if (m_isDragging) {
        move(event->globalPosition().toPoint() - m_dragStartPos);
    } else {
        // Update cursor based on position
        ResizeEdge edge = getResizeEdge(event->pos());
        updateCursorForEdge(edge);
    }
}

void PinWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_isResizing) {
            m_isResizing = false;
            m_resizeEdge = ResizeEdge::None;
        }
        if (m_isDragging) {
            m_isDragging = false;
            setCursor(Qt::ArrowCursor);
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
    const qreal zoomStep = 0.1;
    qreal oldZoom = m_zoomLevel;
    qreal newZoom = (event->angleDelta().y() > 0)
                    ? m_zoomLevel + zoomStep
                    : m_zoomLevel - zoomStep;
    newZoom = qBound(0.1, newZoom, 5.0);

    if (qFuzzyCompare(oldZoom, newZoom)) {
        event->accept();
        return;
    }

    // Mouse position relative to content area
    QPointF mousePos = event->position() - QPointF(kShadowMargin, kShadowMargin);

    // Image coordinate under cursor
    QPointF imagePoint = mousePos / oldZoom;

    // Where this point will be after zoom
    QPointF newMousePos = imagePoint * newZoom;

    // Offset needed to keep cursor over same image point
    QPointF delta = newMousePos - mousePos;

    setZoomLevel(newZoom);
    move(pos() - delta.toPoint());

    showZoomIndicator();
    event->accept();
}

void PinWindow::contextMenuEvent(QContextMenuEvent *event)
{
    m_contextMenu->exec(event->globalPos());
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
