#include "region/MagnifierPanel.h"
#include <QPainter>
#include <QDateTime>
#include <cstring>

MagnifierPanel::MagnifierPanel(QObject* parent)
    : QObject(parent)
{
    initializeGridCache();
}

void MagnifierPanel::initializeGridCache()
{
    const int pixelSizeX = kWidth / kGridCountX;
    const int pixelSizeY = kHeight / kGridCountY;

    m_gridOverlayCache = QPixmap(kWidth, kHeight);
    m_gridOverlayCache.fill(Qt::transparent);

    QPainter gridPainter(&m_gridOverlayCache);
    gridPainter.setRenderHint(QPainter::Antialiasing, false);  // Crisp grid lines

    const int cx = kWidth / 2;
    const int cy = kHeight / 2;
    const int halfCellX = pixelSizeX / 2;
    const int halfCellY = pixelSizeY / 2;

    // Center pixel region coordinates
    const int centerLeft = cx - halfCellX;
    const int centerTop = cy - halfCellY;

    // Draw grid lines (light gray, subtle)
    gridPainter.setPen(QPen(QColor(200, 200, 200, 80), 1));
    // Vertical grid lines
    for (int i = 1; i < kGridCountX; ++i) {
        int x = i * pixelSizeX;
        gridPainter.drawLine(x, 0, x, kHeight);
    }
    // Horizontal grid lines
    for (int i = 1; i < kGridCountY; ++i) {
        int y = i * pixelSizeY;
        gridPainter.drawLine(0, y, kWidth, y);
    }

    // Crosshair - thin blue lines, centered, no gap
    gridPainter.setPen(QPen(QColor(100, 150, 255), 2));  // Light blue crosshair
    // Horizontal line (full width)
    gridPainter.drawLine(0, cy, kWidth, cy);
    // Vertical line (full height)
    gridPainter.drawLine(cx, 0, cx, kHeight);

    // Center pixel box - white outline
    gridPainter.setBrush(Qt::NoBrush);
    gridPainter.setPen(QPen(Qt::white, 2));  // Thicker white border for emphasis
    gridPainter.drawRect(centerLeft, centerTop, pixelSizeX, pixelSizeY);
}

void MagnifierPanel::invalidateCache()
{
    m_cacheValid = false;
}

bool MagnifierPanel::shouldUpdate() const
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    return (now - m_lastUpdateTime) >= kMinUpdateMs;
}

void MagnifierPanel::markUpdated()
{
    m_lastUpdateTime = QDateTime::currentMSecsSinceEpoch();
}

QString MagnifierPanel::colorString() const
{
    if (m_showHexColor) {
        return QString("#%1%2%3")
            .arg(m_currentColor.red(), 2, 16, QChar('0'))
            .arg(m_currentColor.green(), 2, 16, QChar('0'))
            .arg(m_currentColor.blue(), 2, 16, QChar('0'));
    } else {
        return QString("RGB: %1,%2,%3")
            .arg(m_currentColor.red())
            .arg(m_currentColor.green())
            .arg(m_currentColor.blue());
    }
}

void MagnifierPanel::updateMagnifierCache(const QPoint& cursorPos, const QImage& backgroundImage)
{
    int deviceX = static_cast<int>(cursorPos.x() * m_devicePixelRatio);
    int deviceY = static_cast<int>(cursorPos.y() * m_devicePixelRatio);

    QPoint currentDevicePos(deviceX, deviceY);
    if (m_cacheValid && m_cachedDevicePosition == currentDevicePos) {
        return;  // Cache still valid
    }

    // Sample from device pixel pixmap
    // Take gridCount "logical pixels", each logical pixel = devicePixelRatio device pixels
    int deviceGridCountX = static_cast<int>(kGridCountX * m_devicePixelRatio);
    int deviceGridCountY = static_cast<int>(kGridCountY * m_devicePixelRatio);
    // Cursor position at center
    int sampleX = deviceX - deviceGridCountX / 2;
    int sampleY = deviceY - deviceGridCountY / 2;

    // Create a sample image centered on cursor, fill out-of-bounds with black
    QImage sampleImage(deviceGridCountX, deviceGridCountY, QImage::Format_ARGB32);
    sampleImage.fill(Qt::black);

    // Optimized pixel sampling using scanLine() for direct memory access
    // Pre-calculate valid source region bounds
    int srcLeft = qMax(0, sampleX);
    int srcTop = qMax(0, sampleY);
    int srcRight = qMin(backgroundImage.width(), sampleX + deviceGridCountX);
    int srcBottom = qMin(backgroundImage.height(), sampleY + deviceGridCountY);

    // Calculate destination offsets for out-of-bounds handling
    int dstOffsetX = srcLeft - sampleX;
    int dstOffsetY = srcTop - sampleY;

    // Copy valid region using scanLine for direct memory access (much faster than pixel-by-pixel)
    if (srcRight > srcLeft && srcBottom > srcTop) {
        int copyWidth = srcRight - srcLeft;
        for (int srcY = srcTop; srcY < srcBottom; ++srcY) {
            const QRgb* srcLine = reinterpret_cast<const QRgb*>(backgroundImage.constScanLine(srcY));
            QRgb* dstLine = reinterpret_cast<QRgb*>(sampleImage.scanLine(srcY - srcTop + dstOffsetY));
            memcpy(dstLine + dstOffsetX, srcLine + srcLeft, copyWidth * sizeof(QRgb));
        }
    }

    // Use IgnoreAspectRatio to ensure it fills the entire region
    m_magnifierPixmapCache = QPixmap::fromImage(sampleImage).scaled(kWidth, kHeight,
        Qt::IgnoreAspectRatio,
        Qt::FastTransformation);

    m_cachedDevicePosition = currentDevicePos;
    m_cacheValid = true;
}

void MagnifierPanel::draw(QPainter& painter, const QPoint& cursorPos,
                          const QSize& viewportSize, const QImage& backgroundImage)
{
    m_currentCursorPos = cursorPos;

    // Get current pixel color (device coordinates)
    int deviceX = static_cast<int>(cursorPos.x() * m_devicePixelRatio);
    int deviceY = static_cast<int>(cursorPos.y() * m_devicePixelRatio);
    if (deviceX >= 0 && deviceX < backgroundImage.width() &&
        deviceY >= 0 && deviceY < backgroundImage.height()) {
        m_currentColor = QColor(backgroundImage.pixel(deviceX, deviceY));
    } else {
        m_currentColor = Qt::black;
    }

    // Calculate panel position (cursor at top-left corner, like Snipaste/PixPin)
    int panelWidth = kWidth;
    int offset = 20;  // Small offset from cursor
    int panelX = cursorPos.x() + offset;
    int panelY = cursorPos.y() + offset;

    // Calculate total panel height (magnifier + coordinates + color + hotkey instructions)
    int totalHeight = kHeight + 85;  // magnifier + coords + color + 2 lines of hotkey hints

    // Boundary checking - flip to other side if near edges
    if (panelX + panelWidth + 10 > viewportSize.width()) {
        panelX = cursorPos.x() - panelWidth - offset;  // Place to left of cursor
    }
    if (panelY + totalHeight + 10 > viewportSize.height()) {
        panelY = cursorPos.y() - totalHeight - offset;  // Place above cursor
    }
    // Ensure minimum margins
    panelX = qMax(10, panelX);
    panelY = qMax(10, panelY);

    // Calculate magnifier display area
    int magX = panelX;
    int magY = panelY;

    // Draw bottom info area background - black, no border
    int infoAreaY = magY + kHeight;
    int infoAreaHeight = totalHeight - kHeight;
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(30, 35, 45, 240));
    painter.drawRect(panelX, infoAreaY, panelWidth, infoAreaHeight);

    // Update magnifier cache if needed
    updateMagnifierCache(cursorPos, backgroundImage);

    // Draw cached magnified pixmap
    painter.drawPixmap(magX, magY, m_magnifierPixmapCache);

    // Draw cached grid overlay
    painter.drawPixmap(magX, magY, m_gridOverlayCache);

    // Draw magnifier area white border
    painter.setPen(QPen(Qt::white, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(magX, magY, kWidth, kHeight);

    // Draw info panel
    drawInfoPanel(painter, panelX, magY + kHeight + 6, panelWidth);
}

void MagnifierPanel::drawInfoPanel(QPainter& painter, int panelX, int infoY, int panelWidth)
{
    // 1. Coordinate info
    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setPointSize(11);
    painter.setFont(font);

    QString coordText = QString("(%1 , %2)").arg(m_currentCursorPos.x()).arg(m_currentCursorPos.y());
    painter.drawText(panelX, infoY, panelWidth, 20, Qt::AlignCenter, coordText);

    // 2. Color preview + RGB/HEX (left-aligned)
    infoY += 20;
    int colorBoxSize = 14;

    // Left-aligned layout for color swatch and text
    int colorStartX = panelX + 8;

    // Draw color swatch
    painter.fillRect(colorStartX, infoY, colorBoxSize, colorBoxSize, m_currentColor);
    painter.setPen(QPen(QColor(200, 200, 200), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(colorStartX, infoY, colorBoxSize, colorBoxSize);

    // Draw color text
    painter.setPen(Qt::white);
    painter.drawText(colorStartX + colorBoxSize + 8, infoY,
                     panelWidth - 16 - colorBoxSize - 8, colorBoxSize,
                     Qt::AlignVCenter, colorString());

    // 3. Hotkey instructions (left-aligned, smaller font)
    infoY += 18;
    QFont smallFont = font;
    smallFont.setPointSize(9);
    painter.setFont(smallFont);
    painter.setPen(QColor(200, 200, 200));

    QString instruction1 = QString("Shift: Switch color format");
    painter.drawText(colorStartX, infoY, panelWidth - 16, 14,
                     Qt::AlignLeft | Qt::AlignVCenter, instruction1);

    infoY += 14;
    QString instruction2 = QString("C: Copy color value");
    painter.drawText(colorStartX, infoY, panelWidth - 16, 14,
                     Qt::AlignLeft | Qt::AlignVCenter, instruction2);
}
