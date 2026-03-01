#include "mcp/MCPTools.h"

#include "share/ShareUploadClient.h"

#include <QDateTime>
#include <QEventLoop>
#include <QFileInfo>
#include <QImage>
#include <QJsonValue>
#include <QPixmap>
#include <QTimer>

namespace SnapTray {
namespace MCP {

ToolCallResult ToolImpl::shareUpload(const QJsonObject& arguments, const ToolCallContext& context)
{
    const QJsonValue pathValue = arguments.value("image_path");
    if (!pathValue.isString()) {
        return ToolCallResult{false, {}, QStringLiteral("'image_path' is required and must be a string")};
    }

    QString password;
    if (arguments.contains("password")) {
        const QJsonValue passwordValue = arguments.value("password");
        if (!passwordValue.isString()) {
            return ToolCallResult{false, {}, QStringLiteral("'password' must be a string")};
        }
        password = passwordValue.toString();
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

    ShareUploadClient client(context.parentObject);
    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);

    ToolCallResult result;
    result.success = false;

    QObject::connect(&client, &ShareUploadClient::uploadSucceeded, &loop,
        [&](const QString& url, const QDateTime& expiresAt, bool isProtected) {
            result.success = true;
            result.output["url"] = url;
            result.output["expires_at"] = expiresAt.isValid()
                ? expiresAt.toUTC().toString(Qt::ISODateWithMs)
                : QString();
            result.output["protected"] = isProtected;
            loop.quit();
        });

    QObject::connect(&client, &ShareUploadClient::uploadFailed, &loop, [&](const QString& errorMessage) {
        result.success = false;
        result.errorMessage =
            errorMessage.isEmpty() ? QStringLiteral("Upload failed") : errorMessage;
        loop.quit();
    });

    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, [&]() {
        result.success = false;
        result.errorMessage = QStringLiteral("Upload timed out after 30 seconds");
        loop.quit();
    });

    timeoutTimer.start(30000);
    client.uploadPixmap(pixmap, password);
    loop.exec();

    if (!result.success && result.errorMessage.isEmpty()) {
        result.errorMessage = QStringLiteral("Upload failed");
    }

    return result;
}

} // namespace MCP
} // namespace SnapTray
