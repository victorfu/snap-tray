#include "tools/handlers/CropToolHandler.h"
#include "tools/ToolContext.h"

#include <QDebug>
#include <QFontMetrics>
#include <QPainter>
#include <QPen>
#include <QtMath>

namespace {

const QColor kDimColor(0, 0, 0, 128);
const QColor kGridColor(255, 255, 255, 80);
const QColor kLabelBackground(0, 0, 0, 180);
const QColor kHandleOutline(0, 0, 0, 160);
constexpr int kLabelFontPixelSize = 12;
constexpr int kLabelPadding = 4;
constexpr int kLabelGap = 8;
constexpr int kLabelCornerRadius = 3;
constexpr int kBorderWidth = 2;

} // namespace

void CropToolHandler::onActivate(ToolContext* ctx) {
    Q_UNUSED(ctx);
    m_mode = Mode::Idle;
    m_hoverHandle = Handle::None;
}

void CropToolHandler::onDeactivate(ToolContext* ctx) {
    Q_UNUSED(ctx);
    // Leaving the tool drops the draft: it never lingers under another tool.
    discardDraft();
}

bool CropToolHandler::canInteract() const {
    return !m_imageSize.isEmpty() && !m_model.sourceSize().isEmpty();
}

QPoint CropToolHandler::toSource(const QPointF& toolPos) const {
    if (!canInteract()) {
        return QPoint();
    }
    const QSize source = m_model.sourceSize();
    return QPoint(
        qBound(0, qRound(toolPos.x() * source.width() / m_imageSize.width()), source.width()),
        qBound(0, qRound(toolPos.y() * source.height() / m_imageSize.height()), source.height()));
}

QRectF CropToolHandler::draftToolRect() const {
    if (!hasDraft() || !canInteract()) {
        return QRectF();
    }
    const QSize source = m_model.sourceSize();
    const qreal scaleX = static_cast<qreal>(m_imageSize.width()) / source.width();
    const qreal scaleY = static_cast<qreal>(m_imageSize.height()) / source.height();
    const QRect rect = m_model.rect();
    return QRectF(rect.x() * scaleX, rect.y() * scaleY,
                  rect.width() * scaleX, rect.height() * scaleY);
}

bool CropToolHandler::draftContains(const QPointF& toolPos) const {
    return hasDraft() && draftToolRect().contains(toolPos);
}

CropToolHandler::Handle CropToolHandler::hitTest(const QPointF& toolPos) const {
    const QRectF rect = draftToolRect();
    if (rect.isEmpty()) {
        return Handle::None;
    }

    // A handle reaches kHandleHitRadius outward, but inward only a quarter of
    // the draft, so even a draft a few pixels across keeps a movable middle.
    const qreal innerX = qMin(kHandleHitRadius, rect.width() * kHandleInnerFraction);
    const qreal innerY = qMin(kHandleHitRadius, rect.height() * kHandleInnerFraction);
    const auto nearEdge = [](qreal pos, qreal edge, qreal before, qreal after) {
        return pos >= edge - before && pos <= edge + after;
    };
    const bool nearLeft = nearEdge(toolPos.x(), rect.left(), kHandleHitRadius, innerX);
    const bool nearRight = nearEdge(toolPos.x(), rect.right(), innerX, kHandleHitRadius);
    const bool nearTop = nearEdge(toolPos.y(), rect.top(), kHandleHitRadius, innerY);
    const bool nearBottom = nearEdge(toolPos.y(), rect.bottom(), innerY, kHandleHitRadius);
    const bool withinX = toolPos.x() >= rect.left() - kHandleHitRadius
        && toolPos.x() <= rect.right() + kHandleHitRadius;
    const bool withinY = toolPos.y() >= rect.top() - kHandleHitRadius
        && toolPos.y() <= rect.bottom() + kHandleHitRadius;
    if (!withinX || !withinY) {
        return Handle::None;
    }

    if (nearLeft && nearTop) return Handle::TopLeft;
    if (nearRight && nearTop) return Handle::TopRight;
    if (nearLeft && nearBottom) return Handle::BottomLeft;
    if (nearRight && nearBottom) return Handle::BottomRight;
    if (nearLeft) return Handle::Left;
    if (nearRight) return Handle::Right;
    if (nearTop) return Handle::Top;
    if (nearBottom) return Handle::Bottom;
    return rect.contains(toolPos) ? Handle::Move : Handle::None;
}

void CropToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos) {
    onMousePressF(ctx, QPointF(pos));
}

void CropToolHandler::onMouseMove(ToolContext* ctx, const QPoint& pos) {
    onMouseMoveF(ctx, QPointF(pos));
}

void CropToolHandler::onMouseRelease(ToolContext* ctx, const QPoint& pos) {
    onMouseReleaseF(ctx, QPointF(pos));
}

void CropToolHandler::onMousePressF(ToolContext* ctx, const QPointF& pos) {
    if (!canInteract()) {
        qWarning() << "CropToolHandler::onMousePress: Image size not set. Cannot start crop.";
        return;
    }
    if (m_mode != Mode::Idle) {
        return;
    }

    m_pressToolPos = pos;
    m_pressSourcePos = toSource(pos);
    m_rectBeforeGesture = m_model.rect();
    m_dragExceededThreshold = false;

    m_activeHandle = hitTest(pos);
    if (m_activeHandle == Handle::None) {
        m_mode = Mode::Drawing;
    } else if (m_activeHandle == Handle::Move) {
        m_mode = Mode::Moving;
    } else {
        m_mode = Mode::Resizing;
    }
    ctx->repaint();
}

void CropToolHandler::onMouseMoveF(ToolContext* ctx, const QPointF& pos) {
    if (m_mode == Mode::Idle) {
        m_hoverHandle = hitTest(pos);
        return;
    }

    if (!m_dragExceededThreshold) {
        const QPointF travel = pos - m_pressToolPos;
        if (qMax(qAbs(travel.x()), qAbs(travel.y())) < kDragThreshold) {
            return;
        }
        m_dragExceededThreshold = true;
    }

    const QPoint sourcePos = toSource(pos);
    switch (m_mode) {
    case Mode::Drawing:
        m_model.setRectFromDrag(m_pressSourcePos, sourcePos);
        break;
    case Mode::Moving:
        m_model.moveRect(m_rectBeforeGesture, sourcePos - m_pressSourcePos);
        break;
    case Mode::Resizing:
        m_model.resizeRect(m_rectBeforeGesture, m_activeHandle, sourcePos);
        break;
    case Mode::Idle:
        break;
    }
    notifyDraftChanged();
    ctx->repaint();
}

void CropToolHandler::onMouseReleaseF(ToolContext* ctx, const QPointF& pos) {
    if (m_mode == Mode::Idle) {
        return;
    }

    if (m_dragExceededThreshold) {
        onMouseMoveF(ctx, pos);
    }
    // A click, or a drag that ended with nothing selectable, keeps the
    // previous draft instead of silently dropping it.
    if (!m_dragExceededThreshold || !m_model.hasRect()) {
        m_model.restoreRect(m_rectBeforeGesture);
    }

    m_mode = Mode::Idle;
    m_activeHandle = Handle::None;
    m_hoverHandle = hitTest(pos);
    notifyDraftChanged();
    ctx->repaint();
}

void CropToolHandler::drawPreview(QPainter& painter) const {
    const QRectF crop = draftToolRect();
    if (crop.isEmpty()) {
        return;
    }
    const QRectF image(QPointF(0, 0), QSizeF(m_imageSize));

    painter.save();

    // Dim everything outside the selection.
    painter.fillRect(QRectF(image.left(), image.top(), image.width(), crop.top() - image.top()), kDimColor);
    painter.fillRect(QRectF(image.left(), crop.bottom(), image.width(), image.bottom() - crop.bottom()), kDimColor);
    painter.fillRect(QRectF(image.left(), crop.top(), crop.left() - image.left(), crop.height()), kDimColor);
    painter.fillRect(QRectF(crop.right(), crop.top(), image.right() - crop.right(), crop.height()), kDimColor);

    painter.setPen(QPen(Qt::white, kBorderWidth, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(crop);

    // Rule-of-thirds grid.
    painter.setPen(QPen(kGridColor, 1, Qt::SolidLine));
    for (int i = 1; i <= 2; ++i) {
        const qreal x = crop.left() + crop.width() * i / 3.0;
        const qreal y = crop.top() + crop.height() * i / 3.0;
        painter.drawLine(QPointF(x, crop.top()), QPointF(x, crop.bottom()));
        painter.drawLine(QPointF(crop.left(), y), QPointF(crop.right(), y));
    }

    // Corner and edge handles.
    painter.setPen(QPen(kHandleOutline, 1));
    painter.setBrush(Qt::white);
    const qreal half = kHandleSize / 2.0;
    const QPointF center = crop.center();
    const QPointF handleCenters[] = {
        crop.topLeft(), crop.topRight(), crop.bottomLeft(), crop.bottomRight(),
        QPointF(center.x(), crop.top()), QPointF(center.x(), crop.bottom()),
        QPointF(crop.left(), center.y()), QPointF(crop.right(), center.y()),
    };
    for (const QPointF& handleCenter : handleCenters) {
        painter.drawRect(QRectF(handleCenter.x() - half, handleCenter.y() - half, kHandleSize, kHandleSize));
    }

    // Size label in source pixels. The host may paint through a rotation or a
    // mirror, so the label is placed in device space and drawn upright.
    const QRect sourceRect = m_model.rect();
    const QSize shownSize = m_model.viewTransposed() ? sourceRect.size().transposed() : sourceRect.size();
    const QString sizeText = QStringLiteral("%1 \u00D7 %2 px").arg(shownSize.width()).arg(shownSize.height());
    const QTransform world = painter.worldTransform();
    const QRectF viewCrop = world.mapRect(crop);
    const QRectF viewImage = world.mapRect(image);
    painter.resetTransform();

    QFont font = painter.font();
    font.setPixelSize(kLabelFontPixelSize);
    painter.setFont(font);
    const QFontMetrics metrics(font);
    const QSize textSize = metrics.size(Qt::TextSingleLine, sizeText);
    QRectF labelRect(0, 0, textSize.width() + 2 * kLabelPadding, textSize.height() + kLabelPadding);
    qreal labelTop = viewCrop.bottom() + kLabelGap;
    if (labelTop + labelRect.height() > viewImage.bottom()) {
        labelTop = viewCrop.top() - kLabelGap - labelRect.height();
    }
    if (labelTop < viewImage.top()) {
        labelTop = viewCrop.top() + kLabelGap;
    }
    labelRect.moveTo(qBound(viewImage.left(),
                            viewCrop.center().x() - labelRect.width() / 2.0,
                            qMax(viewImage.left(), viewImage.right() - labelRect.width())),
                     labelTop);
    painter.setPen(Qt::NoPen);
    painter.setBrush(kLabelBackground);
    painter.drawRoundedRect(labelRect, kLabelCornerRadius, kLabelCornerRadius);
    painter.setPen(Qt::white);
    painter.drawText(labelRect, Qt::AlignCenter, sizeText);

    painter.restore();
}

void CropToolHandler::cancelDrawing() {
    if (m_mode == Mode::Idle) {
        return;
    }
    // Abandon the drag only; the draft from before it stays.
    m_model.restoreRect(m_rectBeforeGesture);
    m_mode = Mode::Idle;
    m_activeHandle = Handle::None;
    notifyDraftChanged();
}

bool CropToolHandler::handleEscape(ToolContext* ctx) {
    if (m_mode != Mode::Idle) {
        cancelDrawing();
        ctx->repaint();
        return true;
    }
    if (hasDraft()) {
        discardDraft();
        ctx->repaint();
        return true;
    }
    return false;
}

Qt::CursorShape CropToolHandler::cursorForHandle(Handle handle) const {
    const bool transposed = m_rotationAngle == 90 || m_rotationAngle == 270;
    // A quarter turn or a single mirror swaps the two diagonals on screen.
    const bool diagonalSwapped = transposed != (m_flipHorizontal != m_flipVertical);
    switch (handle) {
    case Handle::Move:
        return Qt::SizeAllCursor;
    case Handle::Left:
    case Handle::Right:
        return transposed ? Qt::SizeVerCursor : Qt::SizeHorCursor;
    case Handle::Top:
    case Handle::Bottom:
        return transposed ? Qt::SizeHorCursor : Qt::SizeVerCursor;
    case Handle::TopLeft:
    case Handle::BottomRight:
        return diagonalSwapped ? Qt::SizeBDiagCursor : Qt::SizeFDiagCursor;
    case Handle::TopRight:
    case Handle::BottomLeft:
        return diagonalSwapped ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor;
    case Handle::None:
        break;
    }
    return Qt::CrossCursor;
}

QCursor CropToolHandler::cursor() const {
    if (m_mode == Mode::Moving || m_mode == Mode::Resizing) {
        return cursorForHandle(m_activeHandle);
    }
    if (m_mode == Mode::Drawing) {
        return Qt::CrossCursor;
    }
    return cursorForHandle(m_hoverHandle);
}

void CropToolHandler::setCropCallback(CropCallback callback) {
    m_cropCallback = std::move(callback);
}

void CropToolHandler::setDraftChangedCallback(std::function<void()> callback) {
    m_draftChangedCallback = std::move(callback);
}

void CropToolHandler::setImageSize(const QSize& size) {
    m_imageSize = size;
}

void CropToolHandler::setSource(const QSize& pixelSize, qreal devicePixelRatio, qint64 token) {
    const bool hadDraft = hasDraft() || m_mode != Mode::Idle;
    const bool replaced = token != m_sourceToken || pixelSize != m_model.sourceSize();
    m_sourceToken = token;
    if (replaced) {
        m_mode = Mode::Idle;
        m_activeHandle = Handle::None;
        m_hoverHandle = Handle::None;
        m_model.clear();
    }
    m_model.setSource(pixelSize, devicePixelRatio);
    if (replaced && hadDraft) {
        notifyDraftChanged();
    }
}

void CropToolHandler::setViewOrientation(int rotationAngle, bool flipHorizontal, bool flipVertical) {
    m_rotationAngle = ((rotationAngle % 360) + 360) % 360;
    m_flipHorizontal = flipHorizontal;
    m_flipVertical = flipVertical;
    const bool transposed = m_rotationAngle == 90 || m_rotationAngle == 270;
    if (transposed != m_model.viewTransposed()) {
        m_model.setViewTransposed(transposed);
        notifyDraftChanged();
    }
}

void CropToolHandler::setAspect(Aspect aspect) {
    if (m_mode != Mode::Idle) {
        cancelDrawing();
    }
    m_model.setAspect(aspect);
    notifyDraftChanged();
}

bool CropToolHandler::nudgeDraft(const QPoint& sourceDelta) {
    if (!hasDraft() || m_mode != Mode::Idle) {
        return false;
    }
    m_model.nudge(sourceDelta);
    notifyDraftChanged();
    return true;
}

bool CropToolHandler::commitDraft() {
    if (!hasDraft() || m_mode != Mode::Idle) {
        return false;
    }
    if (!m_cropCallback) {
        qWarning() << "CropToolHandler::commitDraft: Crop callback not set. Crop action discarded.";
        return false;
    }
    // The host replaces the image on success, which drops the draft through
    // setSource(); a refused crop leaves it in place for another try.
    return m_cropCallback(m_model.rect());
}

void CropToolHandler::discardDraft() {
    const bool hadDraft = hasDraft() || m_mode != Mode::Idle;
    m_mode = Mode::Idle;
    m_activeHandle = Handle::None;
    m_hoverHandle = Handle::None;
    m_model.clear();
    if (hadDraft) {
        notifyDraftChanged();
    }
}

void CropToolHandler::notifyDraftChanged() {
    if (m_draftChangedCallback) {
        m_draftChangedCallback();
    }
}
