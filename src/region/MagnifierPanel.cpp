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
    gridPainter.setRenderHint(QPainter::Antialiasing, false);

    // Draw grid lines (separators between each pixel cell)
    QPen gridPen(QColor(80, 80, 80, 100), 1);
    gridPainter.setPen(gridPen);

    // Vertical grid lines
    for (int i = 0; i <= kGridCountX; ++i) {
        int x = i * pixelSizeX;
        gridPainter.drawLine(x, 0, x, kHeight);
    }

    // Horizontal grid lines
    for (int j = 0; j <= kGridCountY; ++j) {
        int y = j * pixelSizeY;
        gridPainter.drawLine(0, y, kWidth, y);
    }

    const int cx = kWidth / 2;
    const int cy = kHeight / 2;
    const int halfCellX = pixelSizeX / 2;
    const int halfCellY = pixelSizeY / 2;

    const int centerLeft = cx - halfCellX;
    const int centerTop = cy - halfCellY;

    // Blue crosshair stripes (filled rectangles)
    QColor crosshairColor(70, 130, 180, 100);

    // Horizontal stripe (full width, one cell height)
    gridPainter.fillRect(0, centerTop, kWidth, pixelSizeY, crosshairColor);

    // Vertical stripe (full height, one cell width)
    gridPainter.fillRect(centerLeft, 0, pixelSizeX, kHeight, crosshairColor);

    // Note: Center pixel box is now drawn dynamically in draw() based on background brightness
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

void MagnifierPanel::updateMagnifierCache(const QPoint& cursorPos, const QPixmap& backgroundPixmap)
{
    int deviceX = static_cast<int>(cursorPos.x() * m_devicePixelRatio);
    int deviceY = static_cast<int>(cursorPos.y() * m_devicePixelRatio);

    QPoint currentDevicePos(deviceX, deviceY);
    if (m_cacheValid && m_cachedDevicePosition == currentDevicePos) {
        return;  // Cache still valid
    }

    int deviceGridCountX = static_cast<int>(kGridCountX * m_devicePixelRatio);
    int deviceGridCountY = static_cast<int>(kGridCountY * m_devicePixelRatio);
    int sampleX = deviceX - deviceGridCountX / 2;
    int sampleY = deviceY - deviceGridCountY / 2;

    // Convert pixmap to QImage for pixel access
    QImage backgroundImage = backgroundPixmap.toImage();

    // Create sample image with SAME format as background to avoid conversion issues
    QImage sampleImage(deviceGridCountX, deviceGridCountY, backgroundImage.format());
    sampleImage.fill(Qt::black);

    // Calculate valid source rect
    QRect srcRect(sampleX, sampleY, deviceGridCountX, deviceGridCountY);
    QRect validSrcRect = srcRect.intersected(backgroundImage.rect());

    if (!validSrcRect.isEmpty()) {
        // Direct pixel copy - avoid QPainter entirely to prevent any painter state issues
        int dstX = validSrcRect.x() - sampleX;
        int dstY = validSrcRect.y() - sampleY;
        int bytesPerPixel = backgroundImage.depth() / 8;

        for (int y = 0; y < validSrcRect.height(); ++y) {
            const uchar* srcLine = backgroundImage.constScanLine(validSrcRect.y() + y);
            uchar* dstLine = sampleImage.scanLine(dstY + y);

            std::memcpy(
                dstLine + (dstX * bytesPerPixel),
                srcLine + (validSrcRect.x() * bytesPerPixel),
                validSrcRect.width() * bytesPerPixel
            );
        }
    }

    // Update current color from center pixel
    int centerX = deviceGridCountX / 2;
    int centerY = deviceGridCountY / 2;
    if (centerX >= 0 && centerX < sampleImage.width() &&
        centerY >= 0 && centerY < sampleImage.height()) {
        m_currentColor = sampleImage.pixelColor(centerX, centerY);
    } else {
        m_currentColor = Qt::black;
    }

    m_magnifierPixmapCache = QPixmap::fromImage(sampleImage).scaled(kWidth, kHeight,
        Qt::IgnoreAspectRatio,
        Qt::FastTransformation);

    m_cachedDevicePosition = currentDevicePos;
    m_cacheValid = true;
}

void MagnifierPanel::draw(QPainter& painter, const QPoint& cursorPos,
                          const QSize& viewportSize, const QPixmap& backgroundPixmap)
{
    m_currentCursorPos = cursorPos;

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
    // This will also update m_currentColor
    updateMagnifierCache(cursorPos, backgroundPixmap);

    // Draw cached magnified pixmap
    painter.drawPixmap(magX, magY, m_magnifierPixmapCache);

    // Draw cached grid overlay
    painter.drawPixmap(magX, magY, m_gridOverlayCache);

    // Draw dynamic center pixel box based on background brightness
    const int pixelSizeX = kWidth / kGridCountX;
    const int pixelSizeY = kHeight / kGridCountY;
    const int cx = kWidth / 2;
    const int cy = kHeight / 2;
    const int halfCellX = pixelSizeX / 2;
    const int halfCellY = pixelSizeY / 2;
    const int centerLeft = magX + cx - halfCellX;
    const int centerTop = magY + cy - halfCellY;

    // Calculate center pixel brightness
    int brightness = (m_currentColor.red() + m_currentColor.green() + m_currentColor.blue()) / 3;
    bool isDarkBackground = brightness < 128;

    if (isDarkBackground) {
        // Dark background: white outline only
        painter.setPen(QPen(Qt::white, 2));
        painter.setBrush(Qt::NoBrush);
    } else {
        // Light background: black outline with white fill
        painter.setPen(QPen(Qt::black, 2));
        painter.setBrush(Qt::white);
    }
    painter.drawRect(centerLeft, centerTop, pixelSizeX, pixelSizeY);

    // Draw magnifier area border (color based on background brightness)
    painter.setPen(QPen(isDarkBackground ? Qt::white : QColor(80, 80, 80), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(magX, magY, kWidth, kHeight);

    // Draw info panel
    drawInfoPanel(painter, panelX, magY + kHeight + 6, panelWidth);
}

void MagnifierPanel::drawInfoPanel(QPainter& painter, int panelX, int infoY, int panelWidth)
{
    // 1. Coordinate info (centered)
    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setPointSize(11);
    painter.setFont(font);

    QString coordText = QString("(%1 , %2)").arg(m_currentCursorPos.x()).arg(m_currentCursorPos.y());
    painter.drawText(panelX, infoY, panelWidth, 20, Qt::AlignCenter, coordText);

    // 2. Color preview + RGB/HEX (centered)
    infoY += 20;
    int colorBoxSize = 14;
    int colorTextGap = 8;

    // Calculate total width of color swatch + gap + text to center them
    QFontMetrics fm(font);
    QString colorText = colorString();
    int textWidth = fm.horizontalAdvance(colorText);
    int totalColorWidth = colorBoxSize + colorTextGap + textWidth;
    int colorStartX = panelX + (panelWidth - totalColorWidth) / 2;

    // Draw color swatch
    painter.fillRect(colorStartX, infoY, colorBoxSize, colorBoxSize, m_currentColor);
    painter.setPen(QPen(QColor(200, 200, 200), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(colorStartX, infoY, colorBoxSize, colorBoxSize);

    // Draw color text
    painter.setPen(Qt::white);
    painter.drawText(colorStartX + colorBoxSize + colorTextGap, infoY,
                     textWidth, colorBoxSize,
                     Qt::AlignVCenter, colorText);

    // 3. Hotkey instructions (centered, smaller font)
    infoY += 18;
    QFont smallFont = font;
    smallFont.setPointSize(9);
    painter.setFont(smallFont);
    painter.setPen(QColor(200, 200, 200));

    QString instruction1 = QString("Shift: Switch color format");
    painter.drawText(panelX, infoY, panelWidth, 14, Qt::AlignCenter, instruction1);

    infoY += 14;
    QString instruction2 = QString("C: Copy color value");
    painter.drawText(panelX, infoY, panelWidth, 14, Qt::AlignCenter, instruction2);
}
