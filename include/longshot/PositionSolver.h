#pragma once

#include "longshot/LongshotTypes.h"
#include <functional>

namespace SnapTray::Longshot {

// Turns pairwise shift observations into one page position per frame.
//
// Model: position[to] - position[from] = dy, weighted by confidence. Solved
// by weighted least squares (conjugate gradient on the grounded weighted Laplacian,
// which is exact for this symmetric positive-definite system; convergence stops on
// the residual rather than iterating a fixed count). Edges whose residual exceeds
// maxResidualPx are dropped one at a time (worst first), with ties broken by
// preferring lower confidence, then larger frame span. The system is re-solved after
// each rejection, so a wrong loop closure cannot bend a good chain. One island is
// kept: the one with the longest page extent (max - min position + frameHeight),
// then the most frames, then the earliest frame; frameHeight 0 ranks by frame
// count only. Frames outside the kept island get no position; the first frame time
// of each dropped island is reported as a break. The kept island is anchored so
// its first frame sits at position 0. Positions are rounded to the nearest integer
// row. The sections collection additionally preserves all components supported
// by at least two connected frames, ordered by their first recorded frame. Each
// section retains its own origin; positions remains the largest-island view for
// closure search compatibility. If any conjugate-gradient solve stops at its iteration cap without
// converging, SolveResult::converged is false and callers must not use the
// positions.
// A wrong closure whose misfit, spread over a long loop, stays under maxResidualPx
// on every edge cannot be detected by residuals alone; the analyzer's confidence
// margin is the defence.
class PositionSolver
{
public:
    static SolveResult solve(const std::vector<qint64>& frameTimesMs,
                             const std::vector<PairShift>& edges,
                             double maxResidualPx,
                             int frameHeight = 0,
                             const std::function<bool()>& shouldContinue = {});
};

} // namespace SnapTray::Longshot
