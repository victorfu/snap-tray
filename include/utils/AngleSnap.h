#ifndef ANGLESNAP_H
#define ANGLESNAP_H

#include <QPoint>
#include <QtMath>

namespace AngleSnap {

/**
 * @brief Snap endpoint to nearest 45-degree angle from reference point.
 *
 * Calculates the angle from reference to endpoint, snaps it to the nearest
 * 45-degree interval (0, 45, 90, 135, 180, 225, 270, 315), and returns a new
 * endpoint at the same distance but on the snapped angle.
 *
 * @param reference The fixed reference point (start of the line segment)
 * @param endpoint The current endpoint to snap
 * @return QPoint The snapped endpoint position
 */
inline QPoint snapTo45Degrees(const QPoint& reference, const QPoint& endpoint)
{
    int dx = endpoint.x() - reference.x();
    int dy = endpoint.y() - reference.y();

    // Handle zero-length case
    if (dx == 0 && dy == 0) {
        return endpoint;
    }

    // Calculate distance
    double distance = std::sqrt(static_cast<double>(dx * dx + dy * dy));

    // Calculate angle in radians (atan2 returns -PI to PI)
    double angle = std::atan2(dy, dx);

    // Snap to nearest 45 degrees (PI/4 radians)
    const double snapAngle = M_PI / 4.0;
    double snappedAngle = std::round(angle / snapAngle) * snapAngle;

    // Calculate new endpoint at snapped angle
    int newX = reference.x() + static_cast<int>(std::round(distance * std::cos(snappedAngle)));
    int newY = reference.y() + static_cast<int>(std::round(distance * std::sin(snappedAngle)));

    return QPoint(newX, newY);
}

} // namespace AngleSnap

#endif // ANGLESNAP_H
