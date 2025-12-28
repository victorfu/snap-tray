#include "RecordingAnnotationOverlay.h"
#include "annotations/AnnotationLayer.h"
#include "AnnotationController.h"
#include "platform/WindowLevel.h"

#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QDebug>

RecordingAnnotationOverlay::RecordingAnnotationOverlay(QWidget *parent)
    : QWidget(parent)
    , m_annotationLayer(nullptr)
    , m_annotationController(nullptr)
    , m_cacheInvalid(true)
{
    setupWindow();

    // Create annotation layer
    m_annotationLayer = new AnnotationLayer(this);
    connect(m_annotationLayer, &AnnotationLayer::changed,
            this, &RecordingAnnotationOverlay::invalidateCache);
    connect(m_annotationLayer, &AnnotationLayer::changed,
            this, &RecordingAnnotationOverlay::annotationChanged);

    // Create annotation controller
    m_annotationController = new AnnotationController(this);
    m_annotationController->setAnnotationLayer(m_annotationLayer);
    m_annotationController->setCurrentTool(AnnotationController::Tool::None);

    // Connect controller signals
    connect(m_annotationController, &AnnotationController::annotationCompleted,
            this, [this]() {
        invalidateCache();
        update();
    });
}

RecordingAnnotationOverlay::~RecordingAnnotationOverlay()
{
    // Children are parented, will be deleted automatically
}

void RecordingAnnotationOverlay::setupWindow()
{
    // Make this a frameless, always-on-top, transparent window
    // Note: Qt::Tool can prevent mouse events on macOS, so we use
    // Qt::WindowDoesNotAcceptFocus instead to avoid stealing focus
    // while still receiving mouse events
    setWindowFlags(Qt::FramelessWindowHint |
                   Qt::WindowStaysOnTopHint |
                   Qt::NoDropShadowWindowHint |
                   Qt::WindowDoesNotAcceptFocus);

    // Enable transparent background
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);

    // Accept mouse events
    setAttribute(Qt::WA_AcceptTouchEvents, false);
    setMouseTracking(true);

    // No keyboard focus needed (undo/redo handled elsewhere)
    setFocusPolicy(Qt::NoFocus);
}

void RecordingAnnotationOverlay::setRegion(const QRect &region)
{
    m_region = region;
    setGeometry(region);
    invalidateCache();

    // Raise above menu bar on macOS
    show();
    raiseWindowAboveMenuBar(this);

    // Ensure window receives mouse events (not click-through)
    setWindowClickThrough(this, false);
}

void RecordingAnnotationOverlay::setCurrentTool(int tool)
{
    qDebug() << "RecordingAnnotationOverlay::setCurrentTool(" << tool << ")";
    if (m_annotationController) {
        m_annotationController->setCurrentTool(static_cast<AnnotationController::Tool>(tool));
    }
}

int RecordingAnnotationOverlay::currentTool() const
{
    if (m_annotationController) {
        return static_cast<int>(m_annotationController->currentTool());
    }
    return 0;
}

void RecordingAnnotationOverlay::setColor(const QColor &color)
{
    if (m_annotationController) {
        m_annotationController->setColor(color);
    }
}

QColor RecordingAnnotationOverlay::color() const
{
    if (m_annotationController) {
        return m_annotationController->color();
    }
    return Qt::red;
}

void RecordingAnnotationOverlay::setWidth(int width)
{
    if (m_annotationController) {
        m_annotationController->setWidth(width);
    }
}

int RecordingAnnotationOverlay::width() const
{
    if (m_annotationController) {
        return m_annotationController->width();
    }
    return 3;
}

void RecordingAnnotationOverlay::compositeOntoFrame(QImage &frame, qreal scale) const
{
    if (!m_annotationLayer || m_annotationLayer->isEmpty()) {
        return;
    }

    // Update cache if needed
    // Note: frame is already in physical pixel size, so we scale annotations to match
    QSize scaledSize = frame.size();
    if (m_cacheInvalid || m_cacheSize != scaledSize) {
        m_annotationCache = QImage(scaledSize, QImage::Format_ARGB32_Premultiplied);
        m_annotationCache.fill(Qt::transparent);

        {
            QPainter cachePainter(&m_annotationCache);
            cachePainter.setRenderHint(QPainter::Antialiasing);

            // Scale annotations to physical pixel size
            // Annotations are drawn in logical coordinates, frame is in physical coordinates
            if (scale != 1.0) {
                cachePainter.scale(scale, scale);
            }

            // Draw annotations
            m_annotationLayer->draw(cachePainter);

            // Draw in-progress annotation
            if (m_annotationController && m_annotationController->isDrawing()) {
                m_annotationController->drawCurrentAnnotation(cachePainter);
            }
        } // cachePainter destroyed here, releasing resources

        m_cacheSize = scaledSize;
        m_cacheInvalid = false;
    }

    // Composite onto frame with explicit scope for painter cleanup
    {
        QPainter framePainter(&frame);
        framePainter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        framePainter.drawImage(0, 0, m_annotationCache);
    } // framePainter destroyed here, releasing resources
}

void RecordingAnnotationOverlay::clear()
{
    if (m_annotationLayer) {
        m_annotationLayer->clear();
        invalidateCache();
        update();
    }
}

bool RecordingAnnotationOverlay::hasAnnotations() const
{
    return m_annotationLayer && !m_annotationLayer->isEmpty();
}

void RecordingAnnotationOverlay::undo()
{
    if (m_annotationLayer && m_annotationLayer->canUndo()) {
        m_annotationLayer->undo();
        invalidateCache();
        update();
    }
}

void RecordingAnnotationOverlay::redo()
{
    if (m_annotationLayer && m_annotationLayer->canRedo()) {
        m_annotationLayer->redo();
        invalidateCache();
        update();
    }
}

void RecordingAnnotationOverlay::invalidateCache()
{
    m_cacheInvalid = true;
}

void RecordingAnnotationOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Draw completed annotations
    if (m_annotationLayer) {
        m_annotationLayer->draw(painter);
    }

    // Draw in-progress annotation
    if (m_annotationController && m_annotationController->isDrawing()) {
        m_annotationController->drawCurrentAnnotation(painter);
    }
}

void RecordingAnnotationOverlay::mousePressEvent(QMouseEvent *event)
{
    qDebug() << "RecordingAnnotationOverlay::mousePressEvent at" << event->pos()
             << "tool:" << static_cast<int>(m_annotationController ? m_annotationController->currentTool() : AnnotationController::Tool::None);

    if (event->button() == Qt::LeftButton && m_annotationController) {
        if (m_annotationController->currentTool() != AnnotationController::Tool::None) {
            m_annotationController->startDrawing(event->pos());
            invalidateCache();
            update();
            event->accept();
            return;
        }
    }
    event->ignore();
}

void RecordingAnnotationOverlay::mouseMoveEvent(QMouseEvent *event)
{
    if (m_annotationController && m_annotationController->isDrawing()) {
        m_annotationController->updateDrawing(event->pos());
        invalidateCache();
        update();
        event->accept();
        return;
    }
    event->ignore();
}

void RecordingAnnotationOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_annotationController) {
        if (m_annotationController->isDrawing()) {
            m_annotationController->finishDrawing();
            invalidateCache();
            update();
            event->accept();
            return;
        }
    }
    event->ignore();
}

void RecordingAnnotationOverlay::keyPressEvent(QKeyEvent *event)
{
    // Handle undo/redo shortcuts
    if (event->modifiers() == Qt::ControlModifier) {
        if (event->key() == Qt::Key_Z) {
            undo();
            event->accept();
            return;
        } else if (event->key() == Qt::Key_Y) {
            redo();
            event->accept();
            return;
        }
    }

    // Handle Cmd+Shift+Z for redo on macOS
    if (event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)) {
        if (event->key() == Qt::Key_Z) {
            redo();
            event->accept();
            return;
        }
    }

    // Escape to clear tool selection
    if (event->key() == Qt::Key_Escape) {
        if (m_annotationController && m_annotationController->isDrawing()) {
            m_annotationController->cancelDrawing();
            invalidateCache();
            update();
            event->accept();
            return;
        }
    }

    event->ignore();
}

#include "moc_RecordingAnnotationOverlay.cpp"
