#pragma once
#include <QPointF>
#include <QString>
class QPainter;
namespace SnapTray {
// Original MIT-licensed vector artwork. Unknown kinds keep the recorded image.
bool drawCrispCursor(QPainter& painter, const QString& kind, QPointF hotspot, double height);
} // namespace SnapTray
