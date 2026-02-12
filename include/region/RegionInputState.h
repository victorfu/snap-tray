#ifndef REGIONINPUTSTATE_H
#define REGIONINPUTSTATE_H

#include <QColor>
#include <QPoint>
#include <QRect>

#include "annotations/ArrowAnnotation.h"
#include "annotations/LineStyle.h"
#include "annotations/ShapeAnnotation.h"
#include "tools/ToolId.h"

struct RegionInputState {
    ToolId currentTool = ToolId::Selection;
    bool showSubToolbar = true;

    QRect highlightedWindowRect;
    bool hasDetectedWindow = false;

    QColor annotationColor = Qt::red;
    int annotationWidth = 3;
    LineEndStyle arrowStyle = LineEndStyle::EndArrow;
    LineStyle lineStyle = LineStyle::Solid;
    ShapeType shapeType = ShapeType::Rectangle;
    ShapeFillMode shapeFillMode = ShapeFillMode::Outline;

    bool multiRegionMode = false;
    int replaceTargetIndex = -1;

    QPoint currentPoint;
    bool isDrawing = false;
};

#endif // REGIONINPUTSTATE_H
