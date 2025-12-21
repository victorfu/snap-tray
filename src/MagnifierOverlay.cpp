#include "MagnifierOverlay.h"

#include <QPainter>
#include <QFont>
#include <QFontMetrics>

const int MagnifierOverlay::PANEL_WIDTH;
const int MagnifierOverlay::PANEL_HEIGHT;
const int MagnifierOverlay::GRID_COUNT;
const int MagnifierOverlay::PANEL_PADDING;

MagnifierOverlay::MagnifierOverlay(QObject* parent)
    : QObject(parent)
    , m_devicePixelRatio(1.0)
    , m_showHexColor(true)
{
}

void MagnifierOverlay::setSourceImage(const QImage& image)
{
    m_sourceImage = image;
}

void MagnifierOverlay::setDevicePixelRatio(qreal ratio)
{
    m_devicePixelRatio = ratio;
}

void MagnifierOverlay::setCursorPosition(const QPoint& pos)
{
    m_cursorPosition = pos;
}

void MagnifierOverlay::setViewportRect(const QRect& rect)
{
    m_viewportRect = rect;
}

void MagnifierOverlay::setShowHexColor(bool hex)
{
    m_showHexColor = hex;
}

void MagnifierOverlay::toggleColorFormat()
{
    m_showHexColor = !m_showHexColor;
}

QColor MagnifierOverlay::colorAtCursor() const
{
    if (m_sourceImage.isNull()) {
        return Qt::black;
    }

    int deviceX = static_cast<int>(m_cursorPosition.x() * m_devicePixelRatio);
    int deviceY = static_cast<int>(m_cursorPosition.y() * m_devicePixelRatio);

    if (deviceX >= 0 && deviceX < m_sourceImage.width() &&
        deviceY >= 0 && deviceY < m_sourceImage.height()) {
        return m_sourceImage.pixelColor(deviceX, deviceY);
    }
    return Qt::black;
}

QString MagnifierOverlay::colorString() const
{
    QColor color = colorAtCursor();
    if (m_showHexColor) {
        return color.name().toUpper();
    } else {
        return QString("R:%1 G:%2 B:%3")
            .arg(color.red())
            .arg(color.green())
            .arg(color.blue());
    }
}

void MagnifierOverlay::drawCrosshair(QPainter& painter) const
{
    // Save painter state
    painter.save();

    // Deep blue color for crosshair
    QColor crosshairColor(65, 105, 225);
    QPen pen(crosshairColor, 1, Qt::SolidLine);
    painter.setPen(pen);

    int x = m_cursorPosition.x();
    int y = m_cursorPosition.y();

    // Horizontal line
    painter.drawLine(0, y, m_viewportRect.width(), y);
    // Vertical line
    painter.drawLine(x, 0, x, m_viewportRect.height());

    // Small center box
    painter.drawRect(x - 4, y - 4, 8, 8);

    painter.restore();
}

void MagnifierOverlay::drawMagnifier(QPainter& painter) const
{
    if (m_sourceImage.isNull()) return;

    int panelWidth = PANEL_WIDTH;
    int panelHeight = PANEL_HEIGHT;

    // Calculate panel position - prefer below cursor
    int panelX = m_cursorPosition.x() + 20;
    int panelY = m_cursorPosition.y() + 20;

    // Adjust if would go off screen
    if (panelX + panelWidth > m_viewportRect.width() - PANEL_PADDING) {
        panelX = m_cursorPosition.x() - panelWidth - 20;
    }
    if (panelY + panelHeight > m_viewportRect.height() - PANEL_PADDING) {
        panelY = m_cursorPosition.y() - panelHeight - 20;
    }
    if (panelX < PANEL_PADDING) panelX = PANEL_PADDING;
    if (panelY < PANEL_PADDING) panelY = PANEL_PADDING;

    // Draw panel background
    painter.save();
    painter.setPen(QPen(QColor(80, 80, 80), 1));
    painter.setBrush(QColor(30, 30, 30, 230));
    painter.drawRoundedRect(panelX, panelY, panelWidth, panelHeight, 8, 8);

    // Calculate magnifier grid area
    int gridSize = panelWidth - 20;  // 160x160
    int gridX = panelX + 10;
    int gridY = panelY + 10;
    int cellSize = gridSize / GRID_COUNT;

    // Device coordinates for sampling
    int deviceCenterX = static_cast<int>(m_cursorPosition.x() * m_devicePixelRatio);
    int deviceCenterY = static_cast<int>(m_cursorPosition.y() * m_devicePixelRatio);

    // Draw zoomed pixels (15x15 grid centered on cursor)
    int halfGrid = GRID_COUNT / 2;
    for (int dy = -halfGrid; dy <= halfGrid; ++dy) {
        for (int dx = -halfGrid; dx <= halfGrid; ++dx) {
            int srcX = deviceCenterX + dx;
            int srcY = deviceCenterY + dy;

            QColor pixelColor = Qt::black;
            if (srcX >= 0 && srcX < m_sourceImage.width() &&
                srcY >= 0 && srcY < m_sourceImage.height()) {
                pixelColor = m_sourceImage.pixelColor(srcX, srcY);
            }

            int cellX = gridX + (dx + halfGrid) * cellSize;
            int cellY = gridY + (dy + halfGrid) * cellSize;

            painter.fillRect(cellX, cellY, cellSize, cellSize, pixelColor);
        }
    }

    // Draw grid lines
    painter.setPen(QPen(QColor(60, 60, 60), 1));
    for (int i = 0; i <= GRID_COUNT; ++i) {
        int offset = i * cellSize;
        painter.drawLine(gridX + offset, gridY, gridX + offset, gridY + gridSize);
        painter.drawLine(gridX, gridY + offset, gridX + gridSize, gridY + offset);
    }

    // Highlight center cell (current pixel)
    int centerCellX = gridX + halfGrid * cellSize;
    int centerCellY = gridY + halfGrid * cellSize;
    painter.setPen(QPen(QColor(255, 255, 255), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(centerCellX, centerCellY, cellSize, cellSize);

    // Draw coordinates text below grid (smaller area)
    QFont font = painter.font();
    font.setPointSize(10);
    painter.setFont(font);

    int infoY = gridY + gridSize + 6;

    // Coordinates
    QString coordText = QString("X:%1 Y:%2")
        .arg(m_cursorPosition.x())
        .arg(m_cursorPosition.y());
    painter.setPen(QColor(180, 180, 180));
    painter.drawText(gridX, infoY, gridSize, 14, Qt::AlignLeft, coordText);

    // Color info
    QColor pixelColor = colorAtCursor();
    QString colorText = colorString();

    // Color swatch
    int swatchSize = 12;
    int swatchX = panelX + panelWidth - swatchSize - 10;
    painter.fillRect(swatchX, infoY, swatchSize, swatchSize, pixelColor);
    painter.setPen(QPen(QColor(80, 80, 80), 1));
    painter.drawRect(swatchX, infoY, swatchSize, swatchSize);

    // Color text (left of swatch)
    QFontMetrics fm(font);
    int textWidth = fm.horizontalAdvance(colorText);
    painter.setPen(Qt::white);
    painter.drawText(swatchX - textWidth - 4, infoY, textWidth, swatchSize, Qt::AlignRight | Qt::AlignVCenter, colorText);

    painter.restore();
}
