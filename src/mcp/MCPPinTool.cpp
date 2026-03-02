#include "mcp/MCPTools.h"

#include "PinWindowManager.h"
#include "utils/CoordinateHelper.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonValue>
#include <QPixmap>
#include <QScreen>

#include <cmath>
#include <limits>

namespace SnapTray {
namespace MCP {
namespace {

bool readOptionalInt(const QJsonObject& object, const QString& key, int* outValue)
{
    if (!outValue || !object.contains(key)) {
        return false;
    }

    const QJsonValue value = object.value(key);
    if (value.isDouble()) {
        const double number = value.toDouble();
        if (!std::isfinite(number)
            || number < static_cast<double>((std::numeric_limits<int>::min)())
            || number > static_cast<double>((std::numeric_limits<int>::max)())
            || std::floor(number) != number) {
            return false;
        }
        *outValue = static_cast<int>(number);
        return true;
    }

    if (value.isString()) {
        bool ok = false;
        const int parsed = value.toString().toInt(&ok);
        if (!ok) {
            return false;
        }
        *outValue = parsed;
        return true;
    }

    return false;
}

} // namespace

ToolCallResult ToolImpl::pinImage(const QJsonObject& arguments, const ToolCallContext& context)
{
    if (!context.pinWindowManager) {
        return ToolCallResult{false, {}, QStringLiteral("PinWindowManager is unavailable")};
    }

    const QJsonValue pathValue = arguments.value("image_path");
    if (!pathValue.isString()) {
        return ToolCallResult{false, {}, QStringLiteral("'image_path' is required and must be a string")};
    }

    const QString imagePath = QFileInfo(pathValue.toString()).absoluteFilePath();
    if (!QFileInfo::exists(imagePath)) {
        return ToolCallResult{false, {}, QStringLiteral("Image file not found: %1").arg(imagePath)};
    }

    QImage image(imagePath);
    if (image.isNull()) {
        return ToolCallResult{false, {}, QStringLiteral("Failed to load image: %1").arg(imagePath)};
    }

    const QPixmap pixmap = QPixmap::fromImage(image);
    if (pixmap.isNull()) {
        return ToolCallResult{false, {}, QStringLiteral("Failed to create pixmap from image")};
    }

    int x = 0;
    int y = 0;
    const bool hasX = arguments.contains("x");
    const bool hasY = arguments.contains("y");
    if (hasX && (!readOptionalInt(arguments, "x", &x))) {
        return ToolCallResult{false, {}, QStringLiteral("'x' must be an integer")};
    }
    if (hasY && (!readOptionalInt(arguments, "y", &y))) {
        return ToolCallResult{false, {}, QStringLiteral("'y' must be an integer")};
    }
    if (hasX != hasY) {
        return ToolCallResult{false, {}, QStringLiteral("'x' and 'y' must be provided together")};
    }

    bool center = true;
    if (arguments.contains("center")) {
        const QJsonValue centerValue = arguments.value("center");
        if (!centerValue.isBool()) {
            return ToolCallResult{false, {}, QStringLiteral("'center' must be a boolean")};
        }
        center = centerValue.toBool();
    }

    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        return ToolCallResult{false, {}, QStringLiteral("No valid screen available for pin window")};
    }

    const QRect screenGeometry = screen->availableGeometry();
    const qreal dpr = pixmap.devicePixelRatio() > 0.0 ? pixmap.devicePixelRatio() : 1.0;
    const QSize logicalSize = CoordinateHelper::toLogical(pixmap.size(), dpr);

    QPoint position;
    if (hasX && hasY) {
        position = QPoint(x, y);
    }
    else if (center) {
        position =
            screenGeometry.center() - QPoint(logicalSize.width() / 2, logicalSize.height() / 2);
    }
    else {
        position = screenGeometry.topLeft();
    }

    context.pinWindowManager->createPinWindow(pixmap, position);

    QJsonObject output;
    output["accepted"] = true;
    output["x"] = position.x();
    output["y"] = position.y();
    output["image_path"] = imagePath;
    return ToolCallResult{true, output, QString()};
}

} // namespace MCP
} // namespace SnapTray
