#include "RecordingAnnotationOverlay.h"
#include "ClickRippleRenderer.h"
#include "input/MouseClickTracker.h"
#include "platform/WindowLevel.h"
#include "LaserPointerRenderer.h"
#include "recording/CursorHighlightEffect.h"
#include "recording/SpotlightEffect.h"

#include <QPainter>
#include <QMouseEvent>
#include <QCursor>
#include <QDebug>

RecordingAnnotationOverlay::RecordingAnnotationOverlay(QWidget *parent)
    : QWidget(parent)
    , m_cacheInvalid(true)
    , m_clickRipple(nullptr)
    , m_clickTracker(nullptr)
    , m_laserRenderer(nullptr)
    , m_cursorHighlight(nullptr)
    , m_spotlightEffect(nullptr)
{
    setupWindow();

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

void RecordingAnnotationOverlay::compositeOntoFrame(QImage &frame, qreal scale) const
{
    bool hasRipples = m_clickRipple && m_clickRipple->hasActiveAnimations();
    bool hasLaser = m_laserRenderer && m_laserRenderer->hasVisiblePoints();
    bool hasCursorHighlight = m_cursorHighlight && m_cursorHighlight->hasVisibleContent();
    bool hasSpotlight = m_spotlightEffect && m_spotlightEffect->hasVisibleContent();

    bool compositeIndicators = m_compositeIndicatorsToVideo;
    bool hasDynamicEffects = compositeIndicators && (hasSpotlight || hasLaser || hasCursorHighlight);

    if (!hasRipples && !hasDynamicEffects) {
        return;
    }

    QSize scaledSize = frame.size();

    // === Part 1: Spotlight (bottom layer, drawn directly) ===
    // Spotlight dims surrounding area and must be below other content
    if (compositeIndicators && hasSpotlight) {
        QPainter framePainter(&frame);
        framePainter.setRenderHint(QPainter::Antialiasing);
        if (scale != 1.0) {
            framePainter.scale(scale, scale);
        }
        m_spotlightEffect->render(framePainter, rect());
    }

    // === Part 2: Click ripple cache ===
    // Only rebuild cache when ripples change
    if (hasRipples) {
        if (m_cacheInvalid || m_cacheSize != scaledSize) {
            m_rippleCache = QImage(scaledSize, QImage::Format_ARGB32_Premultiplied);
            m_rippleCache.fill(Qt::transparent);

            {
                QPainter cachePainter(&m_rippleCache);
                cachePainter.setRenderHint(QPainter::Antialiasing);
                if (scale != 1.0) {
                    cachePainter.scale(scale, scale);
                }
                m_clickRipple->draw(cachePainter);
            }
            m_cacheSize = scaledSize;
            m_cacheInvalid = false;
        }

        // Composite ripple cache onto frame
        QPainter framePainter(&frame);
        framePainter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        framePainter.drawImage(0, 0, m_rippleCache);
    }

    // === Part 3: Cursor-following effects (top layer, drawn directly) ===
    // These are drawn directly without caching to avoid cache invalidation on cursor movement
    if (compositeIndicators && (hasLaser || hasCursorHighlight)) {
        QPainter framePainter(&frame);
        framePainter.setRenderHint(QPainter::Antialiasing);
        if (scale != 1.0) {
            framePainter.scale(scale, scale);
        }

        // Laser pointer trails
        if (hasLaser) {
            m_laserRenderer->draw(framePainter);
        }
        // Cursor highlight circle
        if (hasCursorHighlight) {
            m_cursorHighlight->render(framePainter);
        }
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
    // Only trigger UI repaint for the overlay window, do not invalidate compositing cache.
    // Cursor-following effects (spotlight, cursor highlight, laser pointer) are now drawn
    // directly in compositeOntoFrame() without using the cache.
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

    // 2. Laser pointer trails
    if (m_laserRenderer && m_laserRenderer->hasVisiblePoints()) {
        m_laserRenderer->draw(painter);
    }

    // 3. Cursor highlight circle
    if (m_cursorHighlight && m_cursorHighlight->hasVisibleContent()) {
        m_cursorHighlight->render(painter);
    }

    // 4. Click ripple effects (top layer)
    if (m_clickRipple && m_clickRipple->hasActiveAnimations()) {
        m_clickRipple->draw(painter);
    }
}

void RecordingAnnotationOverlay::mousePressEvent(QMouseEvent *event)
{
    // Update cursor-following effects
    updateCursorEffects(event->pos());

    if (event->button() == Qt::LeftButton) {
        // Check if laser pointer is active
        if (m_laserPointerEnabled && m_laserRenderer) {
            m_laserRenderer->startDrawing(event->pos());
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
            update();
            event->accept();
            return;
        }
    }
    event->ignore();
}

#include "moc_RecordingAnnotationOverlay.cpp"
