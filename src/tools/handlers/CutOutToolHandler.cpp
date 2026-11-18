#include "tools/handlers/CutOutToolHandler.h"
#include "tools/ToolContext.h"

#include <QDebug>
#include <QFontMetrics>
#include <QPainter>
#include <QPen>
#include <QtMath>

namespace {

const QColor kRemoveFill(220, 40, 40, 90);
const QColor kRemoveLine(255, 90, 90);
const QColor kInsertFill(255, 255, 255, 110);
const QColor kInsertLine(255, 255, 255);
const QColor kLabelBackground(0, 0, 0, 180);
constexpr int kLabelFontPixelSize = 12;
constexpr int kLabelPadding = 4;
constexpr int kLabelCornerRadius = 3;
constexpr int kBorderWidth = 2;
constexpr int kHatchSpacing = 8;
constexpr qreal kAppendedBandThickness = 4.0;

} // namespace

void CutOutToolHandler::onActivate(ToolContext* ctx) {
    Q_UNUSED(ctx);
    resetGesture();
}

void CutOutToolHandler::onDeactivate(ToolContext* ctx) {
    Q_UNUSED(ctx);
    discardDraft();
}

void CutOutToolHandler::resetGesture() {
    m_mode = Mode::Idle;
    m_activePart = Part::None;
    m_hoverPart = Part::None;
}

bool CutOutToolHandler::canInteract() const {
    return !m_imageSize.isEmpty() && !m_model.sourceSize().isEmpty();
}

qreal CutOutToolHandler::toolExtent() const {
    return m_model.axis() == Axis::Rows ? m_imageSize.height() : m_imageSize.width();
}

qreal CutOutToolHandler::toolPosAlong(const QPointF& toolPos) const {
    return m_model.axis() == Axis::Rows ? toolPos.y() : toolPos.x();
}

int CutOutToolHandler::toSource(const QPointF& toolPos) const {
    if (!canInteract()) {
        return 0;
    }
    const int extent = m_model.extent();
    return qBound(0, qRound(toolPosAlong(toolPos) * extent / toolExtent()), extent);
}

QRectF CutOutToolHandler::draftToolRect() const {
    if (!hasDraft() || !canInteract()) {
        return QRectF();
    }
    const qreal scale = toolExtent() / m_model.extent();
    const qreal start = m_model.start() * scale;
    const qreal length = m_model.length() * scale;
    return m_model.axis() == Axis::Rows
        ? QRectF(0.0, start, m_imageSize.width(), length)
        : QRectF(start, 0.0, length, m_imageSize.height());
}

bool CutOutToolHandler::draftContains(const QPointF& toolPos) const {
    return hasDraft() && draftToolRect().contains(toolPos);
}

CutOutToolHandler::Part CutOutToolHandler::hitTest(const QPointF& toolPos) const {
    const QRectF rect = draftToolRect();
    if (rect.isEmpty()) {
        return Part::None;
    }
    const bool rows = m_model.axis() == Axis::Rows;
    const qreal pos = toolPosAlong(toolPos);
    const qreal start = rows ? rect.top() : rect.left();
    const qreal end = rows ? rect.bottom() : rect.right();
    // An edge reaches outward by the full radius, inward only a quarter of
    // the band, so a thin band keeps a middle to move it by.
    const qreal inner = qMin(kEdgeHitRadius, (end - start) * kEdgeInnerFraction);
    if (pos >= start - kEdgeHitRadius && pos <= start + inner) return Part::StartEdge;
    if (pos >= end - inner && pos <= end + kEdgeHitRadius) return Part::EndEdge;
    return (pos > start && pos < end) ? Part::Body : Part::None;
}

void CutOutToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos) {
    onMousePressF(ctx, QPointF(pos));
}

void CutOutToolHandler::onMouseMove(ToolContext* ctx, const QPoint& pos) {
    onMouseMoveF(ctx, QPointF(pos));
}

void CutOutToolHandler::onMouseRelease(ToolContext* ctx, const QPoint& pos) {
    onMouseReleaseF(ctx, QPointF(pos));
}

void CutOutToolHandler::onMousePressF(ToolContext* ctx, const QPointF& pos) {
    if (!canInteract()) {
        qWarning() << "CutOutToolHandler::onMousePress: Image size not set.";
        return;
    }
    if (m_mode != Mode::Idle) {
        return;
    }
    m_pressToolPos = pos;
    m_pressSourcePos = toSource(pos);
    m_startBeforeGesture = m_model.start();
    m_lengthBeforeGesture = m_model.length();
    m_dragExceededThreshold = false;

    m_activePart = hitTest(pos);
    if (m_activePart == Part::None) {
        m_mode = Mode::Drawing;
    } else if (m_activePart == Part::Body) {
        m_mode = Mode::Moving;
    } else {
        m_mode = Mode::Resizing;
    }
    ctx->repaint();
}

void CutOutToolHandler::onMouseMoveF(ToolContext* ctx, const QPointF& pos) {
    if (m_mode == Mode::Idle) {
        m_hoverPart = hitTest(pos);
        return;
    }
    if (!m_dragExceededThreshold) {
        if (qAbs(toolPosAlong(pos) - toolPosAlong(m_pressToolPos)) < kDragThreshold) {
            return;
        }
        m_dragExceededThreshold = true;
    }

    const int sourcePos = toSource(pos);
    switch (m_mode) {
    case Mode::Drawing:
        m_model.setFromDrag(m_pressSourcePos, sourcePos);
        break;
    case Mode::Moving:
        m_model.move(m_startBeforeGesture, m_lengthBeforeGesture, sourcePos - m_pressSourcePos);
        break;
    case Mode::Resizing:
        m_model.resize(m_startBeforeGesture, m_lengthBeforeGesture, m_activePart, sourcePos);
        break;
    case Mode::Idle:
        break;
    }
    notifyDraftChanged();
    ctx->repaint();
}

void CutOutToolHandler::onMouseReleaseF(ToolContext* ctx, const QPointF& pos) {
    if (m_mode == Mode::Idle) {
        return;
    }
    if (m_dragExceededThreshold) {
        onMouseMoveF(ctx, pos);
    }
    // A click, or a drag that marked nothing, keeps the previous band.
    if (!m_dragExceededThreshold || !m_model.hasStrip()) {
        m_model.restore(m_startBeforeGesture, m_lengthBeforeGesture);
    }
    m_mode = Mode::Idle;
    m_activePart = Part::None;
    m_hoverPart = hitTest(pos);
    notifyDraftChanged();
    ctx->repaint();
}

void CutOutToolHandler::drawPreview(QPainter& painter) const {
    QRectF band = draftToolRect();
    if (band.isEmpty()) {
        return;
    }
    // A blank band appended after the last row or column lies outside the
    // image; show it as a thin band on that edge instead of nothing.
    const QRectF imageRect(QPointF(0, 0), QSizeF(m_imageSize));
    if (!band.intersects(imageRect)) {
        if (m_model.axis() == Axis::Rows) {
            band = QRectF(0, imageRect.bottom() - kAppendedBandThickness, imageRect.width(), kAppendedBandThickness);
        } else {
            band = QRectF(imageRect.right() - kAppendedBandThickness, 0, kAppendedBandThickness, imageRect.height());
        }
    }
    const bool remove = m_model.operation() == Operation::Remove;
    const bool rows = m_model.axis() == Axis::Rows;

    painter.save();
    painter.fillRect(band, remove ? kRemoveFill : kInsertFill);
    if (remove) {
        // Hatching marks pixels that go away.
        painter.setClipRect(band, Qt::IntersectClip);
        painter.setPen(QPen(kRemoveLine, 1));
        const qreal reach = band.width() + band.height();
        for (qreal offset = -reach; offset < reach; offset += kHatchSpacing) {
            painter.drawLine(QPointF(band.left() + offset, band.bottom()),
                             QPointF(band.left() + offset + band.height(), band.top()));
        }
        painter.setClipping(false);
    }
    painter.setPen(QPen(remove ? kRemoveLine : kInsertLine, kBorderWidth, Qt::DashLine));
    if (rows) {
        painter.drawLine(band.topLeft(), band.topRight());
        painter.drawLine(band.bottomLeft(), band.bottomRight());
    } else {
        painter.drawLine(band.topLeft(), band.bottomLeft());
        painter.drawLine(band.topRight(), band.bottomRight());
    }

    // Length in source pixels, upright whatever transform the host paints with.
    const QString text = QStringLiteral("%1%2 px")
        .arg(remove ? QStringLiteral("\u2212") : QStringLiteral("+"))
        .arg(m_model.length());
    const QTransform world = painter.worldTransform();
    const QRectF viewBand = world.mapRect(band);
    const QRectF viewImage = world.mapRect(QRectF(QPointF(0, 0), QSizeF(m_imageSize)));
    painter.resetTransform();
    QFont font = painter.font();
    font.setPixelSize(kLabelFontPixelSize);
    painter.setFont(font);
    const QSize textSize = QFontMetrics(font).size(Qt::TextSingleLine, text);
    QRectF label(0, 0, textSize.width() + 2 * kLabelPadding, textSize.height() + kLabelPadding);
    label.moveCenter(viewBand.center());
    label.moveLeft(qBound(viewImage.left(), label.left(), qMax(viewImage.left(), viewImage.right() - label.width())));
    label.moveTop(qBound(viewImage.top(), label.top(), qMax(viewImage.top(), viewImage.bottom() - label.height())));
    painter.setPen(Qt::NoPen);
    painter.setBrush(kLabelBackground);
    painter.drawRoundedRect(label, kLabelCornerRadius, kLabelCornerRadius);
    painter.setPen(Qt::white);
    painter.drawText(label, Qt::AlignCenter, text);
    painter.restore();
}

void CutOutToolHandler::cancelDrawing() {
    if (m_mode == Mode::Idle) {
        return;
    }
    m_model.restore(m_startBeforeGesture, m_lengthBeforeGesture);
    m_mode = Mode::Idle;
    m_activePart = Part::None;
    notifyDraftChanged();
}

bool CutOutToolHandler::handleEscape(ToolContext* ctx) {
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

QCursor CutOutToolHandler::cursor() const {
    const Part part = m_mode == Mode::Idle ? m_hoverPart : m_activePart;
    if (m_mode == Mode::Drawing || part == Part::None) {
        return Qt::CrossCursor;
    }
    if (part == Part::Body) {
        return Qt::SizeAllCursor;
    }
    return viewAxis() == Axis::Rows ? Qt::SizeVerCursor : Qt::SizeHorCursor;
}

void CutOutToolHandler::setCommitCallback(CommitCallback callback) {
    m_commitCallback = std::move(callback);
}

void CutOutToolHandler::setDraftChangedCallback(std::function<void()> callback) {
    m_draftChangedCallback = std::move(callback);
}

void CutOutToolHandler::setImageSize(const QSize& size) {
    m_imageSize = size;
}

void CutOutToolHandler::setSource(const QSize& pixelSize, qreal devicePixelRatio, qint64 token) {
    const bool hadDraft = hasDraft() || m_mode != Mode::Idle;
    const bool replaced = token != m_sourceToken || pixelSize != m_model.sourceSize();
    m_sourceToken = token;
    if (replaced) {
        resetGesture();
        m_model.clear();
    }
    // setImageSize() comes first, so the view scale is the current one.
    const QSizeF viewPerPixel = pixelSize.isEmpty()
        ? QSizeF(1.0, 1.0)
        : QSizeF(static_cast<qreal>(m_imageSize.width()) / pixelSize.width(),
                 static_cast<qreal>(m_imageSize.height()) / pixelSize.height());
    const bool hadStrip = hasDraft();
    m_model.setSource(pixelSize, devicePixelRatio, viewPerPixel);
    if ((replaced && hadDraft) || hadStrip != hasDraft()) {
        notifyDraftChanged();
    }
}

void CutOutToolHandler::setViewOrientation(int rotationAngle, bool flipHorizontal, bool flipVertical) {
    const int normalized = ((rotationAngle % 360) + 360) % 360;
    if (normalized == m_rotationAngle && flipHorizontal == m_flipHorizontal
        && flipVertical == m_flipVertical) {
        return;
    }
    // A drag in progress was mapped with the old orientation.
    if (m_mode != Mode::Idle) {
        cancelDrawing();
    }
    const Axis shown = viewAxis();
    m_rotationAngle = ((rotationAngle % 360) + 360) % 360;
    m_flipHorizontal = flipHorizontal;
    m_flipVertical = flipVertical;
    if (viewAxis() != shown) {
        notifyDraftChanged();   // the same band now runs the other way on screen
    }
}

CutOutToolHandler::Axis CutOutToolHandler::viewAxis() const {
    if (!transposed()) {
        return m_model.axis();
    }
    return m_model.axis() == Axis::Rows ? Axis::Columns : Axis::Rows;
}

void CutOutToolHandler::setViewAxis(Axis axis) {
    if (axis == viewAxis()) {
        return;
    }
    resetGesture();
    m_model.setAxis(m_model.axis() == Axis::Rows ? Axis::Columns : Axis::Rows);
    notifyDraftChanged();
}

void CutOutToolHandler::setOperation(Operation operation) {
    if (operation == m_model.operation()) {
        return;
    }
    if (m_mode != Mode::Idle) {
        cancelDrawing();
    }
    m_model.setOperation(operation);
    notifyDraftChanged();
}

bool CutOutToolHandler::nudgeDraft(int sourceDelta) {
    if (!hasDraft() || m_mode != Mode::Idle) {
        return false;
    }
    m_model.nudge(sourceDelta);
    notifyDraftChanged();
    return true;
}

bool CutOutToolHandler::commitDraft() {
    if (!hasDraft() || m_mode != Mode::Idle) {
        return false;
    }
    if (!m_commitCallback) {
        qWarning() << "CutOutToolHandler::commitDraft: Commit callback not set.";
        return false;
    }
    if (!m_commitCallback(m_model.request())) {
        return false;   // refused: the band stays for another try
    }
    discardDraft();   // do not depend on the host leaving the tool
    return true;
}

void CutOutToolHandler::discardDraft() {
    const bool hadDraft = hasDraft() || m_mode != Mode::Idle;
    resetGesture();
    m_model.clear();
    if (hadDraft) {
        notifyDraftChanged();
    }
}

void CutOutToolHandler::notifyDraftChanged() {
    if (m_draftChangedCallback) {
        m_draftChangedCallback();
    }
}
