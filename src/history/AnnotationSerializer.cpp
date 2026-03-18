#include "history/AnnotationSerializer.h"

#include "annotations/AnnotationItem.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/EmojiStickerAnnotation.h"
#include "annotations/ErasedItemsGroup.h"
#include "annotations/MarkerStroke.h"
#include "annotations/MosaicRectAnnotation.h"
#include "annotations/MosaicStroke.h"
#include "annotations/PencilStroke.h"
#include "annotations/PolylineAnnotation.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/StepBadgeAnnotation.h"
#include "annotations/TextBoxAnnotation.h"

#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPoint>
#include <QPointF>

namespace {

QJsonArray serializePoint(const QPoint& point)
{
    return QJsonArray{point.x(), point.y()};
}

QJsonArray serializePointF(const QPointF& point)
{
    return QJsonArray{point.x(), point.y()};
}

QJsonArray serializeRect(const QRect& rect)
{
    return QJsonArray{rect.x(), rect.y(), rect.width(), rect.height()};
}

QJsonArray serializeRectF(const QRectF& rect)
{
    return QJsonArray{rect.x(), rect.y(), rect.width(), rect.height()};
}

QJsonArray serializePoints(const QVector<QPoint>& points)
{
    QJsonArray result;
    for (const QPoint& point : points) {
        result.append(serializePoint(point));
    }
    return result;
}

QJsonArray serializePointsF(const QVector<QPointF>& points)
{
    QJsonArray result;
    for (const QPointF& point : points) {
        result.append(serializePointF(point));
    }
    return result;
}

QJsonArray serializeColor(const QColor& color)
{
    return QJsonArray{color.red(), color.green(), color.blue(), color.alpha()};
}

QPoint deserializePoint(const QJsonValue& value)
{
    const QJsonArray array = value.toArray();
    return QPoint(array.size() > 0 ? array.at(0).toInt() : 0,
                  array.size() > 1 ? array.at(1).toInt() : 0);
}

QPointF deserializePointF(const QJsonValue& value)
{
    const QJsonArray array = value.toArray();
    return QPointF(array.size() > 0 ? array.at(0).toDouble() : 0.0,
                   array.size() > 1 ? array.at(1).toDouble() : 0.0);
}

QRect deserializeRect(const QJsonValue& value)
{
    const QJsonArray array = value.toArray();
    return QRect(array.size() > 0 ? array.at(0).toInt() : 0,
                 array.size() > 1 ? array.at(1).toInt() : 0,
                 array.size() > 2 ? array.at(2).toInt() : 0,
                 array.size() > 3 ? array.at(3).toInt() : 0);
}

QRectF deserializeRectF(const QJsonValue& value)
{
    const QJsonArray array = value.toArray();
    return QRectF(array.size() > 0 ? array.at(0).toDouble() : 0.0,
                  array.size() > 1 ? array.at(1).toDouble() : 0.0,
                  array.size() > 2 ? array.at(2).toDouble() : 0.0,
                  array.size() > 3 ? array.at(3).toDouble() : 0.0);
}

QVector<QPoint> deserializePoints(const QJsonValue& value)
{
    QVector<QPoint> points;
    const QJsonArray array = value.toArray();
    points.reserve(array.size());
    for (const QJsonValue& pointValue : array) {
        points.append(deserializePoint(pointValue));
    }
    return points;
}

QVector<QPointF> deserializePointsF(const QJsonValue& value)
{
    QVector<QPointF> points;
    const QJsonArray array = value.toArray();
    points.reserve(array.size());
    for (const QJsonValue& pointValue : array) {
        points.append(deserializePointF(pointValue));
    }
    return points;
}

QColor deserializeColor(const QJsonValue& value)
{
    const QJsonArray array = value.toArray();
    return QColor(array.size() > 0 ? array.at(0).toInt() : 0,
                  array.size() > 1 ? array.at(1).toInt() : 0,
                  array.size() > 2 ? array.at(2).toInt() : 0,
                  array.size() > 3 ? array.at(3).toInt() : 255);
}

QJsonObject serializeAnnotationItem(const AnnotationItem* item)
{
    if (!item) {
        return {};
    }

    if (const auto* text = dynamic_cast<const TextBoxAnnotation*>(item)) {
        return QJsonObject{
            {"type", QStringLiteral("text_box")},
            {"position", serializePointF(text->position())},
            {"box", serializeRectF(text->box())},
            {"text", text->text()},
            {"font", text->font().toString()},
            {"color", serializeColor(text->color())},
            {"rotation", text->rotation()},
            {"scale", text->scale()},
            {"mirrorX", text->mirrorX()},
            {"mirrorY", text->mirrorY()},
        };
    }

    if (const auto* shape = dynamic_cast<const ShapeAnnotation*>(item)) {
        return QJsonObject{
            {"type", QStringLiteral("shape")},
            {"rect", serializeRectF(shape->rectF())},
            {"shapeType", static_cast<int>(shape->shapeType())},
            {"color", serializeColor(shape->color())},
            {"width", shape->width()},
            {"filled", shape->filled()},
            {"rotation", shape->rotation()},
            {"scaleX", shape->scaleX()},
            {"scaleY", shape->scaleY()},
        };
    }

    if (const auto* arrow = dynamic_cast<const ArrowAnnotation*>(item)) {
        return QJsonObject{
            {"type", QStringLiteral("arrow")},
            {"start", serializePoint(arrow->start())},
            {"end", serializePoint(arrow->end())},
            {"controlPoint", serializePoint(arrow->controlPoint())},
            {"color", serializeColor(arrow->color())},
            {"width", arrow->width()},
            {"lineEndStyle", static_cast<int>(arrow->lineEndStyle())},
            {"lineStyle", static_cast<int>(arrow->lineStyle())},
        };
    }

    if (const auto* polyline = dynamic_cast<const PolylineAnnotation*>(item)) {
        return QJsonObject{
            {"type", QStringLiteral("polyline")},
            {"points", serializePoints(polyline->points())},
            {"color", serializeColor(polyline->color())},
            {"width", polyline->width()},
            {"lineEndStyle", static_cast<int>(polyline->lineEndStyle())},
            {"lineStyle", static_cast<int>(polyline->lineStyle())},
        };
    }

    if (const auto* pencil = dynamic_cast<const PencilStroke*>(item)) {
        return QJsonObject{
            {"type", QStringLiteral("pencil")},
            {"points", serializePointsF(pencil->points())},
            {"color", serializeColor(pencil->color())},
            {"width", pencil->width()},
            {"lineStyle", static_cast<int>(pencil->lineStyle())},
        };
    }

    if (const auto* marker = dynamic_cast<const MarkerStroke*>(item)) {
        return QJsonObject{
            {"type", QStringLiteral("marker")},
            {"points", serializePointsF(marker->points())},
            {"color", serializeColor(marker->color())},
            {"width", marker->width()},
        };
    }

    if (const auto* mosaicRect = dynamic_cast<const MosaicRectAnnotation*>(item)) {
        return QJsonObject{
            {"type", QStringLiteral("mosaic_rect")},
            {"rect", serializeRect(mosaicRect->rect())},
            {"blockSize", mosaicRect->blockSize()},
            {"blurType", static_cast<int>(mosaicRect->blurType())},
        };
    }

    if (const auto* mosaicStroke = dynamic_cast<const MosaicStroke*>(item)) {
        return QJsonObject{
            {"type", QStringLiteral("mosaic_stroke")},
            {"points", serializePoints(mosaicStroke->points())},
            {"width", mosaicStroke->width()},
            {"blockSize", mosaicStroke->blockSize()},
            {"blurType", static_cast<int>(mosaicStroke->blurType())},
        };
    }

    if (const auto* badge = dynamic_cast<const StepBadgeAnnotation*>(item)) {
        return QJsonObject{
            {"type", QStringLiteral("step_badge")},
            {"position", serializePoint(badge->position())},
            {"color", serializeColor(badge->color())},
            {"number", badge->number()},
            {"radius", badge->radius()},
            {"rotation", badge->rotation()},
            {"mirrorX", badge->mirrorX()},
            {"mirrorY", badge->mirrorY()},
        };
    }

    if (const auto* emoji = dynamic_cast<const EmojiStickerAnnotation*>(item)) {
        return QJsonObject{
            {"type", QStringLiteral("emoji")},
            {"position", serializePoint(emoji->position())},
            {"emoji", emoji->emoji()},
            {"scale", emoji->scale()},
            {"rotation", emoji->rotation()},
            {"mirrorX", emoji->mirrorX()},
            {"mirrorY", emoji->mirrorY()},
        };
    }

    return {};
}

std::unique_ptr<AnnotationItem> deserializeAnnotationItem(const QJsonObject& object,
                                                          SharedPixmap sourcePixmap,
                                                          QString* errorMessage)
{
    const QString type = object.value(QStringLiteral("type")).toString();

    if (type == QStringLiteral("text_box")) {
        QFont font;
        font.fromString(object.value(QStringLiteral("font")).toString());
        auto item = std::make_unique<TextBoxAnnotation>(
            deserializePointF(object.value(QStringLiteral("position"))),
            object.value(QStringLiteral("text")).toString(),
            font,
            deserializeColor(object.value(QStringLiteral("color"))));
        item->setRotation(object.value(QStringLiteral("rotation")).toDouble());
        item->setScale(object.value(QStringLiteral("scale")).toDouble(1.0));
        item->setMirror(object.value(QStringLiteral("mirrorX")).toBool(),
                        object.value(QStringLiteral("mirrorY")).toBool());
        item->setBox(deserializeRectF(object.value(QStringLiteral("box"))));
        return item;
    }

    if (type == QStringLiteral("shape")) {
        auto item = std::make_unique<ShapeAnnotation>(
            deserializeRectF(object.value(QStringLiteral("rect"))).toAlignedRect(),
            static_cast<ShapeType>(object.value(QStringLiteral("shapeType")).toInt()),
            deserializeColor(object.value(QStringLiteral("color"))),
            object.value(QStringLiteral("width")).toInt(),
            object.value(QStringLiteral("filled")).toBool());
        item->setRotation(object.value(QStringLiteral("rotation")).toDouble());
        item->setScale(object.value(QStringLiteral("scaleX")).toDouble(1.0),
                       object.value(QStringLiteral("scaleY")).toDouble(1.0));
        return item;
    }

    if (type == QStringLiteral("arrow")) {
        auto item = std::make_unique<ArrowAnnotation>(
            deserializePoint(object.value(QStringLiteral("start"))),
            deserializePoint(object.value(QStringLiteral("end"))),
            deserializeColor(object.value(QStringLiteral("color"))),
            object.value(QStringLiteral("width")).toInt(),
            static_cast<LineEndStyle>(object.value(QStringLiteral("lineEndStyle")).toInt()),
            static_cast<LineStyle>(object.value(QStringLiteral("lineStyle")).toInt()));
        item->setControlPoint(deserializePoint(object.value(QStringLiteral("controlPoint"))));
        return item;
    }

    if (type == QStringLiteral("polyline")) {
        return std::make_unique<PolylineAnnotation>(
            deserializePoints(object.value(QStringLiteral("points"))),
            deserializeColor(object.value(QStringLiteral("color"))),
            object.value(QStringLiteral("width")).toInt(),
            static_cast<LineEndStyle>(object.value(QStringLiteral("lineEndStyle")).toInt()),
            static_cast<LineStyle>(object.value(QStringLiteral("lineStyle")).toInt()));
    }

    if (type == QStringLiteral("pencil")) {
        return std::make_unique<PencilStroke>(
            deserializePointsF(object.value(QStringLiteral("points"))),
            deserializeColor(object.value(QStringLiteral("color"))),
            object.value(QStringLiteral("width")).toInt(),
            static_cast<LineStyle>(object.value(QStringLiteral("lineStyle")).toInt()));
    }

    if (type == QStringLiteral("marker")) {
        return std::make_unique<MarkerStroke>(
            deserializePointsF(object.value(QStringLiteral("points"))),
            deserializeColor(object.value(QStringLiteral("color"))),
            object.value(QStringLiteral("width")).toInt());
    }

    if (type == QStringLiteral("mosaic_rect")) {
        if (!sourcePixmap) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Missing source pixmap for mosaic rectangle");
            }
            return nullptr;
        }
        return std::make_unique<MosaicRectAnnotation>(
            deserializeRect(object.value(QStringLiteral("rect"))),
            sourcePixmap,
            object.value(QStringLiteral("blockSize")).toInt(12),
            static_cast<MosaicBlurType>(object.value(QStringLiteral("blurType")).toInt()));
    }

    if (type == QStringLiteral("mosaic_stroke")) {
        if (!sourcePixmap) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Missing source pixmap for mosaic stroke");
            }
            return nullptr;
        }
        return std::make_unique<MosaicStroke>(
            deserializePoints(object.value(QStringLiteral("points"))),
            sourcePixmap,
            object.value(QStringLiteral("width")).toInt(24),
            object.value(QStringLiteral("blockSize")).toInt(12),
            static_cast<MosaicBlurType>(object.value(QStringLiteral("blurType")).toInt()));
    }

    if (type == QStringLiteral("step_badge")) {
        auto item = std::make_unique<StepBadgeAnnotation>(
            deserializePoint(object.value(QStringLiteral("position"))),
            deserializeColor(object.value(QStringLiteral("color"))),
            object.value(QStringLiteral("number")).toInt(1),
            object.value(QStringLiteral("radius")).toInt(StepBadgeAnnotation::kBadgeRadiusMedium));
        item->setRotation(object.value(QStringLiteral("rotation")).toDouble());
        item->setMirror(object.value(QStringLiteral("mirrorX")).toBool(),
                        object.value(QStringLiteral("mirrorY")).toBool());
        return item;
    }

    if (type == QStringLiteral("emoji")) {
        auto item = std::make_unique<EmojiStickerAnnotation>(
            deserializePoint(object.value(QStringLiteral("position"))),
            object.value(QStringLiteral("emoji")).toString(),
            object.value(QStringLiteral("scale")).toDouble(1.0));
        item->setRotation(object.value(QStringLiteral("rotation")).toDouble());
        item->setMirror(object.value(QStringLiteral("mirrorX")).toBool(),
                        object.value(QStringLiteral("mirrorY")).toBool());
        return item;
    }

    if (errorMessage) {
        *errorMessage = QStringLiteral("Unsupported annotation type: %1").arg(type);
    }
    return nullptr;
}

} // namespace

namespace SnapTray {

QByteArray serializeAnnotationLayer(const AnnotationLayer& layer)
{
    QJsonArray items;
    layer.forEachItem([&items](const AnnotationItem* item) {
        if (!item || !item->isVisible() || dynamic_cast<const ErasedItemsGroup*>(item) != nullptr) {
            return;
        }

        const QJsonObject object = serializeAnnotationItem(item);
        if (!object.isEmpty()) {
            items.append(object);
        }
    });

    const QJsonObject root{{QStringLiteral("items"), items}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool deserializeAnnotationLayer(const QByteArray& data,
                                AnnotationLayer* layer,
                                SharedPixmap sourcePixmap,
                                QString* errorMessage)
{
    if (!layer) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Annotation layer is null");
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage) {
            *errorMessage = parseError.error != QJsonParseError::NoError
                ? parseError.errorString()
                : QStringLiteral("Annotation payload is not an object");
        }
        return false;
    }

    const QJsonArray items = document.object().value(QStringLiteral("items")).toArray();
    layer->clear();

    for (const QJsonValue& value : items) {
        const QJsonObject object = value.toObject();
        auto item = deserializeAnnotationItem(object, sourcePixmap, errorMessage);
        if (!item) {
            layer->clear();
            return false;
        }
        layer->addItem(std::move(item));
    }

    return true;
}

} // namespace SnapTray
