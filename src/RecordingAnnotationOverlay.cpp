#include "RecordingAnnotationOverlay.h"
#include "annotations/AnnotationLayer.h"
#include "AnnotationController.h"
#include "ClickRippleRenderer.h"
#include "input/MouseClickTracker.h"
#include "platform/WindowLevel.h"
#include "LaserPointerRenderer.h"
#include "recording/CursorHighlightEffect.h"
#include "recording/SpotlightEffect.h"

#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCursor>
#include <QDebug>

RecordingAnnotationOverlay::RecordingAnnotationOverlay(QWidget *parent)
    : QWidget(parent)
    , m_annotationLayer(nullptr)
    , m_annotationController(nullptr)
    , m_cacheInvalid(true)
    , m_clickRipple(nullptr)
    , m_clickTracker(nullptr)
    , m_laserRenderer(nullptr)
    , m_cursorHighlight(nullptr)
    , m_spotlightEffect(nullptr)
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

    // Create click ripple renderer
    m_clickRipple = new ClickRippleRenderer(this);
    m_clickRipple->setColor(QColor(0, 122, 255, 200));  // Apple blue
    connect(m_clickRipple, &ClickRippleRenderer::needsRepaint,
            this, &RecordingAnnotationOverlay::onClickRippleNeedsRepaint);

    // Create mouse click tracker (platform-specific)
    m_clickTracker = MouseClickTracker::create(this);
    if (m_clickTracker) {
        connect(m_clickTracker, &MouseClickTracker::mouseClicked,
                this, &RecordingAnnotationOverlay::onMouseClicked);
    }

    // Create laser pointer renderer
    m_laserRenderer = new LaserPointerRenderer(this);
    m_laserRenderer->setColor(Qt::red);
    m_laserRenderer->setWidth(3);
    connect(m_laserRenderer, &LaserPointerRenderer::needsRepaint,
            this, &RecordingAnnotationOverlay::onEffectNeedsRepaint);

    // Create cursor highlight effect
    m_cursorHighlight = new CursorHighlightEffect(this);
    connect(m_cursorHighlight, &CursorHighlightEffect::needsRepaint,
            this, &RecordingAnnotationOverlay::onEffectNeedsRepaint);

    // Create spotlight effect
    m_spotlightEffect = new SpotlightEffect(this);
    connect(m_spotlightEffect, &SpotlightEffect::needsRepaint,
            this, &RecordingAnnotationOverlay::onEffectNeedsRepaint);
}

RecordingAnnotationOverlay::~RecordingAnnotationOverlay()
{
    // Stop click tracker if running
    if (m_clickTracker && m_clickTracker->isRunning()) {
        m_clickTracker->stop();
    }
    // Stop laser pointer if drawing
    if (m_laserRenderer && m_laserRenderer->isDrawing()) {
        m_laserRenderer->stopDrawing();
    }
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
    bool hasAnnotations = m_annotationLayer && !m_annotationLayer->isEmpty();
    bool hasInProgress = m_annotationController && m_annotationController->isDrawing();
    bool hasRipples = m_clickRipple && m_clickRipple->hasActiveAnimations();
    bool hasLaser = m_laserRenderer && m_laserRenderer->hasVisiblePoints();
    bool hasCursorHighlight = m_cursorHighlight && m_cursorHighlight->hasVisibleContent();
    bool hasSpotlight = m_spotlightEffect && m_spotlightEffect->hasVisibleContent();

    // Determine which effects should be composited to video
    bool compositeIndicators = m_compositeIndicatorsToVideo;

    // Check if there's anything to render
    bool hasContent = hasAnnotations || hasInProgress || hasRipples;
    if (compositeIndicators) {
        hasContent = hasContent || hasLaser || hasCursorHighlight || hasSpotlight;
    }

    if (!hasContent) {
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

            // Scale to physical pixel size
            // Annotations and effects are drawn in logical coordinates, frame is in physical coordinates
            if (scale != 1.0) {
                cachePainter.scale(scale, scale);
            }

            // Layer order (bottom to top):
            // 1. Spotlight effect (dims surrounding area)
            if (compositeIndicators && hasSpotlight) {
                m_spotlightEffect->render(cachePainter, rect());
            }

            // 2. Completed annotations
            if (m_annotationLayer) {
                m_annotationLayer->draw(cachePainter);
            }

            // 3. In-progress annotation
            if (hasInProgress) {
                m_annotationController->drawCurrentAnnotation(cachePainter);
            }

            // 4. Laser pointer trails
            if (compositeIndicators && hasLaser) {
                m_laserRenderer->draw(cachePainter);
            }

            // 5. Cursor highlight circle
            if (compositeIndicators && hasCursorHighlight) {
                m_cursorHighlight->render(cachePainter);
            }

            // 6. Click ripple effects (top layer)
            if (hasRipples) {
                m_clickRipple->draw(cachePainter);
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

bool RecordingAnnotationOverlay::setClickHighlightEnabled(bool enabled)
{
    if (m_clickRipple) {
        m_clickRipple->setEnabled(enabled);
    }

    if (enabled && m_clickTracker) {
        if (!m_clickTracker->isRunning()) {
            if (!m_clickTracker->start()) {
                qWarning() << "RecordingAnnotationOverlay: Failed to start mouse click tracker";
                // Disable ripple since tracker failed
                if (m_clickRipple) {
                    m_clickRipple->setEnabled(false);
                }
                return false;
            } else {
                qDebug() << "RecordingAnnotationOverlay: Mouse click tracking started";
            }
        }
    } else if (!enabled && m_clickTracker && m_clickTracker->isRunning()) {
        m_clickTracker->stop();
        qDebug() << "RecordingAnnotationOverlay: Mouse click tracking stopped";
    }

    return true;
}

bool RecordingAnnotationOverlay::isClickHighlightEnabled() const
{
    return m_clickRipple && m_clickRipple->isEnabled();
}

void RecordingAnnotationOverlay::setLaserPointerEnabled(bool enabled)
{
    m_laserPointerEnabled = enabled;
    qDebug() << "RecordingAnnotationOverlay: Laser pointer" << (enabled ? "enabled" : "disabled");
}

bool RecordingAnnotationOverlay::isLaserPointerEnabled() const
{
    return m_laserPointerEnabled;
}

void RecordingAnnotationOverlay::setCursorHighlightEnabled(bool enabled)
{
    if (m_cursorHighlight) {
        m_cursorHighlight->setEnabled(enabled);
    }
    qDebug() << "RecordingAnnotationOverlay: Cursor highlight" << (enabled ? "enabled" : "disabled");
}

bool RecordingAnnotationOverlay::isCursorHighlightEnabled() const
{
    return m_cursorHighlight && m_cursorHighlight->isEnabled();
}

void RecordingAnnotationOverlay::setSpotlightEnabled(bool enabled)
{
    if (m_spotlightEffect) {
        m_spotlightEffect->setEnabled(enabled);
    }
    qDebug() << "RecordingAnnotationOverlay: Spotlight" << (enabled ? "enabled" : "disabled");
}

bool RecordingAnnotationOverlay::isSpotlightEnabled() const
{
    return m_spotlightEffect && m_spotlightEffect->isEnabled();
}

void RecordingAnnotationOverlay::setCompositeIndicatorsToVideo(bool enabled)
{
    m_compositeIndicatorsToVideo = enabled;
    qDebug() << "RecordingAnnotationOverlay: Composite indicators to video" << (enabled ? "enabled" : "disabled");
}

bool RecordingAnnotationOverlay::compositeIndicatorsToVideo() const
{
    return m_compositeIndicatorsToVideo;
}

void RecordingAnnotationOverlay::setLaserColor(const QColor &color)
{
    if (m_laserRenderer) {
        m_laserRenderer->setColor(color);
    }
}

void RecordingAnnotationOverlay::setCursorHighlightColor(const QColor &color)
{
    if (m_cursorHighlight) {
        m_cursorHighlight->setColor(color);
    }
}

void RecordingAnnotationOverlay::setCursorHighlightRadius(int radius)
{
    if (m_cursorHighlight) {
        m_cursorHighlight->setRadius(radius);
    }
}

void RecordingAnnotationOverlay::setSpotlightRadius(int radius)
{
    if (m_spotlightEffect) {
        m_spotlightEffect->setFollowRadius(radius);
    }
}

void RecordingAnnotationOverlay::setSpotlightDimOpacity(qreal opacity)
{
    if (m_spotlightEffect) {
        m_spotlightEffect->setDimOpacity(opacity);
    }
}

void RecordingAnnotationOverlay::onMouseClicked(QPoint globalPos)
{
    // Convert global position to local overlay coordinates
    QPoint localPos = globalPos - m_region.topLeft();

    // Only show ripple if click is within the recording region
    if (rect().contains(localPos)) {
        if (m_clickRipple && m_clickRipple->isEnabled()) {
            m_clickRipple->triggerRipple(localPos);
        }
    }
}

void RecordingAnnotationOverlay::onClickRippleNeedsRepaint()
{
    invalidateCache();
    update();
}

void RecordingAnnotationOverlay::onEffectNeedsRepaint()
{
    invalidateCache();
    update();
}

void RecordingAnnotationOverlay::updateCursorEffects(const QPoint &localPos)
{
    // Update cursor highlight position
    if (m_cursorHighlight && m_cursorHighlight->isEnabled()) {
        m_cursorHighlight->updateLocalPosition(localPos);
    }

    // Update spotlight position
    if (m_spotlightEffect && m_spotlightEffect->isEnabled()) {
        m_spotlightEffect->updateCursorPosition(localPos);
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

    // Layer order (bottom to top):

    // 1. Spotlight effect (dims surrounding area)
    if (m_spotlightEffect && m_spotlightEffect->hasVisibleContent()) {
        m_spotlightEffect->render(painter, rect());
    }

    // 2. Completed annotations
    if (m_annotationLayer) {
        m_annotationLayer->draw(painter);
    }

    // 3. In-progress annotation
    if (m_annotationController && m_annotationController->isDrawing()) {
        m_annotationController->drawCurrentAnnotation(painter);
    }

    // 4. Laser pointer trails
    if (m_laserRenderer && m_laserRenderer->hasVisiblePoints()) {
        m_laserRenderer->draw(painter);
    }

    // 5. Cursor highlight circle
    if (m_cursorHighlight && m_cursorHighlight->hasVisibleContent()) {
        m_cursorHighlight->render(painter);
    }

    // 6. Click ripple effects (top layer)
    if (m_clickRipple && m_clickRipple->hasActiveAnimations()) {
        m_clickRipple->draw(painter);
    }
}

void RecordingAnnotationOverlay::mousePressEvent(QMouseEvent *event)
{
    qDebug() << "RecordingAnnotationOverlay::mousePressEvent at" << event->pos()
             << "tool:" << static_cast<int>(m_annotationController ? m_annotationController->currentTool() : AnnotationController::Tool::None)
             << "laser:" << m_laserPointerEnabled;

    // Update cursor-following effects
    updateCursorEffects(event->pos());

    if (event->button() == Qt::LeftButton) {
        // Check if laser pointer is active
        if (m_laserPointerEnabled && m_laserRenderer) {
            m_laserRenderer->startDrawing(event->pos());
            invalidateCache();
            update();
            event->accept();
            return;
        }

        // Check if annotation tool is active
        if (m_annotationController && m_annotationController->currentTool() != AnnotationController::Tool::None) {
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
    // Update cursor-following effects
    updateCursorEffects(event->pos());

    // Handle laser pointer drawing
    if (m_laserRenderer && m_laserRenderer->isDrawing()) {
        m_laserRenderer->updateDrawing(event->pos());
        invalidateCache();
        update();
        event->accept();
        return;
    }

    // Handle annotation drawing
    if (m_annotationController && m_annotationController->isDrawing()) {
        m_annotationController->updateDrawing(event->pos());
        invalidateCache();
        update();
        event->accept();
        return;
    }

    // Even if not drawing, accept the event to keep tracking cursor position
    // for cursor highlight and spotlight effects
    if ((m_cursorHighlight && m_cursorHighlight->isEnabled()) ||
        (m_spotlightEffect && m_spotlightEffect->isEnabled())) {
        event->accept();
        return;
    }

    event->ignore();
}

void RecordingAnnotationOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // Handle laser pointer stop
        if (m_laserRenderer && m_laserRenderer->isDrawing()) {
            m_laserRenderer->stopDrawing();
            invalidateCache();
            update();
            event->accept();
            return;
        }

        // Handle annotation finish
        if (m_annotationController && m_annotationController->isDrawing()) {
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
