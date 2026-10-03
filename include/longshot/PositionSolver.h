#pragma once

#include "longshot/LongshotTypes.h"

namespace SnapTray::Longshot {

// Turns pairwise shift observations into one page position per frame.
//
// Model: position[to] - position[from] = dy, weighted by confidence. Solved
// by weighted least squares (Gauss-Seidel on the normal equations, which is
// exact for this Laplacian system and needs no external library). Edges whose
// residual exceeds maxResidualPx are dropped one at a time (worst first) and
// the system re-solved, so a wrong loop closure cannot bend a good chain.
// Frames not connected to the largest island get no position; the first frame
// time of each dropped island is reported as a break. The kept island is
// anchored so its first frame sits at position 0. Positions are rounded to
// the nearest integer row.
class PositionSolver
{
public:
    static SolveResult solve(const std::vector<qint64>& frameTimesMs,
                             const std::vector<PairShift>& edges,
                             double maxResidualPx);
};

} // namespace SnapTray::Longshot
