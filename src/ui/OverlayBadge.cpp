#include "ui/OverlayBadge.h"
#include "ui/DesignTokens.h"
#include "GlassRenderer.h"

#include <QPainter>
#include <QTimer>
#include <QPropertyAnimation>
#include <QFontMetrics>

namespace SnapTray {

OverlayBadge::OverlayBadge(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_TranslucentBackground);

    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    connect(m_hideTimer, &QTimer::timeout, this, &OverlayBadge::startFadeOut);

    m_fadeInAnim = new QPropertyAnimation(this, "fadeOpacity", this);
    m_fadeInAnim->setDuration(kFadeInMs);
    m_fadeInAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_fadeOutAnim = new QPropertyAnimation(this, "fadeOpacity", this);
    m_fadeOutAnim->setDuration(kFadeOutMs);
    m_fadeOutAnim->setEasingCurve(QEasingCurve::InCubic);
    connect(m_fadeOutAnim, &QAbstractAnimation::finished, this, [this]() {
        hide();
        m_visible = false;
    });

    hide();
}

void OverlayBadge::showBadge(const QString& text, int durationMs)
{
    m_fadeOutAnim->stop();

    // Calculate size from text
    QFont font;
    font.setPixelSize(kFontSize);
    QFontMetrics fm(font);
    int textWidth = fm.horizontalAdvance(text);
    int textHeight = fm.height();
    setFixedSize(textWidth + kPaddingH * 2, textHeight + kPaddingV * 2);

    m_text = text;

    if (!m_visible) {
        show();
        raise();
        startFadeIn();
        m_visible = true;
    } else {
        update(); // repaint with new text, no re-fade-in
    }

    m_hideTimer->start(durationMs);
}

void OverlayBadge::hideBadge()
{
    m_hideTimer->stop();
    startFadeOut();
}

void OverlayBadge::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setOpacity(m_fadeOpacity);

    const auto& tokens = DesignTokens::current();

    // Glass background
    GlassRenderer::drawGlassPanel(painter, rect(),
        tokens.badgeBackground,
        QColor(255, 255, 255, 10),   // subtle highlight
        tokens.toastBorder,           // reuse toast border for consistency
        kCornerRadius);

    // Centered text
    QFont font = painter.font();
    font.setPixelSize(kFontSize);
    painter.setFont(font);
    painter.setPen(tokens.badgeText);
    painter.drawText(rect(), Qt::AlignCenter, m_text);
}

void OverlayBadge::setFadeOpacity(qreal opacity)
{
    m_fadeOpacity = qBound(0.0, opacity, 1.0);
    update();
}

void OverlayBadge::startFadeIn()
{
    m_fadeInAnim->stop();
    m_fadeInAnim->setStartValue(0.0);
    m_fadeInAnim->setEndValue(1.0);
    m_fadeInAnim->start();
}

void OverlayBadge::startFadeOut()
{
    m_fadeOutAnim->stop();
    m_fadeOutAnim->setStartValue(m_fadeOpacity);
    m_fadeOutAnim->setEndValue(0.0);
    m_fadeOutAnim->start();
}

} // namespace SnapTray
