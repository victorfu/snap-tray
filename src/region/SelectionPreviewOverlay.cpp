#include "region/SelectionPreviewOverlay.h"

#include "GlassRenderer.h"
#include "ToolbarStyle.h"
#include "platform/WindowLevel.h"
#include "utils/CoordinateHelper.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QShowEvent>

namespace {

constexpr qreal kSelectionBorderWidth = 2.0;
constexpr int kSelectionHandleDiameter = 8;
constexpr int kSelectionHandleRadius = kSelectionHandleDiameter / 2;
constexpr int kSelectionChromeMargin = kSelectionHandleRadius + 1;
constexpr int kDimensionPanelHeight = 28;
constexpr int kDimensionPanelPadding = 24;
constexpr int kDimensionPanelTopGap = 8;
constexpr int kDimensionPanelInset = 5;

QRectF alignedSelectionBorderRect(const QRect& selectionRect, qreal penWidth, qreal dpr)
{
    QRectF borderRect = QRectF(selectionRect.normalized()).adjusted(0.5, 0.5, -0.5, -0.5);
    const int physicalPenWidth = qMax(1, qRound(penWidth * dpr));
    if ((physicalPenWidth % 2) != 0) {
        const qreal offset = 0.5 / qMax(dpr, 1.0);
        borderRect.translate(offset, offset);
    }
    return borderRect;
}

QRect selectionChromeBounds(const QRect& selectionRect)
{
    const QRect sel = selectionRect.normalized();
    if (!sel.isValid() || sel.isEmpty()) {
        return {};
    }
    return sel.adjusted(-kSelectionChromeMargin, -kSelectionChromeMargin,
                        kSelectionChromeMargin, kSelectionChromeMargin);
}

QRect physicalSelectionRect(const QRect& selectionRect, qreal dpr)
{
    return CoordinateHelper::toPhysicalCoveringRect(selectionRect.normalized(), dpr);
}

QString selectionSizeLabel(const QRect& selectionRect, qreal dpr)
{
    const QSize physicalSize = physicalSelectionRect(selectionRect, dpr).size();
    return QObject::tr("%1 x %2 px").arg(physicalSize.width()).arg(physicalSize.height());
}

QRect dimensionInfoPanelRect(const QRect& selectionRect,
                             const QString& label,
                             const QFont& baseFont,
                             const QSize& viewportSize)
{
    const QRect sel = selectionRect.normalized();
    if (!sel.isValid() || sel.isEmpty()) {
        return {};
    }

    QFont font(baseFont);
    font.setPointSize(12);
    QFontMetrics fm(font);

    const QString maxWidthText = QObject::tr("%1 x %2 px").arg(99999).arg(99999);
    const int fixedWidth = fm.horizontalAdvance(maxWidthText) + kDimensionPanelPadding;
    const int actualWidth = fm.horizontalAdvance(label) + kDimensionPanelPadding;
    const int width = qMax(fixedWidth, actualWidth);

    QRect textRect(0, 0, width, kDimensionPanelHeight);

    int textX = sel.left();
    int textY = sel.top() - textRect.height() - kDimensionPanelTopGap;
    if (textY < kDimensionPanelInset) {
        textY = sel.top() + kDimensionPanelInset;
        textX = sel.left() + kDimensionPanelInset;
    }

    textRect.moveTo(textX, textY);

    const int maxX = viewportSize.width() - kDimensionPanelInset;
    const int maxY = viewportSize.height() - kDimensionPanelInset;
    if (textRect.right() > maxX) {
        textRect.moveRight(maxX);
    }
    if (textRect.left() < kDimensionPanelInset) {
        textRect.moveLeft(kDimensionPanelInset);
    }
    if (textRect.bottom() > maxY) {
        textRect.moveBottom(maxY);
    }
    if (textRect.top() < kDimensionPanelInset) {
        textRect.moveTop(kDimensionPanelInset);
    }

    return textRect;
}

QRect selectionVisualRect(const QRect& selectionRect, const QSize& viewportSize, qreal dpr)
{
    const QRect chromeRect = selectionChromeBounds(selectionRect);
    if (!chromeRect.isValid() || chromeRect.isEmpty()) {
        return {};
    }

    QFont font;
    font.setPointSize(12);
    const QRect dimRect = dimensionInfoPanelRect(
        selectionRect,
        selectionSizeLabel(selectionRect, dpr),
        font,
        viewportSize);
    return dimRect.isValid() ? chromeRect.united(dimRect) : chromeRect;
}

} // namespace

SelectionPreviewOverlay::SelectionPreviewOverlay(QWidget* parent)
    : QWidget(nullptr,
              Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                  Qt::WindowTransparentForInput | Qt::NoDropShadowWindowHint)
{
    Q_UNUSED(parent);

    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);
    hide();
}

void SelectionPreviewOverlay::syncToHost(QWidget* host,
                                         const QRect& selectionRect,
                                         const QPixmap* backgroundPixmap,
                                         qreal devicePixelRatio,
                                         int cornerRadius,
                                         bool shouldShow)
{
    m_host = host;
    m_backgroundPixmap = backgroundPixmap;
    m_selectionRect = selectionRect.normalized();
    m_devicePixelRatio = devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;
    m_cornerRadius = cornerRadius;

    if (!m_host || !m_host->isVisible() || !m_backgroundPixmap || m_backgroundPixmap->isNull() ||
        !shouldShow || !m_selectionRect.isValid() || m_selectionRect.isEmpty()) {
        hideOverlay();
        return;
    }

    m_visualRect = selectionVisualRect(m_selectionRect, m_host->size(), m_devicePixelRatio);
    if (!m_visualRect.isValid() || m_visualRect.isEmpty()) {
        hideOverlay();
        return;
    }

    const QRect globalRect(m_host->mapToGlobal(m_visualRect.topLeft()), m_visualRect.size());
    if (geometry() != globalRect) {
        setGeometry(globalRect);
    }

    if (!isVisible()) {
        show();
    }

    update();
}

void SelectionPreviewOverlay::hideOverlay()
{
    m_visualRect = QRect();
    hide();
}

void SelectionPreviewOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    if (!m_backgroundPixmap || m_backgroundPixmap->isNull() ||
        !m_selectionRect.isValid() || !m_visualRect.isValid()) {
        return;
    }

    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRect localSelectionRect = m_selectionRect.translated(-m_visualRect.topLeft());
    const int maxRadius = qMin(localSelectionRect.width(), localSelectionRect.height()) / 2;
    const int radius = qMin(m_cornerRadius, maxRadius);

    QPen borderPen(QColor(0, 174, 255), kSelectionBorderWidth);
    borderPen.setJoinStyle(Qt::MiterJoin);
    borderPen.setCapStyle(Qt::SquareCap);
    painter.setPen(borderPen);
    painter.setBrush(Qt::NoBrush);
    const QRectF borderRect = alignedSelectionBorderRect(localSelectionRect, borderPen.widthF(), m_devicePixelRatio);
    if (radius > 0) {
        painter.drawRoundedRect(borderRect, radius, radius);
    } else {
        painter.drawRect(borderRect);
    }

    painter.setBrush(QColor(0, 174, 255));
    painter.setPen(Qt::white);
    auto drawHandle = [&](int x, int y) {
        painter.drawEllipse(QPoint(x, y), kSelectionHandleRadius, kSelectionHandleRadius);
    };
    if (radius < 10) {
        drawHandle(localSelectionRect.left(), localSelectionRect.top());
        drawHandle(localSelectionRect.right(), localSelectionRect.top());
        drawHandle(localSelectionRect.left(), localSelectionRect.bottom());
        drawHandle(localSelectionRect.right(), localSelectionRect.bottom());
    }
    drawHandle(localSelectionRect.center().x(), localSelectionRect.top());
    drawHandle(localSelectionRect.center().x(), localSelectionRect.bottom());
    drawHandle(localSelectionRect.left(), localSelectionRect.center().y());
    drawHandle(localSelectionRect.right(), localSelectionRect.center().y());

    const QString dimensions = selectionSizeLabel(m_selectionRect, m_devicePixelRatio);
    QFont font = painter.font();
    font.setPointSize(12);
    painter.setFont(font);
    QRect dimRect = dimensionInfoPanelRect(
        m_selectionRect,
        dimensions,
        font,
        m_host ? m_host->size() : size());
    dimRect.translate(-m_visualRect.topLeft());

    auto styleConfig = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
    GlassRenderer::drawGlassPanel(painter, dimRect, styleConfig, 6);
    painter.setPen(styleConfig.textColor);
    painter.drawText(dimRect, Qt::AlignCenter, dimensions);
}

void SelectionPreviewOverlay::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    setWindowClickThrough(this, true);
    setWindowExcludedFromCapture(this, true);
    setWindowVisibleOnAllWorkspaces(this, true);
    raiseWindowAboveOverlays(this);
    raise();
}
