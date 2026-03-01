#ifndef SNAPTRAY_MCP_TOOLS_H
#define SNAPTRAY_MCP_TOOLS_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

class QObject;
class PinWindowManager;

namespace SnapTray {
namespace MCP {

struct ToolCallContext {
    PinWindowManager* pinWindowManager = nullptr;
    QObject* parentObject = nullptr;
};

struct ToolCallResult {
    bool success = false;
    QJsonObject output;
    QString errorMessage;
};

QJsonArray buildToolDefinitions();
ToolCallResult callTool(const QString& name, const QJsonObject& arguments, const ToolCallContext& context);

namespace ToolImpl {
ToolCallResult captureScreenshot(const QJsonObject& arguments, const ToolCallContext& context);
ToolCallResult pinImage(const QJsonObject& arguments, const ToolCallContext& context);
ToolCallResult shareUpload(const QJsonObject& arguments, const ToolCallContext& context);
} // namespace ToolImpl

} // namespace MCP
} // namespace SnapTray

#endif // SNAPTRAY_MCP_TOOLS_H
