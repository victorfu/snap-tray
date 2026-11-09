#pragma once

#include "detection/BoundarySnapIndex.h"
#include <QRect>
#include <Qt>
class QElapsedTimer;

// Synchronous, bounded local queries over the current capture source.
class BoundarySnapController
{
public:
    struct Result { QRect rect; Qt::Edges edges; };
    static constexpr qint64 kDefaultQueryBudgetNs = 4000000;
    void setSource(const QImage& image, qreal devicePixelRatio);
    void reset();
    // A zero budget forces fallback; a large budget supports deterministic geometry tests.
    Result snap(const QRect& raw, Qt::Edges movingEdges,
                qint64 budgetNanoseconds = kDefaultQueryBudgetNs);

private:
    struct Lock { int logical; BoundarySnapIndex::Hit hit; };
    std::optional<Lock> query(const QRect& raw, Qt::Edge edge, std::optional<Lock> previous,
                               const QElapsedTimer& timer, qint64 budgetNanoseconds) const;
    BoundarySnapIndex m_index;
    qreal m_dpr = 1.0;
    std::optional<Lock> m_vertical;
    std::optional<Lock> m_horizontal;
};
