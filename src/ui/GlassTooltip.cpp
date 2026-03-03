#include "ui/GlassTooltip.h"
#include "GlassRenderer.h"

#include <QPainter>
#include <QScreen>
#include <QGuiApplication>
#include <QFontMetrics>

namespace SnapTray {

GlassTooltip::GlassTooltip(QWidget* parent)
    : QWidget(parent, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

void GlassTooltip::show(const QString& text, const ToolbarStyleConfig& style,
                         const QPoint& globalPos, bool above, bool showShadow)
{
    m_text = text;
    m_style = style;
    m_showShadow = showShadow;

    QFont f = font();
    f.setPointSize(11);
    setFont(f);

    const QSize tipSize = sizeHint();
    if (tipSize.isEmpty()) {
        hide();
        return;
    }
    resize(tipSize);

    // Position: center horizontally on globalPos.x, above or below globalPos.y
    int x = globalPos.x() - tipSize.width() / 2;
    int y = above
        ? globalPos.y() - tipSize.height() - 6
        : globalPos.y() + 6;

    // Clamp to screen bounds
    if (QScreen* screen = QGuiApplication::screenAt(globalPos)) {
        const QRect bounds = screen->availableGeometry();
        x = qBound(bounds.left() + 5, x, bounds.right() - tipSize.width() - 5);
    }

    move(x, y);
    QWidget::show();
    raise();
}

void GlassTooltip::hideTooltip()
{
    hide();
}

QSize GlassTooltip::sizeHint() const
{
    if (m_text.isEmpty()) {
        return QSize(0, 0);
    }
    QFontMetrics fm(font());
    QRect textRect = fm.boundingRect(m_text);
    textRect.adjust(-8, -4, 8, 4);
    return textRect.size();
}

void GlassTooltip::paintEvent(QPaintEvent* /*event*/)
{
    if (m_text.isEmpty()) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    ToolbarStyleConfig config = m_style;
    if (!m_showShadow) {
        config.shadowColor.setAlpha(0);
        config.shadowOffsetY = 0;
        config.shadowBlurRadius = 0;
    } else {
        config.shadowOffsetY = 2;
        config.shadowBlurRadius = 6;
    }
    GlassRenderer::drawGlassPanel(painter, rect(), config, 6);

    painter.setPen(m_style.tooltipText);
    painter.drawText(rect(), Qt::AlignCenter, m_text);
}

} // namespace SnapTray
