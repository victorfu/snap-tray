#include "LineWidthWidget.h"

#include <QPainter>
#include <QDebug>

LineWidthWidget::LineWidthWidget(QObject* parent)
    : QObject(parent)
    , m_minWidth(1)
    , m_maxWidth(20)
    , m_currentWidth(3)
    , m_visible(false)
    , m_isDragging(false)
    , m_hovered(false)
    , m_previewColor(Qt::red)
{
}

void LineWidthWidget::setWidthRange(int min, int max)
{
    m_minWidth = min;
    m_maxWidth = max;
    if (m_currentWidth < m_minWidth) m_currentWidth = m_minWidth;
    if (m_currentWidth > m_maxWidth) m_currentWidth = m_maxWidth;
}

void LineWidthWidget::setCurrentWidth(int width)
{
    m_currentWidth = qBound(m_minWidth, width, m_maxWidth);
}

void LineWidthWidget::updatePosition(const QRect& anchorRect, bool above)
{
    int widgetX = anchorRect.left();
    int widgetY;

    if (above) {
        widgetY = anchorRect.top() - WIDGET_HEIGHT - 4;
    } else {
        widgetY = anchorRect.bottom() + 4;
    }

    // Keep on screen (basic bounds check)
    if (widgetX < 5) widgetX = 5;
    if (widgetY < 5 && above) {
        widgetY = anchorRect.bottom() + 4;
    }

    m_widgetRect = QRect(widgetX, widgetY, WIDGET_WIDTH, WIDGET_HEIGHT);
    updateLayout();
}

void LineWidthWidget::updateLayout()
{
    // Preview circle on the left
    int previewX = m_widgetRect.left() + PADDING;
    int previewY = m_widgetRect.top() + (WIDGET_HEIGHT - PREVIEW_SIZE) / 2;
    m_previewRect = QRect(previewX, previewY, PREVIEW_SIZE, PREVIEW_SIZE);

    // Label on the right (e.g., "5 px")
    int labelWidth = 36;
    int labelX = m_widgetRect.right() - PADDING - labelWidth;
    m_labelRect = QRect(labelX, m_widgetRect.top(), labelWidth, WIDGET_HEIGHT);

    // Slider track in the middle
    int trackLeft = m_previewRect.right() + PADDING;
    int trackRight = m_labelRect.left() - PADDING;
    int trackY = m_widgetRect.top() + (WIDGET_HEIGHT - SLIDER_HEIGHT) / 2;
    m_sliderTrackRect = QRect(trackLeft, trackY, trackRight - trackLeft, SLIDER_HEIGHT);
}

void LineWidthWidget::draw(QPainter& painter)
{
    if (!m_visible) return;

    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw shadow
    QRect shadowRect = m_widgetRect.adjusted(2, 2, 2, 2);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 50));
    painter.drawRoundedRect(shadowRect, 6, 6);

    // Draw background with gradient
    QLinearGradient gradient(m_widgetRect.topLeft(), m_widgetRect.bottomLeft());
    gradient.setColorAt(0, QColor(55, 55, 55, 245));
    gradient.setColorAt(1, QColor(40, 40, 40, 245));

    painter.setBrush(gradient);
    painter.setPen(QPen(QColor(70, 70, 70), 1));
    painter.drawRoundedRect(m_widgetRect, 6, 6);

    // Draw slider track background
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(30, 30, 30));
    painter.drawRoundedRect(m_sliderTrackRect, SLIDER_HEIGHT / 2, SLIDER_HEIGHT / 2);

    // Draw slider track fill (from left to handle position)
    int handleX = widthToPosition(m_currentWidth);
    QRect fillRect(m_sliderTrackRect.left(), m_sliderTrackRect.top(),
                   handleX - m_sliderTrackRect.left(), SLIDER_HEIGHT);
    painter.setBrush(QColor(0, 174, 255));
    painter.drawRoundedRect(fillRect, SLIDER_HEIGHT / 2, SLIDER_HEIGHT / 2);

    // Draw handle
    int handleY = m_sliderTrackRect.center().y();
    QRect handleRect(handleX - HANDLE_SIZE / 2, handleY - HANDLE_SIZE / 2,
                     HANDLE_SIZE, HANDLE_SIZE);

    // Handle shadow
    painter.setBrush(QColor(0, 0, 0, 30));
    painter.drawEllipse(handleRect.adjusted(1, 1, 1, 1));

    // Handle fill
    painter.setBrush(Qt::white);
    painter.setPen(QPen(QColor(0, 174, 255), 2));
    painter.drawEllipse(handleRect);

    // Draw preview circle (shows actual stroke width)
    int previewDiameter = qMin(m_currentWidth, PREVIEW_SIZE - 4);
    int previewCenterX = m_previewRect.center().x();
    int previewCenterY = m_previewRect.center().y();
    QRect previewCircle(previewCenterX - previewDiameter / 2,
                        previewCenterY - previewDiameter / 2,
                        previewDiameter, previewDiameter);

    painter.setPen(Qt::NoPen);
    painter.setBrush(m_previewColor);
    painter.drawEllipse(previewCircle);

    // Draw width label
    painter.setPen(QColor(180, 180, 180));
    QFont font = painter.font();
    font.setPointSize(11);
    painter.setFont(font);
    QString label = QString("%1 px").arg(m_currentWidth);
    painter.drawText(m_labelRect, Qt::AlignCenter, label);
}

bool LineWidthWidget::contains(const QPoint& pos) const
{
    return m_widgetRect.contains(pos);
}

bool LineWidthWidget::handleMousePress(const QPoint& pos)
{
    if (!m_visible || !m_widgetRect.contains(pos)) {
        return false;
    }

    // Check if clicking on or near the slider track
    QRect sliderArea = m_sliderTrackRect.adjusted(-HANDLE_SIZE / 2, -HANDLE_SIZE,
                                                   HANDLE_SIZE / 2, HANDLE_SIZE);
    if (sliderArea.contains(pos)) {
        m_isDragging = true;
        int newWidth = positionToWidth(pos.x());
        if (newWidth != m_currentWidth) {
            m_currentWidth = newWidth;
            emit widthChanged(m_currentWidth);
            qDebug() << "LineWidthWidget: Width changed to" << m_currentWidth;
        }
        return true;
    }

    return true; // Consume click if on widget
}

bool LineWidthWidget::handleMouseMove(const QPoint& pos, bool pressed)
{
    if (!m_visible) return false;

    bool needsUpdate = false;

    if (m_isDragging && pressed) {
        int newWidth = positionToWidth(pos.x());
        if (newWidth != m_currentWidth) {
            m_currentWidth = newWidth;
            emit widthChanged(m_currentWidth);
            needsUpdate = true;
        }
    }

    // Update hover state
    bool wasHovered = m_hovered;
    m_hovered = m_widgetRect.contains(pos);
    if (wasHovered != m_hovered) {
        needsUpdate = true;
    }

    return needsUpdate;
}

bool LineWidthWidget::handleMouseRelease(const QPoint& pos)
{
    Q_UNUSED(pos);

    if (m_isDragging) {
        m_isDragging = false;
        return true;
    }
    return false;
}

bool LineWidthWidget::updateHovered(const QPoint& pos)
{
    bool wasHovered = m_hovered;
    m_hovered = m_widgetRect.contains(pos);
    return wasHovered != m_hovered;
}

int LineWidthWidget::positionToWidth(int x) const
{
    int trackLeft = m_sliderTrackRect.left();
    int trackRight = m_sliderTrackRect.right();

    if (x <= trackLeft) return m_minWidth;
    if (x >= trackRight) return m_maxWidth;

    double ratio = static_cast<double>(x - trackLeft) / (trackRight - trackLeft);
    int width = m_minWidth + static_cast<int>(ratio * (m_maxWidth - m_minWidth) + 0.5);
    return qBound(m_minWidth, width, m_maxWidth);
}

int LineWidthWidget::widthToPosition(int width) const
{
    int trackLeft = m_sliderTrackRect.left();
    int trackRight = m_sliderTrackRect.right();

    double ratio = static_cast<double>(width - m_minWidth) / (m_maxWidth - m_minWidth);
    return trackLeft + static_cast<int>(ratio * (trackRight - trackLeft));
}
