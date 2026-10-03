#include "longshot/PositionSolver.h"

#include <QDebug>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace SnapTray::Longshot {

namespace {

// Gauss-Seidel converges geometrically on this diagonally dominant system;
// 2000 sweeps at 1e-4 is far beyond what sub-pixel rounding can observe.
constexpr int kMaxSweeps = 2000;
constexpr double kConvergenceTolerancePx = 1e-4;

struct Edge {
    int from;
    int to;
    double dy;
    double weight;
    bool active;
};

// Union-find over frames connected by active edges.
int findRoot(std::vector<int>& parent, int i)
{
    while (parent[i] != i) {
        parent[i] = parent[parent[i]];
        i = parent[i];
    }
    return i;
}

std::vector<int> components(int frameCount, const std::vector<Edge>& edges)
{
    std::vector<int> parent(frameCount);
    std::iota(parent.begin(), parent.end(), 0);
    for (const Edge& e : edges) {
        if (!e.active) continue;
        const int a = findRoot(parent, e.from);
        const int b = findRoot(parent, e.to);
        if (a != b) parent[a] = b;
    }
    std::vector<int> root(frameCount);
    for (int i = 0; i < frameCount; ++i) root[i] = findRoot(parent, i);
    return root;
}

// Weighted least squares for one island, anchored at `anchor` = 0.
std::vector<double> solveIsland(int frameCount, const std::vector<Edge>& edges,
                                const std::vector<int>& root, int islandRoot, int anchor)
{
    std::vector<double> pos(frameCount, 0.0);
    std::vector<double> diag(frameCount, 0.0);
    std::vector<std::vector<std::pair<int, double>>> neighbours(frameCount); // (other, weight)
    std::vector<double> rhs(frameCount, 0.0);
    for (const Edge& e : edges) {
        if (!e.active || root[e.from] != islandRoot) continue;
        diag[e.from] += e.weight;
        diag[e.to] += e.weight;
        neighbours[e.from].push_back({e.to, e.weight});
        neighbours[e.to].push_back({e.from, e.weight});
        rhs[e.from] -= e.weight * e.dy; // pos[from] = pos[to] - dy
        rhs[e.to] += e.weight * e.dy;   // pos[to] = pos[from] + dy
    }
    for (int sweep = 0; sweep < kMaxSweeps; ++sweep) {
        double maxDelta = 0.0;
        for (int i = 0; i < frameCount; ++i) {
            if (root[i] != islandRoot || i == anchor || diag[i] <= 0.0) continue;
            double sum = rhs[i];
            for (const auto& [other, weight] : neighbours[i]) sum += weight * pos[other];
            const double updated = sum / diag[i];
            maxDelta = std::max(maxDelta, std::abs(updated - pos[i]));
            pos[i] = updated;
        }
        if (maxDelta < kConvergenceTolerancePx) break;
    }
    return pos;
}

} // namespace

SolveResult PositionSolver::solve(const std::vector<qint64>& frameTimesMs,
                                  const std::vector<PairShift>& observations,
                                  double maxResidualPx)
{
    SolveResult result;
    const int frameCount = int(frameTimesMs.size());
    result.positions.assign(frameCount, std::nullopt);
    if (frameCount == 0) return result;

    std::vector<Edge> edges;
    edges.reserve(observations.size());
    for (const PairShift& o : observations) {
        if (o.confidence <= 0.0 || o.from < 0 || o.to < 0 || o.from >= frameCount || o.to >= frameCount
            || o.from == o.to) {
            continue;
        }
        edges.push_back({o.from, o.to, double(o.dy), o.confidence, true});
    }

    // Outlier rejection: solve, drop the single worst edge above the residual
    // cap, repeat. Dropping one at a time keeps a good chain from being
    // punished for a bad closure that it shares nodes with.
    std::vector<int> root;
    std::vector<double> pos;
    for (;;) {
        root = components(frameCount, edges);
        pos.assign(frameCount, 0.0);
        std::vector<bool> solved(frameCount, false);
        for (int i = 0; i < frameCount; ++i) {
            if (solved[i]) continue;
            const int islandRoot = root[i];
            const std::vector<double> islandPos = solveIsland(frameCount, edges, root, islandRoot, i);
            for (int j = 0; j < frameCount; ++j) {
                if (root[j] == islandRoot) {
                    pos[j] = islandPos[j];
                    solved[j] = true;
                }
            }
        }
        int worst = -1;
        double worstResidual = maxResidualPx;
        for (int k = 0; k < int(edges.size()); ++k) {
            const Edge& e = edges[k];
            if (!e.active) continue;
            const double residual = std::abs(pos[e.to] - pos[e.from] - e.dy);
            if (residual > worstResidual) {
                worstResidual = residual;
                worst = k;
            }
        }
        if (worst < 0) break;
        edges[worst].active = false;
        ++result.rejectedEdges;
        qDebug() << "PositionSolver: rejected edge" << edges[worst].from << "->" << edges[worst].to
                 << "dy" << edges[worst].dy << "residual" << worstResidual;
    }

    // Keep the largest island (ties: the one containing the earliest frame).
    std::vector<int> islandSize(frameCount, 0);
    for (int i = 0; i < frameCount; ++i) ++islandSize[root[i]];
    int keptRoot = root[0];
    for (int i = 0; i < frameCount; ++i) {
        if (islandSize[root[i]] > islandSize[keptRoot]) keptRoot = root[i];
    }
    int anchor = -1;
    for (int i = 0; i < frameCount; ++i) {
        if (root[i] == keptRoot) {
            anchor = i;
            break;
        }
    }
    const double offset = pos[anchor];
    std::vector<int> seenRoots;
    for (int i = 0; i < frameCount; ++i) {
        if (root[i] == keptRoot) {
            result.positions[i] = int(std::lround(pos[i] - offset));
        } else if (std::find(seenRoots.begin(), seenRoots.end(), root[i]) == seenRoots.end()) {
            seenRoots.push_back(root[i]);
            result.breakTimesMs.push_back(frameTimesMs[i]);
        }
    }
    std::sort(result.breakTimesMs.begin(), result.breakTimesMs.end());
    return result;
}

} // namespace SnapTray::Longshot
