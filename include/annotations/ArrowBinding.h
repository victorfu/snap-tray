#pragma once

#include <QPointF>
#include <QUuid>

// A local, normalized point on a persistent annotation, independent of z-order.
// Null target means a free endpoint; its last resolved coordinates remain on the arrow.
struct ArrowBinding
{
    QUuid targetId;
    QPointF anchor;
    bool operator==(const ArrowBinding& other) const
    { return targetId == other.targetId && (targetId.isNull() || anchor == other.anchor); }
    bool operator!=(const ArrowBinding& other) const { return !(*this == other); }
};
