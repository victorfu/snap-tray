#pragma once

#include "detection/BoundarySnapIndex.h"
#include <QRect>
#include <Qt>
class QElapsedTimer;

// Synchronous, bounded local queries. No worker can publish stale generations.
class BoundarySnapController
{
public:
    struct Result { QRect rect; Qt::Edges edges; };
    void setSource(const QImage& image, qreal devicePixelRatio);
    void reset();
    Result snap(const QRect& raw, Qt::Edges movingEdges);
    quint64 generation() const { return m_generation; }

private:
    struct Lock { int logical; BoundarySnapIndex::Hit hit; };
    std::optional<Lock> query(const QRect& raw, Qt::Edge edge, std::optional<Lock> previous,
                               const QElapsedTimer& timer) const;
    BoundarySnapIndex m_index;
    qreal m_dpr = 1.0;
    quint64 m_generation = 0;
    std::optional<Lock> m_vertical;
    std::optional<Lock> m_horizontal;
};
