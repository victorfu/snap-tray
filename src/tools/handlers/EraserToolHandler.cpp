#include "tools/handlers/EraserToolHandler.h"
#include "tools/ToolContext.h"

#include <QCursor>
#include <QGuiApplication>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QtMath>
#include <algorithm>

namespace {
constexpr qreal kCursorRenderDprFloor = 2.0;
constexpr qreal kEraserCursorOutlineWidth = 2.0;
constexpr int kEraserCursorPadding = 4;
const QColor kCursorOutlineColor(0x6C, 0x5C, 0xE7);

qreal cursorRenderDpr()
{
    if (auto* screen = QGuiApplication::screenAt(QCursor::pos())) {
        return std::max(kCursorRenderDprFloor, screen->devicePixelRatio());
    }
    if (auto* screen = QGuiApplication::primaryScreen()) {
        return std::max(kCursorRenderDprFloor, screen->devicePixelRatio());
    }
    if (qApp) {
        return std::max(kCursorRenderDprFloor, qApp->devicePixelRatio());
    }
    return kCursorRenderDprFloor;
}

QPixmap createCursorCanvas(const QSize& logicalSize, qreal dpr)
{
    const QSize physicalSize(
        qCeil(logicalSize.width() * dpr),
        qCeil(logicalSize.height() * dpr)
    );

    QPixmap pixmap(physicalSize);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    return pixmap;
}

QPixmap createCursorPixmap(int diameter) {
    const QSize logicalSize(
        diameter + (kEraserCursorPadding * 2),
        diameter + (kEraserCursorPadding * 2)
    );
    const qreal dpr = cursorRenderDpr();
    QPixmap pixmap = createCursorCanvas(logicalSize, dpr);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(kCursorOutlineColor,
        kEraserCursorOutlineWidth,
        Qt::SolidLine,
        Qt::RoundCap,
        Qt::RoundJoin));

    const qreal inset = kEraserCursorPadding + (kEraserCursorOutlineWidth / 2.0);
    const qreal ellipseExtent = std::max<qreal>(1.0, diameter - kEraserCursorOutlineWidth);
    painter.drawEllipse(QRectF(inset, inset, ellipseExtent, ellipseExtent));

    painter.end();
    return pixmap;
}
}  // namespace

void EraserToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos)
{
    if (!ctx || !ctx->annotationLayer) {
        return;
    }

    m_isErasing = true;
    m_lastPoint = pos;
    m_currentStrokeErasedItems.clear();
    m_removedOriginalIndices.clear();

    eraseAt(ctx, pos);
    ctx->repaint();
}

void EraserToolHandler::onMouseMove(ToolContext* ctx, const QPoint& pos)
{
    if (m_isErasing) {
        if (pos == m_lastPoint) {
            return;
        }

        eraseAt(ctx, pos);
        m_lastPoint = pos;
        ctx->repaint();
        return;
    }
}

void EraserToolHandler::onMouseRelease(ToolContext* ctx, const QPoint& pos)
{
    if (!m_isErasing) {
        return;
    }

    if (pos != m_lastPoint) {
        eraseAt(ctx, pos);
    }

    if (!m_currentStrokeErasedItems.empty() && ctx) {
        ctx->addItem(std::make_unique<ErasedItemsGroup>(std::move(m_currentStrokeErasedItems)));
        m_currentStrokeErasedItems.clear();
    }

    m_isErasing = false;
    m_removedOriginalIndices.clear();
    m_lastPoint = pos;

    if (ctx) {
        ctx->repaint();
    }
}

void EraserToolHandler::drawPreview(QPainter& painter) const
{
    Q_UNUSED(painter);
}

void EraserToolHandler::cancelDrawing()
{
    m_isErasing = false;
    m_currentStrokeErasedItems.clear();
    m_removedOriginalIndices.clear();
}

QCursor EraserToolHandler::cursor() const
{
    int diameter = std::clamp(m_eraserWidth, kMinWidth, kMaxWidth);
    if (m_cachedCursorWidth != diameter || m_cachedCursor.pixmap().isNull()) {
        QPixmap pixmap = createCursorPixmap(diameter);
        const QSizeF logicalSize = pixmap.deviceIndependentSize();
        const QPoint hotspot(
            qRound(logicalSize.width() / 2.0),
            qRound(logicalSize.height() / 2.0)
        );
        m_cachedCursor = QCursor(pixmap, hotspot.x(), hotspot.y());
        m_cachedCursorWidth = diameter;
    }
    return m_cachedCursor;
}

void EraserToolHandler::eraseAt(ToolContext* ctx, const QPoint& pos)
{
    if (!ctx || !ctx->annotationLayer) {
        return;
    }

    int diameter = std::clamp(m_eraserWidth, kMinWidth, kMaxWidth);
    m_eraserWidth = diameter;
    auto removedItems = ctx->annotationLayer->removeItemsIntersecting(pos, diameter);
    if (removedItems.empty()) {
        return;
    }

    for (auto& indexed : removedItems) {
        indexed.originalIndex = mapToOriginalIndex(indexed.originalIndex);
        m_currentStrokeErasedItems.push_back(std::move(indexed));
    }
}

size_t EraserToolHandler::mapToOriginalIndex(size_t currentIndex)
{
    size_t originalIndex = currentIndex;
    for (size_t removedIndex : m_removedOriginalIndices) {
        if (removedIndex <= originalIndex) {
            ++originalIndex;
        } else {
            break;
        }
    }

    auto it = std::lower_bound(m_removedOriginalIndices.begin(),
        m_removedOriginalIndices.end(), originalIndex);
    m_removedOriginalIndices.insert(it, originalIndex);

    return originalIndex;
}
