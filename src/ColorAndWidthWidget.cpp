#include "ColorAndWidthWidget.h"

#include <QPainter>
#include <QDebug>

ColorAndWidthWidget::ColorAndWidthWidget(QObject* parent)
    : QObject(parent)
    , m_currentColor(Qt::red)
    , m_showMoreButton(true)
    , m_hoveredSwatch(-1)
    , m_minWidth(1)
    , m_maxWidth(20)
    , m_currentWidth(3)
    , m_isDragging(false)
    , m_widthSectionHovered(false)
    , m_visible(false)
{
    // Default color palette (same as ColorPaletteWidget)
    m_colors = {
        Qt::red,
        QColor(0, 122, 255),      // Blue (system blue)
        QColor(52, 199, 89),      // Green (system green)
        QColor(255, 204, 0),      // Yellow
        QColor(255, 149, 0),      // Orange
        QColor(175, 82, 222),     // Purple
        Qt::black,
        Qt::white
    };
}

void ColorAndWidthWidget::setColors(const QVector<QColor>& colors)
{
    m_colors = colors;
}

void ColorAndWidthWidget::setCurrentColor(const QColor& color)
{
    m_currentColor = color;
}

void ColorAndWidthWidget::setWidthRange(int min, int max)
{
    m_minWidth = min;
    m_maxWidth = max;
    if (m_currentWidth < m_minWidth) m_currentWidth = m_minWidth;
    if (m_currentWidth > m_maxWidth) m_currentWidth = m_maxWidth;
}

void ColorAndWidthWidget::setCurrentWidth(int width)
{
    m_currentWidth = qBound(m_minWidth, width, m_maxWidth);
}

void ColorAndWidthWidget::updatePosition(const QRect& anchorRect, bool above, int screenWidth)
{
    // Calculate total width
    int swatchCount = m_colors.size() + (m_showMoreButton ? 1 : 0);
    int colorSectionWidth = swatchCount * SWATCH_SIZE + (swatchCount - 1) * SWATCH_SPACING + SECTION_PADDING * 2;
    int totalWidth = colorSectionWidth + SECTION_SPACING + SLIDER_WIDTH;

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
        int maxX = screenWidth - totalWidth - 5;
        if (widgetX > maxX) widgetX = maxX;
    }

    // Keep on screen - top boundary (fallback to below)
    if (widgetY < 5 && above) {
        widgetY = anchorRect.bottom() + 4;
    }

    m_widgetRect = QRect(widgetX, widgetY, totalWidth, WIDGET_HEIGHT);
    updateLayout();
}

void ColorAndWidthWidget::updateLayout()
{
    // === COLOR SECTION ===
    int swatchCount = m_colors.size() + (m_showMoreButton ? 1 : 0);
    int colorSectionWidth = swatchCount * SWATCH_SIZE + (swatchCount - 1) * SWATCH_SPACING + SECTION_PADDING * 2;

    m_colorSectionRect = QRect(
        m_widgetRect.left(),
        m_widgetRect.top(),
        colorSectionWidth,
        WIDGET_HEIGHT
    );

    // Calculate swatch positions
    m_swatchRects.resize(swatchCount);
    int x = m_colorSectionRect.left() + SECTION_PADDING;
    int y = m_colorSectionRect.top() + (WIDGET_HEIGHT - SWATCH_SIZE) / 2;
    for (int i = 0; i < swatchCount; ++i) {
        m_swatchRects[i] = QRect(x, y, SWATCH_SIZE, SWATCH_SIZE);
        x += SWATCH_SIZE + SWATCH_SPACING;
    }

    // === WIDTH SECTION ===
    m_widthSectionRect = QRect(
        m_colorSectionRect.right() + SECTION_SPACING,
        m_widgetRect.top(),
        SLIDER_WIDTH,
        WIDGET_HEIGHT
    );

    // Preview circle (left side of width section)
    int previewX = m_widthSectionRect.left() + SECTION_PADDING;
    int previewY = m_widthSectionRect.top() + (WIDGET_HEIGHT - PREVIEW_SIZE) / 2;
    m_previewRect = QRect(previewX, previewY, PREVIEW_SIZE, PREVIEW_SIZE);

    // Label (right side of width section)
    int labelWidth = 36;
    int labelX = m_widthSectionRect.right() - SECTION_PADDING - labelWidth;
    m_labelRect = QRect(labelX, m_widthSectionRect.top(), labelWidth, WIDGET_HEIGHT);

    // Slider track (middle of width section)
    int trackLeft = m_previewRect.right() + SECTION_PADDING;
    int trackRight = m_labelRect.left() - SECTION_PADDING;
    int trackY = m_widthSectionRect.top() + (WIDGET_HEIGHT - SLIDER_HEIGHT) / 2;
    m_sliderTrackRect = QRect(trackLeft, trackY, trackRight - trackLeft, SLIDER_HEIGHT);
}

void ColorAndWidthWidget::draw(QPainter& painter)
{
    if (!m_visible) return;

    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw unified shadow
    QRect shadowRect = m_widgetRect.adjusted(2, 2, 2, 2);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 50));
    painter.drawRoundedRect(shadowRect, 6, 6);

    // Draw unified background with gradient
    QLinearGradient gradient(m_widgetRect.topLeft(), m_widgetRect.bottomLeft());
    gradient.setColorAt(0, QColor(55, 55, 55, 245));
    gradient.setColorAt(1, QColor(40, 40, 40, 245));
    painter.setBrush(gradient);
    painter.setPen(QPen(QColor(70, 70, 70), 1));
    painter.drawRoundedRect(m_widgetRect, 6, 6);

    // Draw color section
    drawColorSection(painter);

    // Draw width section
    drawWidthSection(painter);
}

void ColorAndWidthWidget::drawColorSection(QPainter& painter)
{
    // Draw color swatches (adapted from ColorPaletteWidget)
    for (int i = 0; i < m_colors.size(); ++i) {
        QRect swatchRect = m_swatchRects[i];

        // Highlight if hovered
        if (i == m_hoveredSwatch) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(80, 80, 80));
            painter.drawRoundedRect(swatchRect.adjusted(-2, -2, 2, 2), 4, 4);
        }

        // Draw color circle
        int circleSize = swatchRect.width() - 6;
        QRect circleRect(swatchRect.center().x() - circleSize / 2,
                         swatchRect.center().y() - circleSize / 2,
                         circleSize, circleSize);

        // Draw border (white for dark colors, dark for light colors)
        QColor borderColor = (m_colors[i].lightness() > 200) ? QColor(80, 80, 80) : Qt::white;
        painter.setPen(QPen(borderColor, 1));
        painter.setBrush(m_colors[i]);
        painter.drawEllipse(circleRect);

        // Draw selection indicator for current color
        if (m_colors[i] == m_currentColor) {
            painter.setPen(QPen(QColor(0, 174, 255), 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(circleRect.adjusted(-3, -3, 3, 3));
        }
    }

    // Draw "more colors" button (last swatch position)
    if (m_showMoreButton) {
        int moreIdx = m_colors.size();
        QRect moreRect = m_swatchRects[moreIdx];

        if (moreIdx == m_hoveredSwatch) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(80, 80, 80));
            painter.drawRoundedRect(moreRect.adjusted(-2, -2, 2, 2), 4, 4);
        }

        // Draw "..." text
        painter.setPen(QColor(180, 180, 180));
        QFont font = painter.font();
        font.setPointSize(12);
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(moreRect, Qt::AlignCenter, "...");
    }
}

void ColorAndWidthWidget::drawWidthSection(QPainter& painter)
{
    // Draw slider track background (adapted from LineWidthWidget)
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

    // Draw preview circle (shows actual stroke width with current color)
    int previewDiameter = qMin(m_currentWidth, PREVIEW_SIZE - 4);
    int previewCenterX = m_previewRect.center().x();
    int previewCenterY = m_previewRect.center().y();
    QRect previewCircle(previewCenterX - previewDiameter / 2,
                        previewCenterY - previewDiameter / 2,
                        previewDiameter, previewDiameter);

    painter.setPen(Qt::NoPen);
    painter.setBrush(m_currentColor);  // Use current color for preview
    painter.drawEllipse(previewCircle);

    // Draw width label
    painter.setPen(QColor(180, 180, 180));
    QFont font = painter.font();
    font.setPointSize(11);
    painter.setFont(font);
    QString label = QString("%1 px").arg(m_currentWidth);
    painter.drawText(m_labelRect, Qt::AlignCenter, label);
}

bool ColorAndWidthWidget::contains(const QPoint& pos) const
{
    return m_widgetRect.contains(pos);
}

int ColorAndWidthWidget::swatchAtPosition(const QPoint& pos) const
{
    if (!m_colorSectionRect.contains(pos)) return -1;

    for (int i = 0; i < m_swatchRects.size(); ++i) {
        if (m_swatchRects[i].contains(pos)) {
            return i;
        }
    }
    return -1;
}

bool ColorAndWidthWidget::isInWidthSection(const QPoint& pos) const
{
    return m_widthSectionRect.contains(pos);
}

bool ColorAndWidthWidget::handleClick(const QPoint& pos)
{
    if (!m_widgetRect.contains(pos)) return false;

    // Priority 1: Check width section (slider has precedence for UX)
    if (isInWidthSection(pos)) {
        // Handle slider click (same logic as LineWidthWidget)
        QRect sliderArea = m_sliderTrackRect.adjusted(-HANDLE_SIZE / 2, -HANDLE_SIZE,
                                                       HANDLE_SIZE / 2, HANDLE_SIZE);
        if (sliderArea.contains(pos)) {
            m_isDragging = true;
            int newWidth = positionToWidth(pos.x());
            if (newWidth != m_currentWidth) {
                m_currentWidth = newWidth;
                emit widthChanged(m_currentWidth);
                qDebug() << "ColorAndWidthWidget: Width changed to" << m_currentWidth;
            }
            return true;
        }
        return true; // Consume click even if not on slider
    }

    // Priority 2: Check color section
    int swatchIdx = swatchAtPosition(pos);
    if (swatchIdx >= 0) {
        if (swatchIdx < m_colors.size()) {
            m_currentColor = m_colors[swatchIdx];
            emit colorSelected(m_currentColor);
            qDebug() << "ColorAndWidthWidget: Color selected:" << m_currentColor.name();
        } else if (m_showMoreButton) {
            emit moreColorsRequested();
        }
        return true;
    }

    return false;
}

bool ColorAndWidthWidget::handleMousePress(const QPoint& pos)
{
    // Delegate to handleClick for unified handling
    return handleClick(pos);
}

bool ColorAndWidthWidget::handleMouseMove(const QPoint& pos, bool pressed)
{
    if (!m_visible) return false;

    bool needsUpdate = false;

    // Handle slider dragging
    if (m_isDragging && pressed) {
        int newWidth = positionToWidth(pos.x());
        if (newWidth != m_currentWidth) {
            m_currentWidth = newWidth;
            emit widthChanged(m_currentWidth);
            needsUpdate = true;
        }
        return needsUpdate; // Don't update hover during drag
    }

    // Update hover states when not dragging
    if (updateHovered(pos)) {
        needsUpdate = true;
    }

    return needsUpdate;
}

bool ColorAndWidthWidget::handleMouseRelease(const QPoint& pos)
{
    Q_UNUSED(pos);

    if (m_isDragging) {
        m_isDragging = false;
        return true;
    }
    return false;
}

bool ColorAndWidthWidget::updateHovered(const QPoint& pos)
{
    bool changed = false;

    // Update color swatch hover
    int newHoveredSwatch = swatchAtPosition(pos);
    if (newHoveredSwatch != m_hoveredSwatch) {
        m_hoveredSwatch = newHoveredSwatch;
        changed = true;
    }

    // Update width section hover
    bool inWidthSection = isInWidthSection(pos);
    if (inWidthSection != m_widthSectionHovered) {
        m_widthSectionHovered = inWidthSection;
        changed = true;
    }

    return changed;
}

int ColorAndWidthWidget::positionToWidth(int x) const
{
    int trackLeft = m_sliderTrackRect.left();
    int trackRight = m_sliderTrackRect.right();

    if (x <= trackLeft) return m_minWidth;
    if (x >= trackRight) return m_maxWidth;

    double ratio = static_cast<double>(x - trackLeft) / (trackRight - trackLeft);
    int width = m_minWidth + static_cast<int>(ratio * (m_maxWidth - m_minWidth) + 0.5);
    return qBound(m_minWidth, width, m_maxWidth);
}

int ColorAndWidthWidget::widthToPosition(int width) const
{
    int trackLeft = m_sliderTrackRect.left();
    int trackRight = m_sliderTrackRect.right();

    double ratio = static_cast<double>(width - m_minWidth) / (m_maxWidth - m_minWidth);
    return trackLeft + static_cast<int>(ratio * (trackRight - trackLeft));
}
