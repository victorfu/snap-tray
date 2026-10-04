#include "SyntheticScroll.h"

#include "IVideoEncoder.h"
#include "encoding/IntermediateQuality.h"
#include "encoding/VideoRateControl.h"

#include <QElapsedTimer>
#include <QPainter>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <memory>
#include <cstring>
#include <map>

namespace SyntheticScroll {

namespace {

// Page content: "text lines" of random dark rectangles with a line pitch,
// headings, horizontal rules and coloured blocks. Dense enough that every
// 64-row band has ink unless a blank band is requested.
constexpr int kLinePitch = 22;
constexpr int kLineHeight = 12;
constexpr int kMargin = 24;
constexpr int kMinWordWidth = 18;
constexpr int kMaxWordWidth = 90;
constexpr int kWordGap = 8;
constexpr int kHeadingEvery = 9;        // every ninth line is a heading
constexpr int kRuleEvery = 23;          // horizontal rule every 23 lines
constexpr int kBlockEvery = 31;         // coloured block every 31 lines
constexpr int kBlockHeight = 60;
constexpr int kLazyBlockHeight = 200;
constexpr int kHoverBlockHeight = 40;
constexpr int kHoverBlockWidth = 160;
constexpr int kHoverEveryFrames = 3;

constexpr int kFrameAcceptTimeoutMs = 2000;
constexpr int kFinishTimeoutMs = 10000;
constexpr int kBackpressurePollMs = 5;

// Row profile for matching: luma summed into 64 column bins.
constexpr int kProfileBins = 64;
constexpr double kProfileTolerance = 6.0;   // mean abs bin difference, 0..255
constexpr double kStartBias = 1e-6;         // first row: prefer candidates near firstPageRow

const QColor kInk(30, 30, 30);
const QColor kHeading(10, 40, 120);
const QColor kRule(180, 180, 180);
const QColor kBlockColours[] = {QColor(230, 120, 60), QColor(60, 160, 90), QColor(90, 110, 220)};
const QColor kHeader(245, 245, 250);
const QColor kHeaderInk(60, 60, 80);
const QColor kSidebar(235, 238, 242);
const QColor kLazyPlaceholder(200, 200, 200);
const QColor kHoverA(250, 220, 100);
const QColor kHoverB(100, 200, 250);

std::vector<float> rowProfile(const QImage& image, int y)
{
    std::vector<float> bins(kProfileBins, 0.0f);
    std::vector<int> counts(kProfileBins, 0);
    const uchar* line = image.constScanLine(y);
    for (int x = 0; x < image.width(); ++x) {
        const QRgb px = reinterpret_cast<const QRgb*>(line)[x];
        const int bin = int(qint64(x) * kProfileBins / image.width());
        bins[bin] += float(qGray(px));
        ++counts[bin];
    }
    for (int b = 0; b < kProfileBins; ++b) {
        if (counts[b] > 0) bins[b] /= float(counts[b]);
    }
    return bins;
}

double profileDistance(const std::vector<float>& a, const std::vector<float>& b)
{
    double sum = 0.0;
    for (int i = 0; i < kProfileBins; ++i) sum += std::abs(a[i] - b[i]);
    return sum / kProfileBins;
}

// Most page rows are clearly different. Stop once the absolute tolerance
// cannot be met; this preserves the exact candidate set with codec references.
bool profilesMatch(const std::vector<float>& a, const std::vector<float>& b)
{
    double sum = 0.0;
    const double limit = kProfileTolerance * kProfileBins;
    for (int i = 0; i < kProfileBins; ++i) {
        sum += std::abs(a[i] - b[i]);
        if (sum >= limit) return false;
    }
    return true;
}

} // namespace

double rowProfileDistance(const QImage& a, int ya, const QImage& b, int yb)
{
    return profileDistance(rowProfile(a, ya), rowProfile(b, yb));
}

QImage renderPage(const PageSpec& spec)
{
    QImage page(spec.width, spec.height, QImage::Format_RGB32);
    page.fill(Qt::white);
    QPainter painter(&page);
    QRandomGenerator random(spec.seed);
    int line = 0;
    for (int y = kMargin; y + kLineHeight <= spec.height - kMargin; y += kLinePitch, ++line) {
        const bool isBlock = line % kBlockEvery == kBlockEvery - 1;
        const bool isRule = !isBlock && line % kRuleEvery == kRuleEvery - 1;
        const bool heading = !isBlock && !isRule && line % kHeadingEvery == 0;
        // Real vertical extent of what this line would paint.
        const int top = isRule ? y + kLineHeight / 2 : y;
        const int extent = isBlock ? kBlockHeight : isRule ? 2 : (heading ? kLineHeight + 6 : kLineHeight);
        if (spec.blankHeight > 0 && top + extent > spec.blankTop && top < spec.blankTop + spec.blankHeight) {
            continue;
        }
        if (isBlock) {
            painter.fillRect(QRect(kMargin, y, spec.width - 2 * kMargin, kBlockHeight),
                             kBlockColours[line % 3]);
            y += kBlockHeight - kLinePitch; // the loop adds one pitch
            continue;
        }
        if (isRule) {
            painter.fillRect(QRect(kMargin, y + kLineHeight / 2, spec.width - 2 * kMargin, 2), kRule);
            continue;
        }
        const int height = heading ? kLineHeight + 6 : kLineHeight;
        const QColor colour = heading ? kHeading : kInk;
        int x = kMargin + int(random.bounded(40));
        const int lineEnd = spec.width - kMargin - int(random.bounded(120));
        while (x < lineEnd) {
            const int width = kMinWordWidth + int(random.bounded(kMaxWordWidth - kMinWordWidth));
            painter.fillRect(QRect(x, y, std::min(width, lineEnd - x), height), colour);
            x += width + kWordGap;
        }
    }
    // A fixed page element at row 900 in its resting colour; frames repaint it in
    // the hover colour when Disturbances::hoverChange is set.
    if (spec.height >= 900 + kHoverBlockHeight) {
        painter.fillRect(QRect(kMargin, 900, kHoverBlockWidth, kHoverBlockHeight), kHoverB);
    }
    painter.end();
    return page;
}

Trajectory constantSpeed(int frameCount, int startOffset, int pixelsPerFrame)
{
    Trajectory t;
    for (int i = 0; i < frameCount; ++i) t.offsets.push_back(startOffset + i * pixelsPerFrame);
    return t;
}

Trajectory fling(int frameCount, int startOffset, int peakPixelsPerFrame)
{
    Trajectory t;
    double offset = startOffset;
    double speed = peakPixelsPerFrame;
    const double decay = 0.82;
    for (int i = 0; i < frameCount; ++i) {
        t.offsets.push_back(int(std::lround(offset)));
        offset += speed;
        speed = speed * decay;
        if (speed < 0.5) speed = 0.0;
    }
    return t;
}

Trajectory backAndForth(int frameCount, int startOffset, int amplitude, int pixelsPerFrame)
{
    Trajectory t;
    int offset = startOffset;
    int direction = 1;
    for (int i = 0; i < frameCount; ++i) {
        t.offsets.push_back(offset);
        offset += direction * pixelsPerFrame;
        if (offset >= startOffset + amplitude) { offset = startOffset + amplitude; direction = -1; }
        if (offset <= startOffset - amplitude) { offset = startOffset - amplitude; direction = 1; }
    }
    return t;
}

Trajectory withPauses(const Trajectory& base, int pauseEveryFrames, int pauseFrames)
{
    Trajectory t;
    for (size_t i = 0; i < base.offsets.size(); ++i) {
        t.offsets.push_back(base.offsets[i]);
        if ((int(i) + 1) % pauseEveryFrames == 0) {
            for (int p = 0; p < pauseFrames; ++p) t.offsets.push_back(base.offsets[i]);
        }
    }
    return t;
}

Trajectory clampTrajectory(const Trajectory& trajectory, int pageHeight, int viewportHeight)
{
    Trajectory t = trajectory;
    const int maxOffset = std::max(0, pageHeight - viewportHeight);
    for (int& o : t.offsets) o = std::clamp(o, 0, maxOffset);
    return t;
}

QImage renderFrame(const QImage& page, const QSize& viewport, const Trajectory& trajectory, int frameIndex,
                   const Disturbances& d)
{
    const int offset = trajectory.offsets.at(size_t(frameIndex));
    QImage frame(viewport, QImage::Format_RGB32);
    frame.fill(Qt::white);
    QPainter painter(&frame);
    // Lazy-load placeholder is part of the page until lazyLoadFrame.
    QImage source = page;
    if (d.lazyLoadRow >= 0 && frameIndex < d.lazyLoadFrame) {
        QPainter cover(&source);
        cover.fillRect(QRect(0, d.lazyLoadRow, page.width(), kLazyBlockHeight), kLazyPlaceholder);
    }
    painter.drawImage(0, 0, source, 0, offset, viewport.width(), viewport.height());
    if (d.hoverChange) {
        // The page's hover element (row 900) shows its hover colour every third frame.
        const int pageRow = 900;
        const int y = pageRow - offset;
        if (frameIndex % kHoverEveryFrames == 0 && y + kHoverBlockHeight > 0 && y < viewport.height()) {
            painter.fillRect(QRect(kMargin, y, kHoverBlockWidth, kHoverBlockHeight),
                             kHoverA);
        }
    }
    if (d.sidebarWidth > 0) {
        painter.fillRect(QRect(viewport.width() - d.sidebarWidth, 0, d.sidebarWidth, viewport.height()), kSidebar);
        for (int y = kMargin; y < viewport.height(); y += 2 * kLinePitch) {
            painter.fillRect(QRect(viewport.width() - d.sidebarWidth + 12, y, d.sidebarWidth - 24, kLineHeight), kHeaderInk);
        }
    }
    if (d.stickyHeaderHeight > 0) {
        painter.fillRect(QRect(0, 0, viewport.width(), d.stickyHeaderHeight), kHeader);
        painter.fillRect(QRect(kMargin, d.stickyHeaderHeight / 3, 200, d.stickyHeaderHeight / 3), kHeaderInk);
    }
    if (d.scrollUpHeaderHeight > 0 && frameIndex > 0
        && trajectory.offsets.at(size_t(frameIndex)) < trajectory.offsets.at(size_t(frameIndex - 1))) {
        painter.fillRect(QRect(0, 0, viewport.width(), d.scrollUpHeaderHeight), kHeader);
        painter.fillRect(QRect(kMargin, d.scrollUpHeaderHeight / 3, 260, d.scrollUpHeaderHeight / 3), kHeading);
    }
    painter.end();
    return frame;
}

QString encodeFrames(const QString& path, const std::vector<QImage>& frames, int frameRate)
{
    if (frames.empty()) return QStringLiteral("no frames");
    std::unique_ptr<IVideoEncoder> encoder(IVideoEncoder::createNativeEncoder());
    if (!encoder) return QStringLiteral("No native encoder");
    using namespace SnapTray;
    encoder->setQuality(IntermediateQuality::kConstantQualityValue);
    encoder->setRateControl(VideoRateControl::ConstantQuality, IntermediateQuality::kConstantQualityValue);
    encoder->setKeyFrameIntervalSeconds(IntermediateQuality::kKeyFrameIntervalSeconds);
    if (!encoder->start(path, frames.front().size(), frameRate)) return encoder->lastError();
    for (size_t i = 0; i < frames.size(); ++i) {
        const qint64 before = encoder->framesWritten();
        QElapsedTimer timer;
        timer.start();
        do {
            encoder->writeFrame(frames[i], qint64(i) * 1000 / frameRate);
            if (encoder->framesWritten() != before) break;
            QTest::qWait(kBackpressurePollMs);
        } while (timer.elapsed() < kFrameAcceptTimeoutMs);
        if (encoder->framesWritten() != before + 1) return QStringLiteral("frame %1 rejected").arg(i);
    }
    QSignalSpy finished(encoder.get(), &IVideoEncoder::finished);
    encoder->finish();
    if (finished.isEmpty() && !finished.wait(kFinishTimeoutMs)) return QStringLiteral("encoder did not finish");
    return finished.first().at(0).toBool() ? QString() : encoder->lastError();
}

void addRowMatchReferences(RowMatchReferences& references, const QImage& decoded, const QImage& original,
                           const QImage& page, int pageTop)
{
    Q_ASSERT(decoded.size() == original.size() && original.width() == page.width());
    const QImage input = original.convertToFormat(QImage::Format_RGB32);
    const QImage truth = page.convertToFormat(QImage::Format_RGB32);
    const QImage output = decoded.convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < input.height(); ++y) {
        const int pageRow = pageTop + y;
        if (pageRow < 0 || pageRow >= truth.height()) continue;
        if (std::memcmp(input.constScanLine(y), truth.constScanLine(pageRow), size_t(input.width()) * sizeof(QRgb)) == 0) {
            references.push_back({pageRow, rowProfile(output, y)});
        }
    }
}

RowMatchReport compareWithGroundTruth(const QImage& result, const QImage& page, int firstPageRow, int lastPageRow,
                                     const RowMatchReferences& references)
{
    RowMatchReport report;
    report.outputRows = result.height();
    if (result.isNull() || page.isNull()) return report;
    const QImage out = result.convertToFormat(QImage::Format_RGB32);
    const QImage truth = page.convertToFormat(QImage::Format_RGB32);
    std::vector<std::vector<std::vector<float>>> truthProfiles(truth.height());
    for (int y = 0; y < truth.height(); ++y) truthProfiles[y].push_back(rowProfile(truth, y));
    for (const RowMatchReference& reference : references) {
        if (reference.pageRow < 0 || reference.pageRow >= truth.height()) continue;
        auto& profiles = truthProfiles[reference.pageRow];
        if (std::find(profiles.begin(), profiles.end(), reference.profile) == profiles.end()) {
            profiles.push_back(reference.profile);
        }
    }
    // Text lines and flat bands repeat profiles. Memoize their candidate sets
    // so calibration does not multiply the cost for each identical output row.
    std::map<std::vector<float>, std::vector<int>> candidateCache;

    const int outRows = out.height();
    std::vector<int> mapping(outRows, -1);

    // Rows inside a uniform band (and every row of one text line) have identical
    // profiles, so a per-row best match is ambiguous. Pick, per output row, the
    // set of near-best page rows and choose the path through those sets with the
    // fewest discontinuities (a step other than +1 costs one jump); the first row
    // prefers the page row nearest firstPageRow.
    struct State {
        int pageRow;
        double cost;
        int back; // index into the previous matched row's state list, -1 at the start
    };
    std::vector<std::vector<State>> layers;
    std::vector<int> layerRow;   // output row of each layer
    int skipped = 0;             // unmatched output rows since the last layer
    for (int y = 0; y < outRows; ++y) {
        const auto profile = rowProfile(out, y);
        auto [cached, inserted] = candidateCache.try_emplace(profile);
        if (inserted) {
            for (int p = 0; p < truth.height(); ++p) {
                for (const auto& reference : truthProfiles[p]) {
                    if (profilesMatch(profile, reference)) {
                        cached->second.push_back(p);
                        break;
                    }
                }
            }
        }
        const auto& candidates = cached->second;
        if (candidates.empty()) {
            ++skipped;
            continue;
        }
        // Keep the same absolute tolerance and continuity rule. Codec-domain
        // references prevent rejecting the correct row merely because RGB
        // reconstruction differs between native encoders/decoders.
        std::vector<State> layer;
        if (layers.empty()) {
            for (int p : candidates) {
                layer.push_back({p, kStartBias * std::abs(p - firstPageRow), -1});
            }
        } else {
            const std::vector<State>& prev = layers.back();
            int globalBest = 0;
            for (size_t i = 1; i < prev.size(); ++i) {
                if (prev[i].cost < prev[size_t(globalBest)].cost) globalBest = int(i);
            }
            std::vector<int> indexOfRow(truth.height(), -1);
            for (size_t i = 0; i < prev.size(); ++i) indexOfRow[prev[i].pageRow] = int(i);
            for (int p : candidates) {
                State st{p, prev[size_t(globalBest)].cost + 1.0, globalBest};
                const int from = p - 1 - skipped;
                if (from >= 0 && indexOfRow[from] >= 0) {
                    const State& cont = prev[size_t(indexOfRow[from])];
                    if (cont.cost <= st.cost) st = {p, cont.cost, indexOfRow[from]};
                }
                layer.push_back(st);
            }
        }
        layers.push_back(std::move(layer));
        layerRow.push_back(y);
        skipped = 0;
    }
    if (!layers.empty()) {
        int index = 0;
        const std::vector<State>& last = layers.back();
        for (size_t i = 1; i < last.size(); ++i) {
            if (last[i].cost < last[size_t(index)].cost) index = int(i);
        }
        for (int l = int(layers.size()) - 1; l >= 0; --l) {
            const State& st = layers[size_t(l)][size_t(index)];
            mapping[layerRow[size_t(l)]] = st.pageRow;
            index = st.back;
        }
    }

    // Score the final mapping.
    int previous = -1;
    int highest = -1; // highest page row matched so far
    for (int y = 0; y < outRows; ++y) {
        const int best = mapping[y];
        if (best < 0) {
            ++report.unmatchedRows;
            continue;
        }
        ++report.matchedRows;
        if (best <= highest) ++report.duplicatedRows;
        else if (previous >= 0 && std::abs(best - (previous + 1)) > 1) ++report.misalignedRows;
        previous = best;
        highest = std::max(highest, best);
    }
    std::vector<bool> covered(truth.height(), false);
    for (int p : mapping) {
        if (p >= 0) covered[p] = true;
    }
    for (int p = std::max(0, firstPageRow); p <= std::min(truth.height() - 1, lastPageRow); ++p) {
        if (!covered[p]) ++report.missingRows;
    }
    return report;
}

} // namespace SyntheticScroll
