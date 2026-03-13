#ifndef CURSORTYPES_H
#define CURSORTYPES_H

#include <QCursor>
#include <QString>

enum class CursorStyleId {
    LegacyCursor = 0,
    Arrow,
    PointingHand,
    Crosshair,
    OpenHand,
    ClosedHand,
    TextBeam,
    Move,
    ResizeHorizontal,
    ResizeVertical,
    ResizeDiagonalForward,
    ResizeDiagonalBackward,
    MosaicBrush,
    EraserBrush,
};

struct CursorStyleSpec {
    CursorStyleId styleId = CursorStyleId::Crosshair;
    QCursor legacyCursor;
    int primaryValue = 0;

    static CursorStyleSpec fromShape(Qt::CursorShape shape)
    {
        CursorStyleSpec spec;
        switch (shape) {
        case Qt::ArrowCursor:
            spec.styleId = CursorStyleId::Arrow;
            break;
        case Qt::PointingHandCursor:
            spec.styleId = CursorStyleId::PointingHand;
            break;
        case Qt::CrossCursor:
            spec.styleId = CursorStyleId::Crosshair;
            break;
        case Qt::OpenHandCursor:
            spec.styleId = CursorStyleId::OpenHand;
            break;
        case Qt::ClosedHandCursor:
            spec.styleId = CursorStyleId::ClosedHand;
            break;
        case Qt::IBeamCursor:
            spec.styleId = CursorStyleId::TextBeam;
            break;
        case Qt::SizeAllCursor:
            spec.styleId = CursorStyleId::Move;
            break;
        case Qt::SizeHorCursor:
            spec.styleId = CursorStyleId::ResizeHorizontal;
            break;
        case Qt::SizeVerCursor:
            spec.styleId = CursorStyleId::ResizeVertical;
            break;
        case Qt::SizeFDiagCursor:
            spec.styleId = CursorStyleId::ResizeDiagonalForward;
            break;
        case Qt::SizeBDiagCursor:
            spec.styleId = CursorStyleId::ResizeDiagonalBackward;
            break;
        default:
            spec.styleId = CursorStyleId::LegacyCursor;
            spec.legacyCursor = QCursor(shape);
            break;
        }

        if (spec.styleId != CursorStyleId::LegacyCursor) {
            spec.legacyCursor = QCursor(shape);
        }
        return spec;
    }

    static CursorStyleSpec fromCursor(const QCursor& cursor)
    {
        const Qt::CursorShape shape = cursor.shape();
        if (!cursor.pixmap().isNull() && shape == Qt::BitmapCursor) {
            CursorStyleSpec spec;
            spec.styleId = CursorStyleId::LegacyCursor;
            spec.legacyCursor = cursor;
            return spec;
        }

        CursorStyleSpec spec = fromShape(shape);
        if (spec.styleId == CursorStyleId::LegacyCursor) {
            spec.legacyCursor = cursor;
        }
        return spec;
    }

    bool isLegacy() const
    {
        return styleId == CursorStyleId::LegacyCursor;
    }

    bool isBrush() const
    {
        return styleId == CursorStyleId::MosaicBrush ||
               styleId == CursorStyleId::EraserBrush;
    }

    QString debugString() const
    {
        switch (styleId) {
        case CursorStyleId::LegacyCursor:
            return QStringLiteral("Legacy(%1)").arg(static_cast<int>(legacyCursor.shape()));
        case CursorStyleId::Arrow:
            return QStringLiteral("Arrow");
        case CursorStyleId::PointingHand:
            return QStringLiteral("PointingHand");
        case CursorStyleId::Crosshair:
            return QStringLiteral("Crosshair");
        case CursorStyleId::OpenHand:
            return QStringLiteral("OpenHand");
        case CursorStyleId::ClosedHand:
            return QStringLiteral("ClosedHand");
        case CursorStyleId::TextBeam:
            return QStringLiteral("TextBeam");
        case CursorStyleId::Move:
            return QStringLiteral("Move");
        case CursorStyleId::ResizeHorizontal:
            return QStringLiteral("ResizeHorizontal");
        case CursorStyleId::ResizeVertical:
            return QStringLiteral("ResizeVertical");
        case CursorStyleId::ResizeDiagonalForward:
            return QStringLiteral("ResizeDiagonalForward");
        case CursorStyleId::ResizeDiagonalBackward:
            return QStringLiteral("ResizeDiagonalBackward");
        case CursorStyleId::MosaicBrush:
            return QStringLiteral("MosaicBrush(%1)").arg(primaryValue);
        case CursorStyleId::EraserBrush:
            return QStringLiteral("EraserBrush(%1)").arg(primaryValue);
        }

        return QStringLiteral("Unknown");
    }

    bool operator==(const CursorStyleSpec& other) const
    {
        return styleId == other.styleId &&
               primaryValue == other.primaryValue &&
               legacyCursor.shape() == other.legacyCursor.shape() &&
               legacyCursor.pixmap().cacheKey() == other.legacyCursor.pixmap().cacheKey();
    }

    bool operator!=(const CursorStyleSpec& other) const
    {
        return !(*this == other);
    }
};

enum class CursorRequestSource {
    SurfaceDefault = 0,
    Tool,
    Hover,
    Selection,
    Drag,
    Overlay,
    Popup,
    TextEditor,
    LayoutMode,
    Override,
};

inline int cursorPriorityForRequestSource(CursorRequestSource source)
{
    switch (source) {
    case CursorRequestSource::SurfaceDefault:
        return 50;
    case CursorRequestSource::Tool:
        return 100;
    case CursorRequestSource::Hover:
        return 150;
    case CursorRequestSource::Selection:
        return 200;
    case CursorRequestSource::Drag:
        return 300;
    case CursorRequestSource::TextEditor:
        return 400;
    case CursorRequestSource::LayoutMode:
        return 650;
    case CursorRequestSource::Overlay:
        return 700;
    case CursorRequestSource::Override:
        return 800;
    case CursorRequestSource::Popup:
        return 900;
    }

    return 0;
}

inline QString cursorRequestSourceName(CursorRequestSource source)
{
    switch (source) {
    case CursorRequestSource::SurfaceDefault:
        return QStringLiteral("SurfaceDefault");
    case CursorRequestSource::Tool:
        return QStringLiteral("Tool");
    case CursorRequestSource::Hover:
        return QStringLiteral("Hover");
    case CursorRequestSource::Selection:
        return QStringLiteral("Selection");
    case CursorRequestSource::Drag:
        return QStringLiteral("Drag");
    case CursorRequestSource::Overlay:
        return QStringLiteral("Overlay");
    case CursorRequestSource::Popup:
        return QStringLiteral("Popup");
    case CursorRequestSource::TextEditor:
        return QStringLiteral("TextEditor");
    case CursorRequestSource::LayoutMode:
        return QStringLiteral("LayoutMode");
    case CursorRequestSource::Override:
        return QStringLiteral("Override");
    }

    return QStringLiteral("Unknown");
}

enum class CursorSurfaceMode {
    Legacy = 0,
    Shadow,
    Authority,
};

inline QString cursorSurfaceModeName(CursorSurfaceMode mode)
{
    switch (mode) {
    case CursorSurfaceMode::Legacy:
        return QStringLiteral("Legacy");
    case CursorSurfaceMode::Shadow:
        return QStringLiteral("Shadow");
    case CursorSurfaceMode::Authority:
        return QStringLiteral("Authority");
    }

    return QStringLiteral("Unknown");
}

#endif // CURSORTYPES_H
