#include "recording/CrispCursor.h"
#include <QHash>
#include <QPainter>
#include <QSvgRenderer>
#include <memory>

static void initRecordingCursorResources()
{
    static const bool initialized = [] { Q_INIT_RESOURCE(cursor_resources); return true; }();
    Q_UNUSED(initialized);
}

namespace SnapTray {
bool drawCrispCursor(QPainter& painter, const QString& kind, QPointF hotspot, double height)
{
    struct Drawing { const char* kind; QPointF hotspot; };
    static const Drawing drawings[] = {
        {"arrow", {4, 2}}, {"text", {16, 16}}, {"pointer", {12, 2}},
        {"openHand", {16, 16}}, {"closedHand", {16, 16}}, {"crosshair", {16, 16}},
        {"resizeEW", {16, 16}}, {"resizeNS", {16, 16}}, {"resizeNWSE", {16, 16}},
        {"resizeNESW", {16, 16}}, {"move", {16, 16}}, {"wait", {16, 16}}, {"notAllowed", {16, 16}}
    };
    for (const auto& drawing : drawings) {
        if (kind != QLatin1String(drawing.kind)) continue;
        initRecordingCursorResources();
        // GUI previews and background exports do not share QObject renderers.
        thread_local QHash<QString, std::shared_ptr<QSvgRenderer>> cache;
        auto& renderer = cache[kind];
        if (!renderer) renderer = std::make_shared<QSvgRenderer>(QStringLiteral(":/cursor/recording/%1.svg").arg(kind));
        if (!renderer->isValid()) return false;
        constexpr double kDrawingEdge = 32.0;
        renderer->render(&painter, QRectF(hotspot - drawing.hotspot * (height / kDrawingEdge), QSizeF(height, height)));
        return true;
    }
    return false;
}
} // namespace SnapTray
