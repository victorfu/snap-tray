#include "scrollcapture/RowHashShiftEstimator.h"
#include "scrollcapture/FrameRowHashes.h"

#include <QHash>
#include <QSet>

#include <algorithm>

namespace snaptray::scrollcapture {

namespace {

constexpr int kNoIndex = -1;

ShiftEstimate makeResult(ShiftEstimate::Status status, int staticRows)
{
    ShiftEstimate result;
    result.status = status;
    result.staticRows = staticRows;
    return result;
}

} // namespace

ShiftEstimate estimateVerticalShift(const QVector<quint64>& before,
                                    const QVector<quint64>& after,
                                    const ShiftEstimatorOptions& options)
{
    const int height = static_cast<int>(before.size());
    if (height == 0 || after.size() != before.size()) {
        return makeResult(ShiftEstimate::Status::InvalidInput, 0);
    }

    QVector<bool> isStatic(height, false);
    int staticRows = 0;
    for (int i = 0; i < height; ++i) {
        isStatic[i] = before[i] == after[i];
        staticRows += isStatic[i] ? 1 : 0;
    }

    if (staticRows == height) {
        ShiftEstimate result = makeResult(ShiftEstimate::Status::Found, staticRows);
        result.matchedRows = height;
        result.overlapRows = height;
        return result;
    }

    const int maxShift = height - std::max(1, options.minOverlapRows);
    int low = -maxShift;
    int high = maxShift;
    if (options.expectedShift.has_value()) {
        const int radius = std::max(0, options.searchRadius);
        low = std::max(low, *options.expectedShift - radius);
        high = std::min(high, *options.expectedShift + radius);
    }
    if (maxShift < 1 || low > high) {
        ShiftEstimate result = makeResult(ShiftEstimate::Status::NotFound, staticRows);
        result.searchLow = low;
        result.searchHigh = high;
        return result;
    }

    // Index of each moving row whose content occurs exactly once in `before`.
    QHash<quint64, int> uniqueIndex;
    uniqueIndex.reserve(height);
    for (int j = 0; j < height; ++j) {
        auto it = uniqueIndex.find(before[j]);
        if (it == uniqueIndex.end()) {
            uniqueIndex.insert(before[j], isStatic[j] ? kNoIndex : j);
        } else {
            it.value() = kNoIndex;
        }
    }
    int movingUniqueRows = 0;
    for (auto it = uniqueIndex.cbegin(); it != uniqueIndex.cend(); ++it) {
        movingUniqueRows += it.value() != kNoIndex ? 1 : 0;
    }

    // Each moving row of `after` votes for the shift that brings its unique
    // twin in `before` to its position. Shift 0 never gets a vote: a row equal
    // at the same position is static.
    QVector<int> votes(high - low + 1, 0);
    for (int i = 0; i < height; ++i) {
        if (isStatic[i]) {
            continue;
        }
        const auto it = uniqueIndex.constFind(after[i]);
        if (it == uniqueIndex.cend() || it.value() == kNoIndex) {
            continue;
        }
        const int shift = it.value() - i;
        if (shift >= low && shift <= high) {
            ++votes[shift - low];
        }
    }

    const auto bestIt = std::max_element(votes.cbegin(), votes.cend());
    const int bestVotes = *bestIt;
    const int bestShift = low + static_cast<int>(bestIt - votes.cbegin());
    const auto annotate = [&](ShiftEstimate& result) {
        result.shift = bestShift;
        result.distinctMatches = bestVotes;
        result.searchLow = low;
        result.searchHigh = high;
        for (int index = 0; index < votes.size(); ++index) {
            if (low + index != bestShift && votes[index] > 0 && votes[index] > result.runnerUpVotes) {
                result.runnerUpShift = low + index;
                result.runnerUpVotes = votes[index];
            }
        }
    };
    if (bestVotes < std::max(1, options.minDistinctMatches)) {
        const bool lacksDetail = movingUniqueRows < std::max(1, options.minDistinctMatches) || bestVotes > 0;
        ShiftEstimate result = makeResult(lacksDetail ? ShiftEstimate::Status::InsufficientDetail
                                                      : ShiftEstimate::Status::NotFound,
                                          staticRows);
        annotate(result);
        return result;
    }

    ShiftEstimate result = makeResult(ShiftEstimate::Status::Found, staticRows);
    annotate(result);
    for (int i = 0; i < height; ++i) {
        const int source = i + bestShift;
        if (isStatic[i] || source < 0 || source >= height) {
            continue;
        }
        ++result.overlapRows;
        result.matchedRows += after[i] == before[source] ? 1 : 0;
    }
    if (result.overlapRows == 0 ||
        result.matchedRows < options.minMatchRatio * result.overlapRows) {
        result.status = ShiftEstimate::Status::NotFound;
        return result;
    }

    const double ambiguityThreshold = options.ambiguityRatio * bestVotes;
    for (int index = 0; index < votes.size(); ++index) {
        if (votes[index] > 0 && votes[index] >= ambiguityThreshold) {
            result.candidates.append(low + index);
        }
    }
    std::stable_sort(result.candidates.begin(), result.candidates.end(),
                     [&votes, low](int a, int b) { return votes[a - low] > votes[b - low]; });
    if (result.candidates.size() > 1) {
        result.status = ShiftEstimate::Status::Ambiguous;
    } else {
        result.candidates.clear();
    }
    return result;
}

BandShiftEstimate estimateVerticalShiftByBands(const QImage& before, const QImage& after,
                                              const ShiftEstimatorOptions& options,
                                              int horizontalInset)
{
    constexpr int kBandCount = 8;
    BandShiftEstimate result;
    if (before.isNull() || before.size() != after.size() || horizontalInset < 0
        || horizontalInset > (before.width() - 1) / 2
        || before.width() - 2 * horizontalInset < kBandCount) {
        return result;
    }
    const int width = before.width() - 2 * horizontalInset;
    QHash<int, int> support;
    QVector<bool> blankStaticBands;
    for (int band = 0; band < kBandCount; ++band) {
        const int left = horizontalInset + width * band / kBandCount;
        const int right = horizontalInset + width * (band + 1) / kBandCount;
        const QRect rect(left, 0, right - left, before.height());
        const auto beforeRows = computeRowHashes(before.copy(rect));
        const auto afterRows = computeRowHashes(after.copy(rect));
        const auto estimate = estimateVerticalShift(beforeRows, afterRows, options);
        auto unconstrained = options;
        unconstrained.expectedShift.reset();
        result.observedBands.append(estimateVerticalShift(beforeRows, afterRows, unconstrained));
        const QSet<quint64> distinct(beforeRows.cbegin(), beforeRows.cend());
        blankStaticBands.append(beforeRows == afterRows
            && distinct.size() < std::max(1, options.minDistinctMatches));
        result.bands.append(estimate);
        if (estimate.status == ShiftEstimate::Status::Found) {
            ++support[estimate.shift];
        }
    }
    result.estimate.status = ShiftEstimate::Status::NotFound;
    for (const auto& band : result.bands) {
        if (band.status == ShiftEstimate::Status::Found && support[band.shift] > kBandCount / 2
            && (band.shift != 0 || before == after)) {
            result.estimate = band;
            result.supportingBands = support[band.shift];
            break;
        }
    }
    // Without a majority, describe why. A blank static band reports Found/0
    // but shows no motion, so it is not a competing displacement.
    QSet<int> texturedShifts;
    bool anyNotFound = false;
    bool anyInsufficientDetail = false;
    for (int band = 0; band < result.bands.size(); ++band) {
        const auto status = result.bands[band].status;
        if (status == ShiftEstimate::Status::Found && !blankStaticBands[band]) {
            texturedShifts.insert(result.bands[band].shift);
        }
        anyNotFound = anyNotFound || status == ShiftEstimate::Status::NotFound;
        anyInsufficientDetail = anyInsufficientDetail || status == ShiftEstimate::Status::InsufficientDetail;
    }
    if (result.estimate.status != ShiftEstimate::Status::Found) {
        if (texturedShifts.size() > 1) {
            result.estimate.status = ShiftEstimate::Status::Ambiguous;
            result.estimate.candidates = texturedShifts.values().toVector();
            std::sort(result.estimate.candidates.begin(), result.estimate.candidates.end());
        } else if (texturedShifts.isEmpty() && anyInsufficientDetail && !anyNotFound) {
            // Nothing measurable anywhere: blank or repeating content, not a mismatch.
            result.estimate.status = ShiftEstimate::Status::InsufficientDetail;
        }
    }
    result.alignmentEstimate = result.estimate;
    if (result.estimate.status == ShiftEstimate::Status::Found) {
        QSet<int> shifts{result.estimate.shift};
        for (int band = 0; band < result.observedBands.size(); ++band) {
            if (blankStaticBands[band]) {
                continue;
            }
            const auto& observed = result.observedBands[band];
            if (observed.status != ShiftEstimate::Status::Found) {
                result.unresolvedBands.append(band);
            } else if (observed.shift != result.estimate.shift) {
                result.conflictingBands.append(band);
                shifts.insert(observed.shift);
            }
        }
        result.nonUniformMotion = !result.conflictingBands.isEmpty();
        if (result.nonUniformMotion) {
            result.alignmentEstimate.status = ShiftEstimate::Status::NonUniformMotion;
            result.alignmentEstimate.candidates = shifts.values().toVector();
            std::sort(result.alignmentEstimate.candidates.begin(), result.alignmentEstimate.candidates.end());
        } else if (!result.unresolvedBands.isEmpty()) {
            result.alignmentEstimate.status = ShiftEstimate::Status::NotFound;
        }
    } else {
        result.nonUniformMotion = texturedShifts.size() > 1;
    }
    return result;
}

const char* shiftEstimateStatusName(ShiftEstimate::Status status)
{
    struct Entry {
        ShiftEstimate::Status status;
        const char* name;
    };
    static constexpr Entry kNames[] = {
        {ShiftEstimate::Status::Found, "found"},
        {ShiftEstimate::Status::Ambiguous, "ambiguous"},
        {ShiftEstimate::Status::NonUniformMotion, "non_uniform_motion"},
        {ShiftEstimate::Status::InsufficientDetail, "insufficient_detail"},
        {ShiftEstimate::Status::NotFound, "not_found"},
        {ShiftEstimate::Status::InvalidInput, "invalid_input"},
    };
    for (const Entry& entry : kNames) {
        if (entry.status == status) {
            return entry.name;
        }
    }
    return "unknown";
}

} // namespace snaptray::scrollcapture
