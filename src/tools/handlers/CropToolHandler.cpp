#include "tools/handlers/CropToolHandler.h"
#include "tools/ToolContext.h"

#include <QDebug>
#include <QPainter>
#include <QPen>
#include <QBrush>

void CropToolHandler::onActivate(ToolContext* ctx) {
    Q_UNUSED(ctx);
    m_isDrawing = false;
}

void CropToolHandler::onDeactivate(ToolContext* ctx) {
    Q_UNUSED(ctx);
    cancelDrawing();
}

void CropToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos) {
    if (!m_imageSize.isValid() || m_imageSize.isEmpty()) {
        qWarning() << "CropToolHandler::onMousePress: Image size not set. Cannot start crop.";
        return;
    }
    m_isDrawing = true;
    m_startPoint = pos;
    m_currentPoint = pos;
    ctx->repaint();
}

void CropToolHandler::onMouseMove(ToolContext* ctx, const QPoint& pos) {
    if (!m_isDrawing) return;
    m_currentPoint = pos;
    ctx->repaint();
}

void CropToolHandler::onMouseRelease(ToolContext* ctx, const QPoint& pos) {
    if (!m_isDrawing) return;

    m_currentPoint = pos;
    QRect cropRect = clampToImage(makeRect(m_startPoint, m_currentPoint));

    m_isDrawing = false;

    // Only apply if large enough
    if (cropRect.width() >= kMinCropSize && cropRect.height() >= kMinCropSize) {
        if (m_cropCallback) {
            m_cropCallback(cropRect);
        } else {
            qWarning() << "CropToolHandler::onMouseRelease: Crop callback not set. Crop action discarded.";
        }
    } else {
        qDebug() << "CropToolHandler: Crop region too small" << cropRect.size() << "minimum:" << kMinCropSize;
    }

    ctx->repaint();
}

void CropToolHandler::drawPreview(QPainter& painter) const {
    if (!m_isDrawing) return;

    QRect cropRect = makeRect(m_startPoint, m_currentPoint);
    if (cropRect.isEmpty()) return;

    QRect clampedCrop = clampToImage(cropRect);
    if (clampedCrop.isEmpty()) return;

    QRect imageRect(QPoint(0, 0), m_imageSize);

    painter.save();

    // Draw dimmed overlay outside crop area
    QColor dimColor(0, 0, 0, 128);
    // Top strip
    if (clampedCrop.top() > imageRect.top()) {
        painter.fillRect(QRect(imageRect.left(), imageRect.top(),
                               imageRect.width(), clampedCrop.top() - imageRect.top()), dimColor);
    }
    // Bottom strip
    if (clampedCrop.bottom() < imageRect.bottom()) {
        painter.fillRect(QRect(imageRect.left(), clampedCrop.bottom() + 1,
                               imageRect.width(), imageRect.bottom() - clampedCrop.bottom()), dimColor);
    }
    // Left strip (between top and bottom strips)
    if (clampedCrop.left() > imageRect.left()) {
        painter.fillRect(QRect(imageRect.left(), clampedCrop.top(),
                               clampedCrop.left() - imageRect.left(), clampedCrop.height()), dimColor);
    }
    // Right strip (between top and bottom strips)
    if (clampedCrop.right() < imageRect.right()) {
        painter.fillRect(QRect(clampedCrop.right() + 1, clampedCrop.top(),
                               imageRect.right() - clampedCrop.right(), clampedCrop.height()), dimColor);
    }

    // Draw crop rectangle border (white dashed)
    QPen borderPen(Qt::white, 2, Qt::DashLine);
    painter.setPen(borderPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(clampedCrop);

    // Draw rule-of-thirds grid (thin semi-transparent white)
    QPen gridPen(QColor(255, 255, 255, 80), 1, Qt::SolidLine);
    painter.setPen(gridPen);
    int thirdW = clampedCrop.width() / 3;
    int thirdH = clampedCrop.height() / 3;
    // Vertical lines
    painter.drawLine(clampedCrop.left() + thirdW, clampedCrop.top(),
                     clampedCrop.left() + thirdW, clampedCrop.bottom());
    painter.drawLine(clampedCrop.left() + 2 * thirdW, clampedCrop.top(),
                     clampedCrop.left() + 2 * thirdW, clampedCrop.bottom());
    // Horizontal lines
    painter.drawLine(clampedCrop.left(), clampedCrop.top() + thirdH,
                     clampedCrop.right(), clampedCrop.top() + thirdH);
    painter.drawLine(clampedCrop.left(), clampedCrop.top() + 2 * thirdH,
                     clampedCrop.right(), clampedCrop.top() + 2 * thirdH);

    // Draw corner handles
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    int hs = kHandleSize;
    // Top-left
    painter.drawRect(QRect(clampedCrop.left() - hs / 2, clampedCrop.top() - hs / 2, hs, hs));
    // Top-right
    painter.drawRect(QRect(clampedCrop.right() - hs / 2, clampedCrop.top() - hs / 2, hs, hs));
    // Bottom-left
    painter.drawRect(QRect(clampedCrop.left() - hs / 2, clampedCrop.bottom() - hs / 2, hs, hs));
    // Bottom-right
    painter.drawRect(QRect(clampedCrop.right() - hs / 2, clampedCrop.bottom() - hs / 2, hs, hs));

    // Draw size label
    QString sizeText = QString("%1 × %2").arg(clampedCrop.width()).arg(clampedCrop.height());
    QFont font = painter.font();
    font.setPixelSize(12);
    painter.setFont(font);
    QFontMetrics fm(font);
    QRect textRect = fm.boundingRect(sizeText);
    int textX = clampedCrop.center().x() - textRect.width() / 2;
    int textY = clampedCrop.bottom() + 20;
    // If text would go below image, put it above the crop rect
    if (textY + textRect.height() > m_imageSize.height()) {
        textY = clampedCrop.top() - 8;
    }
    // Background for text
    QRect bgRect(textX - 4, textY - textRect.height(), textRect.width() + 8, textRect.height() + 4);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 180));
    painter.drawRoundedRect(bgRect, 3, 3);
    // Text
    painter.setPen(Qt::white);
    painter.drawText(textX, textY, sizeText);

    painter.restore();
}

void CropToolHandler::cancelDrawing() {
    m_isDrawing = false;
}

bool CropToolHandler::handleEscape(ToolContext* ctx) {
    if (m_isDrawing) {
        cancelDrawing();
        ctx->repaint();
        return true;
    }
    return false;
}

QCursor CropToolHandler::cursor() const {
    return Qt::CrossCursor;
}

void CropToolHandler::setCropCallback(std::function<void(const QRect&)> callback) {
    m_cropCallback = std::move(callback);
}

void CropToolHandler::setImageSize(const QSize& size) {
    m_imageSize = size;
}

QRect CropToolHandler::makeRect(const QPoint& start, const QPoint& end) const {
    return QRect(start, end).normalized();
}

QRect CropToolHandler::clampToImage(const QRect& rect) const {
    if (!m_imageSize.isValid() || m_imageSize.isEmpty() || rect.isEmpty()) {
        return QRect();
    }

    // Accept endpoint-inclusive coordinates (x == width, y == height), then
    // map back to pixel-index bounds so edge-aligned crops keep their last row/column.
    const QRect endpointBounds(
        QPoint(0, 0),
        QSize(m_imageSize.width() + 1, m_imageSize.height() + 1));
    QRect clamped = rect.intersected(endpointBounds);
    if (clamped.isEmpty()) {
        return QRect();
    }

    const int maxX = m_imageSize.width() - 1;
    const int maxY = m_imageSize.height() - 1;
    clamped.setLeft(qBound(0, clamped.left(), maxX));
    clamped.setTop(qBound(0, clamped.top(), maxY));
    clamped.setRight(qBound(0, clamped.right(), maxX));
    clamped.setBottom(qBound(0, clamped.bottom(), maxY));

    return clamped.isValid() ? clamped : QRect();
}
