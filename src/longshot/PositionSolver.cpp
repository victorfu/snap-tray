#include "longshot/PositionSolver.h"

#include <QDebug>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace SnapTray::Longshot {

namespace {

// Conjugate gradient on the grounded weighted Laplacian: exact for this
// symmetric positive-definite system in at most n steps; the stop is on the
// residual, so a converged answer is converged everywhere, not just where
// the last sweep happened to look.
constexpr double kResidualTolerancePx = 1e-6;
constexpr int kExtraIterations = 10;        // beyond n, for rounding noise
constexpr double kResidualTieWindow = 1e-6; // residuals closer than this are tied

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
    std::vector<int> nodes; // island members except the anchor
    std::vector<int> slot(frameCount, -1);
    for (int i = 0; i < frameCount; ++i) {
        if (root[i] == islandRoot && i != anchor) {
            slot[i] = int(nodes.size());
            nodes.push_back(i);
        }
    }
    const int n = int(nodes.size());
    std::vector<double> pos(frameCount, 0.0);
    if (n == 0) return pos;
    std::vector<const Edge*> islandEdges;
    for (const Edge& e : edges) {
        if (e.active && root[e.from] == islandRoot) islandEdges.push_back(&e);
    }
    // b = L-side constant: for pos[to] - pos[from] = dy, the gradient terms.
    std::vector<double> b(n, 0.0);
    for (const Edge* e : islandEdges) {
        if (slot[e->to] >= 0) b[slot[e->to]] += e->weight * e->dy;
        if (slot[e->from] >= 0) b[slot[e->from]] -= e->weight * e->dy;
    }
    // y = A x, A = grounded weighted Laplacian (anchor row/column removed).
    auto applyA = [&](const std::vector<double>& x, std::vector<double>& y) {
        std::fill(y.begin(), y.end(), 0.0);
        for (const Edge* e : islandEdges) {
            const int a = slot[e->from];
            const int c = slot[e->to];
            const double xa = a >= 0 ? x[a] : 0.0;
            const double xc = c >= 0 ? x[c] : 0.0;
            const double d = e->weight * (xa - xc);
            if (a >= 0) y[a] += d;
            if (c >= 0) y[c] -= d;
        }
    };
    std::vector<double> x(n, 0.0), r = b, p = b, Ap(n);
    double rr = std::inner_product(r.begin(), r.end(), r.begin(), 0.0);
    for (int iteration = 0; iteration < n + kExtraIterations; ++iteration) {
        double maxResidual = 0.0;
        for (double v : r) maxResidual = std::max(maxResidual, std::abs(v));
        if (maxResidual < kResidualTolerancePx) break;
        applyA(p, Ap);
        const double pAp = std::inner_product(p.begin(), p.end(), Ap.begin(), 0.0);
        if (pAp <= 0.0) break;
        const double alpha = rr / pAp;
        for (int i = 0; i < n; ++i) { x[i] += alpha * p[i]; r[i] -= alpha * Ap[i]; }
        const double rrNext = std::inner_product(r.begin(), r.end(), r.begin(), 0.0);
        const double beta = rrNext / rr;
        rr = rrNext;
        for (int i = 0; i < n; ++i) p[i] = r[i] + beta * p[i];
    }
    for (int i = 0; i < n; ++i) pos[nodes[i]] = x[i];
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
        if (!(o.confidence > 0.0) || o.from < 0 || o.to < 0 || o.from >= frameCount || o.to >= frameCount
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
            } else if (worst >= 0 && std::abs(residual - worstResidual) <= kResidualTieWindow) {
                // Residuals tied: prefer lower confidence, then larger span
                const Edge& worstE = edges[worst];
                const int worstSpan = std::abs(worstE.to - worstE.from);
                const int thisSpan = std::abs(e.to - e.from);
                if (e.weight < worstE.weight ||
                    (std::abs(e.weight - worstE.weight) <= kResidualTieWindow && thisSpan > worstSpan)) {
                    worstResidual = residual;
                    worst = k;
                }
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
