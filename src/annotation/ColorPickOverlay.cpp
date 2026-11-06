#include "annotation/ColorPickOverlay.h"

#include "cursor/CursorManager.h"
#include "platform/WindowLevel.h"
#include "region/MagnifierPanel.h"

#include <QAccessible>
#include <QHash>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPointer>
#include <QTimer>
#include <QtMath>

#include <utility>

bool ColorPickSource::isValid() const
{
    return overlayGeometry.isValid() && sampleRect.isValid() && !image.isNull()
        && overlayGeometry.intersects(sampleRect);
}

namespace ColorPick {

QPoint pixelAt(const QPointF& logicalInSample, qreal devicePixelRatio, const QSize& imageSize)
{
    const qreal dpr = devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;
    const QPoint pixel(qFloor(logicalInSample.x() * dpr), qFloor(logicalInSample.y() * dpr));
    if (!QRect(QPoint(0, 0), imageSize).contains(pixel)) {
        return QPoint(-1, -1);
    }
    return pixel;
}

QPoint nudged(const QPoint& pixel, const QPoint& delta, const QSize& imageSize)
{
    if (imageSize.isEmpty()) {
        return pixel;
    }
    return QPoint(qBound(0, pixel.x() + delta.x(), imageSize.width() - 1),
                  qBound(0, pixel.y() + delta.y(), imageSize.height() - 1));
}

QColor colorAt(const QImage& image, const QPoint& pixel)
{
    if (!image.valid(pixel)) {
        return QColor();
    }
    // pixelColor un-premultiplies; a screenshot is opaque, so dropping alpha
    // only matters for transparent pin corners, which should not yield an
    // invisible annotation color.
    QColor color = image.pixelColor(pixel).toRgb();
    color.setAlpha(255);
    return color;
}

QString hexName(const QColor& color)
{
    return color.name(QColor::HexRgb).toUpper();
}

}  // namespace ColorPick

namespace {
// Keys move the pick point by one physical pixel.
const QHash<int, QPoint>& nudgeKeys()
{
    static const QHash<int, QPoint> keys{
        {Qt::Key_Left, QPoint(-1, 0)},
        {Qt::Key_Right, QPoint(1, 0)},
        {Qt::Key_Up, QPoint(0, -1)},
        {Qt::Key_Down, QPoint(0, 1)},
    };
    return keys;
}
}  // namespace

ColorPickOverlay::ColorPickOverlay(ColorPickSource source)
    : QWidget(nullptr,
              Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                  | Qt::NoDropShadowWindowHint)
    , m_source(std::move(source))
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAccessibleName(tr("Pick color from screen"));

    m_samplePixmap = QPixmap::fromImage(m_source.image);
    m_sampleOrigin = m_source.sampleRect.topLeft() - m_source.overlayGeometry.topLeft();

    m_magnifier = new MagnifierPanel(this);
    m_magnifier->setDevicePixelRatio(m_source.image.devicePixelRatio());
    m_magnifier->setShowHexColor(true);
    m_magnifier->setHintLines(tr("Click or Enter: Use color"),
                              tr("Arrow keys: 1 px, Esc: Cancel"));

    setGeometry(m_source.overlayGeometry);
    CursorManager::instance().registerWidget(this);
}

ColorPickOverlay::~ColorPickOverlay()
{
    CursorManager::instance().unregisterWidget(this);
}

ColorPickOverlay* ColorPickOverlay::begin(QWidget* host, ColorPickSource source,
                                          std::function<void(const QColor&)> onPicked)
{
    if (!source.isValid()) {
        qWarning() << "ColorPickOverlay: no image to pick from";
        return nullptr;
    }

    auto* overlay = new ColorPickOverlay(std::move(source));
    overlay->setAttribute(Qt::WA_DeleteOnClose);
    const QPointer<QWidget> hostGuard(host);
    auto restoreHost = [hostGuard] {
        if (!hostGuard || !hostGuard->isVisible()) {
            return;
        }
        hostGuard->raise();
        hostGuard->activateWindow();
#ifdef Q_OS_LINUX
        requestNativeWindowFocus(hostGuard);
#endif
    };
    QObject::connect(overlay, &ColorPickOverlay::colorPicked, overlay,
                     [hostGuard, onPicked = std::move(onPicked), restoreHost](const QColor& color) {
                         restoreHost();
                         if (!hostGuard) {
                             return;
                         }
                         if (onPicked) {
                             onPicked(color);
                         }
                         QAccessibleAnnouncementEvent announcement(
                             hostGuard.data(),
                             tr("Color set to %1").arg(ColorPick::hexName(color)));
                         QAccessible::updateAccessibility(&announcement);
                     });
    QObject::connect(overlay, &ColorPickOverlay::cancelled, overlay, restoreHost);
    // A host that goes away mid-pick takes the overlay with it.
    if (host) {
        QObject::connect(host, &QObject::destroyed, overlay, &ColorPickOverlay::cancel);
    }

    overlay->show();
    overlay->raise();
    overlay->activateWindow();
    overlay->setFocus(Qt::OtherFocusReason);
#ifdef Q_OS_LINUX
    requestNativeWindowFocus(overlay);
#endif
    overlay->trackPointer(QPointF(overlay->mapFromGlobal(QCursor::pos())));
    return overlay;
}

QColor ColorPickOverlay::currentColor() const
{
    return m_hasSample ? ColorPick::colorAt(m_source.image, m_pixel) : QColor();
}

void ColorPickOverlay::cancel()
{
    finish(false);
}

void ColorPickOverlay::trackPointer(const QPointF& localPos)
{
    const QPoint pixel = ColorPick::pixelAt(localPos - QPointF(m_sampleOrigin),
                                            m_source.image.devicePixelRatio(),
                                            m_source.image.size());
    setSample(pixel.x() >= 0, pixel);
}

void ColorPickOverlay::setSample(bool hasSample, const QPoint& pixel)
{
    if (hasSample == m_hasSample && (!hasSample || pixel == m_pixel)) {
        return;
    }
    // Repaint only where the magnifier and reticle were and now are; a
    // full-screen translucent repaint per pointer move is costly at 4K.
    const QRect before = paintedRect();
    m_hasSample = hasSample;
    if (hasSample) {
        m_pixel = pixel;
    }
    syncCursor();
    update(before.united(paintedRect()));
}

QRect ColorPickOverlay::paintedRect() const
{
    if (!m_hasSample) {
        return QRect();
    }
    const QPointF center = pixelCenterLocal();
    const int reach = kReticleGap + kReticleArm + kReticlePenWidth;
    const QRect reticle(qFloor(center.x()) - reach, qFloor(center.y()) - reach,
                        2 * reach + 1, 2 * reach + 1);
    return m_dirtyPlanner.magnifierRectForCursor(center.toPoint(), size()).united(reticle);
}

QPointF ColorPickOverlay::pixelCenterLocal() const
{
    const qreal dpr = m_source.image.devicePixelRatio() > 0.0
        ? m_source.image.devicePixelRatio()
        : 1.0;
    return QPointF(m_sampleOrigin) + (QPointF(m_pixel) + QPointF(0.5, 0.5)) / dpr;
}

void ColorPickOverlay::syncCursor()
{
    // Inside the sample area the reticle replaces the pointer, so it can
    // follow arrow-key moves smaller than one logical pixel.
    if (m_hasSample == m_cursorHidden) {
        return;
    }
    auto& cursors = CursorManager::instance();
    if (m_hasSample) {
        cursors.pushCursorForWidget(this, CursorContext::Override, Qt::BlankCursor);
    } else {
        cursors.popCursorForWidget(this, CursorContext::Override);
    }
    m_cursorHidden = m_hasSample;
}

void ColorPickOverlay::finish(bool accepted)
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    releaseKeyboard();
    const QColor color = currentColor();
    hide();
    if (accepted && color.isValid()) {
        emit colorPicked(color);
    } else {
        emit cancelled();
    }
    close();
}

void ColorPickOverlay::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setClipRegion(event->region());
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), QColor(0, 0, 0, kHitTestAlpha));
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    if (!m_hasSample) {
        return;
    }

    const QPointF center = pixelCenterLocal();
    m_magnifier->drawAt(painter, center.toPoint(), m_pixel, size(), m_samplePixmap);

    // Reticle: a two-tone cross with a gap, readable on any background.
    painter.setRenderHint(QPainter::Antialiasing, false);
    const qreal arm = kReticleArm;
    const qreal gap = kReticleGap;
    const QLineF lines[] = {
        QLineF(center.x() - gap - arm, center.y(), center.x() - gap, center.y()),
        QLineF(center.x() + gap, center.y(), center.x() + gap + arm, center.y()),
        QLineF(center.x(), center.y() - gap - arm, center.x(), center.y() - gap),
        QLineF(center.x(), center.y() + gap, center.x(), center.y() + gap + arm),
    };
    painter.setPen(QPen(Qt::black, kReticlePenWidth));
    painter.drawLines(lines, 4);
    painter.setPen(QPen(Qt::white, 1));
    painter.drawLines(lines, 4);
}

void ColorPickOverlay::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    setWindowExcludedFromCapture(this, true);
    raiseWindowAboveOverlays(this);
    raise();
    // Keys must reach the overlay even if the window manager keeps focus on
    // the host. Grab once the window is mapped; X11 refuses earlier grabs.
    QTimer::singleShot(0, this, [this] {
        if (!m_finished && isVisible()) {
            grabKeyboard();
        }
    });
}

void ColorPickOverlay::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        cancel();
        return;
    }
    trackPointer(event->position());
    if (!m_hasSample) {
        cancel();
        return;
    }
    m_pressed = true;
}

void ColorPickOverlay::mouseMoveEvent(QMouseEvent* event)
{
    trackPointer(event->position());
}

void ColorPickOverlay::mouseReleaseEvent(QMouseEvent* event)
{
    // Only a press that began here counts; press, drag to fine-tune, release.
    if (event->button() != Qt::LeftButton || !m_pressed) {
        return;
    }
    m_pressed = false;
    trackPointer(event->position());
    finish(m_hasSample);
}

void ColorPickOverlay::keyPressEvent(QKeyEvent* event)
{
    const int key = event->key();
    if (key == Qt::Key_Escape) {
        cancel();
        return;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        if (m_hasSample) {
            finish(true);
        }
        return;
    }
    const auto nudge = nudgeKeys().constFind(key);
    if (nudge != nudgeKeys().constEnd() && m_hasSample) {
        setSample(true, ColorPick::nudged(m_pixel, *nudge, m_source.image.size()));
        return;
    }
    event->accept();
}

bool ColorPickOverlay::event(QEvent* event)
{
    // Leaving for another window (Alt+Tab, a click elsewhere) ends the pick,
    // so the host never stays behind an invisible input sink.
    if (event->type() == QEvent::WindowActivate) {
        m_wasActive = true;
    } else if (event->type() == QEvent::WindowDeactivate && m_wasActive) {
        cancel();
    }
    return QWidget::event(event);
}
