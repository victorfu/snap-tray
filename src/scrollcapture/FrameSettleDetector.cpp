#include "scrollcapture/FrameSettleDetector.h"

#include "scrollcapture/FrameRowHashes.h"

#include <algorithm>
#include <cstring>

namespace snaptray::scrollcapture {

namespace {

constexpr int kBytesPerPixel = 4;

int tilesAlong(int pixels, int tileSize)
{
    return (pixels + tileSize - 1) / tileSize;
}

} // namespace

FrameSettleDetector::FrameSettleDetector()
    : FrameSettleDetector(Config{})
{
}

FrameSettleDetector::FrameSettleDetector(const Config& config)
    : m_config(config)
{
    m_config.tileSize = std::max(1, m_config.tileSize);
    m_config.strictPairs = std::max(1, m_config.strictPairs);
}

void FrameSettleDetector::start(const QImage& startFrame)
{
    m_result = Result{};
    m_start = toComparableFormat(startFrame);
    m_startHashes = computeRowHashes(m_start);
    m_previous = m_start;
    m_previousHashes = m_startHashes;
    m_previousMs = 0;
    m_identicalPairs = 0;
    m_identicalRunStartMs.reset();
    m_relaxedSinceMs.reset();
}

FrameSettleDetector::Result FrameSettleDetector::addFrame(const QImage& frame, qint64 elapsedMs)
{
    if (m_result.outcome != Outcome::Pending) {
        return m_result;
    }

    const QImage current = toComparableFormat(frame);
    const QVector<quint64> hashes = computeRowHashes(current);
    ++m_result.frames;

    const bool sameSizeAsStart = current.size() == m_start.size();
    if (!sameSizeAsStart || hashes != m_startHashes) {
        m_result.motionObserved = true;
    }

    const bool identical = current.size() == m_previous.size() && hashes == m_previousHashes;
    int totalTiles = 0;
    const int changedTiles = identical ? 0 : countChangedTiles(m_previous, current, m_config.tileSize, &totalTiles);
    m_result.lastChangedTiles = changedTiles;

    const bool relaxedStable = identical ||
        (changedTiles <= m_config.relaxedMaxChangedTiles &&
         totalTiles > 0 &&
         changedTiles <= m_config.relaxedMaxChangedFraction * totalTiles);
    if (relaxedStable) {
        if (!m_relaxedSinceMs.has_value()) {
            m_relaxedSinceMs = m_previousMs;
        }
    } else {
        m_relaxedSinceMs.reset();
    }

    if (identical) {
        if (m_identicalPairs == 0) {
            m_identicalRunStartMs = m_previousMs;
        }
        ++m_identicalPairs;
    } else {
        m_identicalPairs = 0;
        m_identicalRunStartMs.reset();
    }

    m_previous = current;
    m_previousHashes = hashes;
    m_previousMs = elapsedMs;

    const bool mayConcludeStill = m_result.motionObserved || elapsedMs >= m_config.noMotionGraceMs;
    if (m_identicalPairs >= m_config.strictPairs && mayConcludeStill) {
        m_result.outcome = Outcome::Settled;
        m_result.strictSettleMs = m_identicalRunStartMs;
        m_result.strictConfirmedMs = elapsedMs;
        m_result.relaxedSettleMs = m_relaxedSinceMs;
    } else if (elapsedMs >= m_config.timeoutMs) {
        m_result.outcome = Outcome::TimedOut;
        m_result.relaxedSettleMs = m_relaxedSinceMs;
    }
    return m_result;
}

int FrameSettleDetector::countChangedTiles(const QImage& a, const QImage& b, int tileSize, int* totalTiles)
{
    tileSize = std::max(1, tileSize);
    const QImage left = toComparableFormat(a);
    const QImage right = toComparableFormat(b);
    const int columns = tilesAlong(right.width(), tileSize);
    const int rows = tilesAlong(right.height(), tileSize);
    const int total = columns * rows;
    if (totalTiles) {
        *totalTiles = total;
    }
    if (left.size() != right.size()) {
        return total;
    }

    QVector<bool> changed(total, false);
    const qsizetype rowBytes = static_cast<qsizetype>(right.width()) * kBytesPerPixel;
    const qsizetype tileBytes = static_cast<qsizetype>(tileSize) * kBytesPerPixel;
    for (int y = 0; y < right.height(); ++y) {
        const uchar* leftRow = left.constScanLine(y);
        const uchar* rightRow = right.constScanLine(y);
        if (std::memcmp(leftRow, rightRow, static_cast<size_t>(rowBytes)) == 0) {
            continue;
        }
        const int tileRow = y / tileSize;
        for (int column = 0; column < columns; ++column) {
            const qsizetype offset = column * tileBytes;
            const qsizetype bytes = std::min(tileBytes, rowBytes - offset);
            if (std::memcmp(leftRow + offset, rightRow + offset, static_cast<size_t>(bytes)) != 0) {
                changed[tileRow * columns + column] = true;
            }
        }
    }
    return static_cast<int>(std::count(changed.cbegin(), changed.cend(), true));
}

const char* settleOutcomeName(FrameSettleDetector::Outcome outcome)
{
    struct Entry {
        FrameSettleDetector::Outcome outcome;
        const char* name;
    };
    static constexpr Entry kNames[] = {
        {FrameSettleDetector::Outcome::Pending, "pending"},
        {FrameSettleDetector::Outcome::Settled, "settled"},
        {FrameSettleDetector::Outcome::TimedOut, "timed_out"},
    };
    for (const Entry& entry : kNames) {
        if (entry.outcome == outcome) {
            return entry.name;
        }
    }
    return "unknown";
}

} // namespace snaptray::scrollcapture
