#include "cursor/CursorStyleCatalog.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QtMath>

#include <algorithm>

static void ensureCursorResourcesLoaded()
{
    static const bool loaded = []() {
        Q_INIT_RESOURCE(cursor_resources);
        return true;
    }();
    Q_UNUSED(loaded);
}

namespace {
constexpr qreal kCursorRenderDprFloor = 2.0;
constexpr qreal kCursorOutlineWidth = 2.0;
constexpr int kCursorPadding = 4;
constexpr int kMoveCursorLogicalSize = 16;
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
        qCeil(logicalSize.height() * dpr));

    QPixmap pixmap(physicalSize);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    return pixmap;
}

QString customCursorCacheKey(QStringView family, int logicalSize, qreal dpr)
{
    return QStringLiteral("%1:%2:%3")
        .arg(family)
        .arg(logicalSize)
        .arg(qRound64(dpr * 1000.0));
}

QCursor renderSquareBrushCursor(int size)
{
    const int boxSize = std::max(size, 2);
    const QSize logicalSize(
        boxSize + (kCursorPadding * 2),
        boxSize + (kCursorPadding * 2));
    const qreal dpr = cursorRenderDpr();
    QPixmap pixmap = createCursorCanvas(logicalSize, dpr);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(kCursorOutlineColor, kCursorOutlineWidth, Qt::SolidLine,
        Qt::SquareCap, Qt::MiterJoin));
    painter.setBrush(Qt::NoBrush);

    const qreal inset = kCursorPadding + (kCursorOutlineWidth / 2.0);
    const qreal rectExtent = std::max<qreal>(1.0, boxSize - kCursorOutlineWidth);
    painter.drawRect(QRectF(inset, inset, rectExtent, rectExtent));

    const QPoint hotspot(logicalSize.width() / 2, logicalSize.height() / 2);
    return QCursor(pixmap, hotspot.x(), hotspot.y());
}

QCursor renderCircleBrushCursor(int diameter)
{
    const int size = std::max(diameter, 2);
    const QSize logicalSize(
        size + (kCursorPadding * 2),
        size + (kCursorPadding * 2));
    const qreal dpr = cursorRenderDpr();
    QPixmap pixmap = createCursorCanvas(logicalSize, dpr);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(kCursorOutlineColor, kCursorOutlineWidth, Qt::SolidLine,
        Qt::RoundCap, Qt::RoundJoin));

    const qreal inset = kCursorPadding + (kCursorOutlineWidth / 2.0);
    const qreal ellipseExtent = std::max<qreal>(1.0, size - kCursorOutlineWidth);
    painter.drawEllipse(QRectF(inset, inset, ellipseExtent, ellipseExtent));

    const QPoint hotspot(logicalSize.width() / 2, logicalSize.height() / 2);
    return QCursor(pixmap, hotspot.x(), hotspot.y());
}

QCursor renderMoveCursor()
{
    ensureCursorResourcesLoaded();

    QPixmap basePixmap(QStringLiteral(":/cursor/sizeallcursor.png"));
    if (basePixmap.isNull()) {
        return QCursor(Qt::SizeAllCursor);
    }

    const QSize logicalSize(kMoveCursorLogicalSize, kMoveCursorLogicalSize);
    const qreal dpr = cursorRenderDpr();
    QPixmap pixmap = createCursorCanvas(logicalSize, dpr);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawPixmap(QRect(QPoint(0, 0), logicalSize), basePixmap);

    const QPoint hotspot(logicalSize.width() / 2, logicalSize.height() / 2);
    return QCursor(pixmap, hotspot.x(), hotspot.y());
}
}  // namespace

CursorStyleCatalog& CursorStyleCatalog::instance()
{
    static CursorStyleCatalog catalog;
    return catalog;
}

QCursor CursorStyleCatalog::cursorForStyle(const CursorStyleSpec& spec) const
{
    switch (spec.styleId) {
    case CursorStyleId::LegacyCursor:
        return spec.legacyCursor;
    case CursorStyleId::Arrow:
        return QCursor(Qt::ArrowCursor);
    case CursorStyleId::PointingHand:
        return QCursor(Qt::PointingHandCursor);
    case CursorStyleId::Crosshair:
        return QCursor(Qt::CrossCursor);
    case CursorStyleId::OpenHand:
        return QCursor(Qt::OpenHandCursor);
    case CursorStyleId::ClosedHand:
        return QCursor(Qt::ClosedHandCursor);
    case CursorStyleId::TextBeam:
        return QCursor(Qt::IBeamCursor);
    case CursorStyleId::Move:
#ifdef Q_OS_MACOS
        return cachedCustomCursor(QStringLiteral("move"), kMoveCursorLogicalSize);
#else
        return QCursor(Qt::SizeAllCursor);
#endif
    case CursorStyleId::ResizeHorizontal:
        return QCursor(Qt::SizeHorCursor);
    case CursorStyleId::ResizeVertical:
        return QCursor(Qt::SizeVerCursor);
    case CursorStyleId::ResizeDiagonalForward:
        return QCursor(Qt::SizeFDiagCursor);
    case CursorStyleId::ResizeDiagonalBackward:
        return QCursor(Qt::SizeBDiagCursor);
    case CursorStyleId::MosaicBrush:
        return mosaicCursor(spec.primaryValue);
    case CursorStyleId::EraserBrush:
        return eraserCursor(spec.primaryValue);
    }

    return QCursor(Qt::ArrowCursor);
}

QCursor CursorStyleCatalog::mosaicCursor(int size) const
{
    return cachedCustomCursor(QStringLiteral("mosaic"), size);
}

QCursor CursorStyleCatalog::eraserCursor(int diameter) const
{
    return cachedCustomCursor(QStringLiteral("eraser"), diameter);
}

QCursor CursorStyleCatalog::cachedCustomCursor(const QString& family, int logicalSize) const
{
    const int clampedSize = std::max(logicalSize, 2);
    const qreal dpr = cursorRenderDpr();
    const QString key = customCursorCacheKey(family, clampedSize, dpr);

    auto it = m_brushCursorCache.constFind(key);
    if (it != m_brushCursorCache.cend()) {
        return it.value();
    }

    QCursor cursor;
    if (family == QStringLiteral("mosaic")) {
        cursor = renderSquareBrushCursor(clampedSize);
    } else if (family == QStringLiteral("eraser")) {
        cursor = renderCircleBrushCursor(clampedSize);
    } else {
        cursor = renderMoveCursor();
    }

    m_brushCursorCache.insert(key, cursor);
    return cursor;
}
