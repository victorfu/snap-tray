#include "LineWidthWidget.h"
#include "GlassRenderer.h"

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
    , m_styleConfig(ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle()))
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

void LineWidthWidget::updatePosition(const QRect& anchorRect, bool above, int screenWidth)
{
    int widgetX = anchorRect.left();
    int widgetY;

    if (above) {
        widgetY = anchorRect.top() - WIDGET_HEIGHT - 4;
    } else {
        widgetY = anchorRect.bottom() + 4;
    }

    // Keep on screen - left boundary
    if (widgetX < 5) widgetX = 5;

    // Keep on screen - right boundary
    if (screenWidth > 0) {
        int maxX = screenWidth - WIDGET_WIDTH - 5;
        if (widgetX > maxX) widgetX = maxX;
    }

    // Keep on screen - top boundary (fallback to below)
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

    // Draw glass panel (6px radius for sub-widgets)
    GlassRenderer::drawGlassPanel(painter, m_widgetRect, m_styleConfig, 6);

    // --- Slider Track ---
    // Use buttonInactiveColor for the track background (unfilled part)
    // Make it thinner (4px) for a modern look
    int trackHeight = 4;
    int trackY = m_sliderTrackRect.center().y() - trackHeight / 2;
    QRect trackRect(m_sliderTrackRect.left(), trackY, m_sliderTrackRect.width(), trackHeight);

    painter.setPen(Qt::NoPen);
    painter.setBrush(m_styleConfig.buttonInactiveColor);
    painter.drawRoundedRect(trackRect, trackHeight / 2, trackHeight / 2);

    // --- Slider Fill (Active part) ---
    // Use iconActiveColor (primary theme color) for the filled part
    int handleX = widthToPosition(m_currentWidth);
    int fillWidth = qMax(trackHeight, handleX - m_sliderTrackRect.left()); // Ensure at least a dot visible
    QRect fillRect(m_sliderTrackRect.left(), trackY, fillWidth, trackHeight);
    
    painter.setBrush(m_styleConfig.iconActiveColor);
    painter.drawRoundedRect(fillRect, trackHeight / 2, trackHeight / 2);

    // --- Handle ---
    int handleSize = 16; // Slightly larger for better touch/click target
    int handleY = m_sliderTrackRect.center().y();
    QRect handleRect(handleX - handleSize / 2, handleY - handleSize / 2,
                     handleSize, handleSize);

    // Handle Shadow (Soft drop shadow)
    painter.setBrush(m_styleConfig.shadowColor); 
    painter.drawEllipse(handleRect.adjusted(1, 1, 1, 1));

    // Handle Body
    // In Dark mode, white handle pops. In Light mode, white handle with border works too.
    painter.setBrush(Qt::white); 
    // Add subtle border for definition against light backgrounds
    painter.setPen(QPen(m_styleConfig.hairlineBorderColor, 1));
    painter.drawEllipse(handleRect);
    
    // Optional: Subtle border for handle in light mode to separate from white bg if needed
    // But since we are on a glass panel, pure white usually stands out well against the track.

    // --- Preview Circle ---
    // Draw preview circle (shows actual stroke width)
    int previewDiameter = qMin(m_currentWidth, PREVIEW_SIZE - 2);
    int previewCenterX = m_previewRect.center().x();
    int previewCenterY = m_previewRect.center().y();
    QRect previewCircle(previewCenterX - previewDiameter / 2,
                        previewCenterY - previewDiameter / 2,
                        previewDiameter, previewDiameter);

    painter.setPen(Qt::NoPen);
    painter.setBrush(m_previewColor);
    painter.drawEllipse(previewCircle);

    // --- Width Label ---
    painter.setPen(m_styleConfig.textColor);
    QFont font = painter.font();
    font.setPointSize(10); // Slightly smaller for cleaner look
    font.setWeight(QFont::Medium);
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
