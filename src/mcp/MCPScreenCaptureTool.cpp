#include "mcp/MCPTools.h"

#include "ImageColorSpaceHelper.h"
#include "utils/CoordinateHelper.h"
#include "utils/ImageSaveUtils.h"

#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QScreen>
#include <QStandardPaths>
#include <QThread>

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
            || number < static_cast<double>(std::numeric_limits<int>::min())
            || number > static_cast<double>(std::numeric_limits<int>::max())
            || std::floor(number) != number) {
            return false;
        }
        *outValue = static_cast<int>(number);
        return true;
    }

    if (value.isString()) {
        bool ok = false;
        const int parsedValue = value.toString().toInt(&ok);
        if (!ok) {
            return false;
        }
        *outValue = parsedValue;
        return true;
    }

    return false;
}

bool readRequiredInt(const QJsonObject& object, const QString& key, int* outValue)
{
    return object.contains(key) && readOptionalInt(object, key, outValue);
}

QString saveScreenshotToFile(
    const QPixmap& screenshot, QScreen* sourceScreen, const QString& requestedOutputPath)
{
    QString filePath = requestedOutputPath;
    if (filePath.isEmpty()) {
        QString tempRoot = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        if (tempRoot.isEmpty()) {
            tempRoot = QDir::tempPath();
        }

        const QString mcpTempDir = QDir(tempRoot).filePath(QStringLiteral("snaptray/mcp"));
        const QString timestamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz");
        const QString randomSuffix = QString::number(QRandomGenerator::global()->generate(), 16);
        const QString fileName = QStringLiteral("screenshot_%1_%2.png").arg(timestamp, randomSuffix);
        filePath = QDir(mcpTempDir).filePath(fileName);
    }

    filePath = QFileInfo(filePath).absoluteFilePath();
    QDir().mkpath(QFileInfo(filePath).absolutePath());

    QImage taggedImage = tagImageWithScreenColorSpace(
        screenshot.toImage().convertToFormat(QImage::Format_RGB32), sourceScreen);

    ImageSaveUtils::Error saveError;
    const bool saved = ImageSaveUtils::saveImageAtomically(
        taggedImage, filePath, QByteArrayLiteral("PNG"), &saveError);

    if (!saved) {
        const QString detail = saveError.stage.isEmpty()
            ? (saveError.message.isEmpty() ? QStringLiteral("Unknown error") : saveError.message)
            : QStringLiteral("%1: %2").arg(saveError.stage, saveError.message);
        return QStringLiteral("ERROR:%1").arg(detail);
    }

    return filePath;
}

} // namespace

ToolCallResult ToolImpl::captureScreenshot(const QJsonObject& arguments, const ToolCallContext&)
{
    int screenIndex = -1;
    if (arguments.contains("screen")) {
        if (!readOptionalInt(arguments, "screen", &screenIndex) || screenIndex < 0) {
            return ToolCallResult{false, {}, QStringLiteral("'screen' must be a non-negative integer")};
        }
    }

    int delayMs = 0;
    if (arguments.contains("delay_ms")) {
        if (!readOptionalInt(arguments, "delay_ms", &delayMs) || delayMs < 0) {
            return ToolCallResult{false, {}, QStringLiteral("'delay_ms' must be a non-negative integer")};
        }
    }

    QRect logicalRegion;
    bool hasRegion = false;
    if (arguments.contains("region")) {
        const QJsonValue regionValue = arguments.value("region");
        if (!regionValue.isObject()) {
            return ToolCallResult{false, {}, QStringLiteral("'region' must be an object")};
        }

        const QJsonObject regionObject = regionValue.toObject();
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        if (!readRequiredInt(regionObject, "x", &x) || !readRequiredInt(regionObject, "y", &y)
            || !readRequiredInt(regionObject, "width", &width)
            || !readRequiredInt(regionObject, "height", &height)) {
            return ToolCallResult{
                false, {}, QStringLiteral("'region' requires integer x, y, width, and height")};
        }

        if (width <= 0 || height <= 0) {
            return ToolCallResult{false, {}, QStringLiteral("'region.width' and 'region.height' must be > 0")};
        }

        logicalRegion = QRect(x, y, width, height);
        hasRegion = true;
    }

    QString outputPath;
    if (arguments.contains("output_path")) {
        const QJsonValue outputValue = arguments.value("output_path");
        if (!outputValue.isString()) {
            return ToolCallResult{false, {}, QStringLiteral("'output_path' must be a string")};
        }
        outputPath = outputValue.toString().trimmed();
    }

    if (delayMs > 0) {
        QThread::msleep(delayMs);
    }

    const QList<QScreen*> screens = QGuiApplication::screens();
    if (screens.isEmpty()) {
        return ToolCallResult{false, {}, QStringLiteral("No screen available")};
    }

    QScreen* targetScreen = nullptr;
    if (screenIndex >= 0) {
        if (screenIndex >= screens.size()) {
            return ToolCallResult{
                false,
                {},
                QStringLiteral("Invalid screen index: %1 (available: 0-%2)")
                    .arg(screenIndex)
                    .arg(screens.size() - 1)};
        }
        targetScreen = screens.at(screenIndex);
    }
    else {
        targetScreen = QGuiApplication::screenAt(QCursor::pos());
        if (!targetScreen) {
            targetScreen = QGuiApplication::primaryScreen();
        }
    }

    if (!targetScreen) {
        return ToolCallResult{false, {}, QStringLiteral("No target screen available")};
    }

    QPixmap screenshot = targetScreen->grabWindow(0);
    if (screenshot.isNull()) {
        return ToolCallResult{false, {}, QStringLiteral("Failed to capture screen")};
    }

    if (hasRegion) {
        const qreal dpr = screenshot.devicePixelRatio() > 0.0 ? screenshot.devicePixelRatio() : 1.0;
        const QSize logicalScreenSize = CoordinateHelper::toLogical(screenshot.size(), dpr);
        const QRect logicalScreenRect(QPoint(0, 0), logicalScreenSize);
        if (!logicalScreenRect.contains(logicalRegion)) {
            return ToolCallResult{
                false,
                {},
                QStringLiteral("Region (%1,%2,%3,%4) exceeds logical screen bounds (%5x%6)")
                    .arg(logicalRegion.x())
                    .arg(logicalRegion.y())
                    .arg(logicalRegion.width())
                    .arg(logicalRegion.height())
                    .arg(logicalScreenRect.width())
                    .arg(logicalScreenRect.height())};
        }

        const QRect physicalRegion = CoordinateHelper::toPhysicalCoveringRect(logicalRegion, dpr);
        const QRect clampedPhysicalRegion = physicalRegion.intersected(screenshot.rect());
        if (clampedPhysicalRegion.isEmpty()) {
            return ToolCallResult{false, {}, QStringLiteral("Failed to crop region")};
        }

        screenshot = screenshot.copy(clampedPhysicalRegion);
        screenshot.setDevicePixelRatio(dpr);
    }

    const QString savedPath = saveScreenshotToFile(screenshot, targetScreen, outputPath);
    if (savedPath.startsWith("ERROR:")) {
        return ToolCallResult{false, {}, savedPath.mid(6)};
    }

    const qreal dpr = screenshot.devicePixelRatio() > 0.0 ? screenshot.devicePixelRatio() : 1.0;
    const QSize logicalSize = CoordinateHelper::toLogical(screenshot.size(), dpr);
    const int resolvedScreenIndex = QGuiApplication::screens().indexOf(targetScreen);

    QJsonObject output;
    output["file_path"] = savedPath;
    output["width"] = logicalSize.width();
    output["height"] = logicalSize.height();
    output["screen_index"] = resolvedScreenIndex;
    output["created_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

    return ToolCallResult{true, output, QString()};
}

} // namespace MCP
} // namespace SnapTray
