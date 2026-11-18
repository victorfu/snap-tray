#pragma once
#include <QColor>
#include <QJsonObject>
#include <QPointF>
#include <QSizeF>
#include <optional>
class QPainter;

// Minimal serializable inputs to the existing annotation renderers. Geometry
// is interpreted by the caller; no layer, tool handler, or undo state is owned.
struct ShapeParameters {
    enum Kind { Arrow, Rectangle, Ellipse, Line, Highlight, Step };
    Kind kind = Arrow;
    QPointF from{0, 0}, to{100, 100};
    QColor color{QStringLiteral("#ffcc00")};
    double width = 4; // units at a 1080-pixel shorter side
    int arrowStyle = 1;
    int number = 1;
    bool operator==(const ShapeParameters& o) const;
    QJsonObject toJson() const;
    static std::optional<ShapeParameters> fromJson(const QJsonObject& json);
    // Render at pixel geometry with a resolution-relative pen. Both screenshot
    // and recording code can construct this independent of a canvas.
    void draw(QPainter& painter, const QSizeF& referenceSize) const;
};
