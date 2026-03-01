#include "mcp/MCPTools.h"

namespace SnapTray {
namespace MCP {

QJsonArray buildToolDefinitions()
{
    QJsonArray tools;

    tools.append(QJsonObject{
        {"name", "capture_screenshot"},
        {"description", "Capture full screen or a specific region and save a PNG file."},
        {"inputSchema",
            QJsonObject{
                {"type", "object"},
                {"properties",
                    QJsonObject{
                        {"screen", QJsonObject{{"type", "integer"}, {"minimum", 0}}},
                        {"region",
                            QJsonObject{
                                {"type", "object"},
                                {"properties",
                                    QJsonObject{
                                        {"x", QJsonObject{{"type", "integer"}}},
                                        {"y", QJsonObject{{"type", "integer"}}},
                                        {"width", QJsonObject{{"type", "integer"}, {"minimum", 1}}},
                                        {"height", QJsonObject{{"type", "integer"}, {"minimum", 1}}},
                                    }},
                                {"required", QJsonArray{"x", "y", "width", "height"}},
                                {"additionalProperties", false},
                            }},
                        {"delay_ms", QJsonObject{{"type", "integer"}, {"minimum", 0}}},
                        {"output_path", QJsonObject{{"type", "string"}}},
                    }},
                {"additionalProperties", false},
            }},
    });

    tools.append(QJsonObject{
        {"name", "pin_image"},
        {"description", "Pin an existing image file as a floating pin window."},
        {"inputSchema",
            QJsonObject{
                {"type", "object"},
                {"properties",
                    QJsonObject{
                        {"image_path", QJsonObject{{"type", "string"}}},
                        {"x", QJsonObject{{"type", "integer"}}},
                        {"y", QJsonObject{{"type", "integer"}}},
                        {"center", QJsonObject{{"type", "boolean"}}},
                    }},
                {"required", QJsonArray{"image_path"}},
                {"additionalProperties", false},
            }},
    });

    tools.append(QJsonObject{
        {"name", "share_upload"},
        {"description", "Upload an image file and return a share URL."},
        {"inputSchema",
            QJsonObject{
                {"type", "object"},
                {"properties",
                    QJsonObject{
                        {"image_path", QJsonObject{{"type", "string"}}},
                        {"password", QJsonObject{{"type", "string"}}},
                    }},
                {"required", QJsonArray{"image_path"}},
                {"additionalProperties", false},
            }},
    });

    return tools;
}

ToolCallResult callTool(const QString& name, const QJsonObject& arguments, const ToolCallContext& context)
{
    if (name == "capture_screenshot") {
        return ToolImpl::captureScreenshot(arguments, context);
    }
    if (name == "pin_image") {
        return ToolImpl::pinImage(arguments, context);
    }
    if (name == "share_upload") {
        return ToolImpl::shareUpload(arguments, context);
    }

    return ToolCallResult{false, {}, QStringLiteral("Unknown tool: %1").arg(name)};
}

} // namespace MCP
} // namespace SnapTray
