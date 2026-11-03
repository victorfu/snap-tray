#include "recording/TimelineEdit.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>

namespace SnapTray {

namespace {

bool isAllowedSpeed(double speed)
{
    return std::any_of(std::begin(TimelineEdit::kSpeeds), std::end(TimelineEdit::kSpeeds),
                       [speed](double allowed) { return qFuzzyCompare(allowed, speed); });
}

} // namespace

TimelineMap::TimelineMap(std::vector<TimelineSegment> segments)
{
    for (auto& segment : segments) {
        if (segment.endMs > segment.startMs && segment.speed > 0.0) m_segments.push_back(segment);
    }
    m_outputStarts.reserve(m_segments.size() + 1);
    qint64 at = 0;
    m_outputStarts.push_back(at);
    for (const auto& segment : m_segments) {
        at += segment.outputLengthMs();
        m_outputStarts.push_back(at);
    }
}

bool TimelineMap::isPlainTrim() const
{
    return m_segments.size() == 1 && qFuzzyCompare(m_segments.front().speed, 1.0);
}

int TimelineMap::segmentAt(qint64 sourceMs) const
{
    auto it = std::upper_bound(m_segments.begin(), m_segments.end(), sourceMs,
                               [](qint64 t, const TimelineSegment& s) { return t < s.startMs; });
    if (it == m_segments.begin()) return -1;
    --it;
    return it->contains(sourceMs) ? int(it - m_segments.begin()) : -1;
}

qint64 TimelineMap::segmentOutputStartMs(int index) const
{
    return index >= 0 && index < int(m_outputStarts.size()) ? m_outputStarts[size_t(index)] : 0;
}

std::optional<qint64> TimelineMap::sourceToOutput(qint64 sourceMs) const
{
    const int index = segmentAt(sourceMs);
    if (index < 0) return std::nullopt;
    const TimelineSegment& s = m_segments[size_t(index)];
    return m_outputStarts[size_t(index)] + qint64(std::llround(double(sourceMs - s.startMs) / s.speed));
}

qint64 TimelineMap::outputToSource(qint64 outputMs) const
{
    if (m_segments.empty()) return 0;
    if (outputMs <= 0) return m_segments.front().startMs;
    auto it = std::upper_bound(m_outputStarts.begin(), m_outputStarts.end(), outputMs);
    const size_t index = size_t(std::max<ptrdiff_t>(0, (it - m_outputStarts.begin()) - 1));
    if (index >= m_segments.size()) return m_segments.back().endMs;
    const TimelineSegment& s = m_segments[index];
    const qint64 source = s.startMs + qint64(std::llround(double(outputMs - m_outputStarts[index]) * s.speed));
    // A time inside this part never maps to its end: the end is exclusive,
    // the first moment of what was cut (rounding at 0.5x would reach it,
    // PR #199 review). Past the whole output, the end is the answer.
    const bool inside = index + 1 < m_outputStarts.size() && outputMs < m_outputStarts[index + 1];
    return std::min(source, inside ? std::max(s.startMs, s.endMs - 1) : s.endMs);
}

qint64 TimelineMap::nextShownSourceMs(qint64 sourceMs) const
{
    if (segmentAt(sourceMs) >= 0) return sourceMs;
    for (const auto& segment : m_segments) {
        if (segment.startMs > sourceMs) return segment.startMs;
    }
    return -1;
}

double TimelineMap::speedAt(qint64 sourceMs) const
{
    const int index = segmentAt(sourceMs);
    return index >= 0 ? m_segments[size_t(index)].speed : 1.0;
}

bool TimelineEdit::isEmpty() const
{
    return splits.empty() && parts.size() == 1 && parts.front() == Part();
}

int TimelineEdit::partAt(qint64 sourceMs) const
{
    return int(std::upper_bound(splits.begin(), splits.end(), sourceMs) - splits.begin());
}

qint64 TimelineEdit::partStartMs(int index) const
{
    return index <= 0 ? 0 : splits[size_t(index - 1)];
}

qint64 TimelineEdit::partEndMs(int index, qint64 durationMs) const
{
    return index >= int(splits.size()) ? durationMs : splits[size_t(index)];
}

bool TimelineEdit::splitAt(qint64 sourceMs, qint64 durationMs)
{
    const int part = partAt(sourceMs);
    if (sourceMs - partStartMs(part) < kMinPartMs || partEndMs(part, durationMs) - sourceMs < kMinPartMs) return false;
    splits.insert(splits.begin() + part, sourceMs);
    parts.insert(parts.begin() + part, parts[size_t(part)]); // both halves keep the style
    return true;
}

bool TimelineEdit::removeSplit(int index)
{
    if (index < 0 || index >= int(splits.size())) return false;
    splits.erase(splits.begin() + index);
    parts.erase(parts.begin() + index + 1);
    return true;
}

bool TimelineEdit::moveSplit(int index, qint64 sourceMs, qint64 durationMs)
{
    if (index < 0 || index >= int(splits.size())) return false;
    const qint64 lower = partStartMs(index) + kMinPartMs;
    const qint64 upper = partEndMs(index + 1, durationMs) - kMinPartMs;
    if (sourceMs < lower || sourceMs > upper) return false;
    splits[size_t(index)] = sourceMs;
    return true;
}

bool TimelineEdit::setDeleted(int part, bool deleted)
{
    if (part < 0 || part >= int(parts.size()) || parts[size_t(part)].deleted == deleted) return false;
    parts[size_t(part)].deleted = deleted;
    return true;
}

bool TimelineEdit::setSpeed(int part, double speed)
{
    if (part < 0 || part >= int(parts.size()) || !isAllowedSpeed(speed)) return false;
    if (qFuzzyCompare(parts[size_t(part)].speed, speed)) return false;
    parts[size_t(part)].speed = speed;
    return true;
}

TimelineMap TimelineEdit::map(qint64 trimStartMs, qint64 trimEndMs) const
{
    std::vector<TimelineSegment> kept;
    for (int i = 0; i < int(parts.size()); ++i) {
        if (parts[size_t(i)].deleted) continue;
        const qint64 start = std::max(trimStartMs, partStartMs(i));
        const qint64 end = std::min(trimEndMs, partEndMs(i, trimEndMs));
        if (end <= start) continue;
        // Neighbours at the same speed are one part (a split nobody used).
        if (!kept.empty() && kept.back().endMs == start && qFuzzyCompare(kept.back().speed, parts[size_t(i)].speed)) {
            kept.back().endMs = end;
        } else {
            kept.push_back({start, end, parts[size_t(i)].speed});
        }
    }
    return TimelineMap(std::move(kept));
}

QJsonObject TimelineEdit::toJson() const
{
    QJsonArray splitArray;
    for (const qint64 split : splits) splitArray.append(double(split));
    QJsonArray partArray;
    for (const auto& part : parts) {
        partArray.append(QJsonObject{{QStringLiteral("deleted"), part.deleted}, {QStringLiteral("speed"), part.speed}});
    }
    return {{QStringLiteral("splits"), splitArray}, {QStringLiteral("parts"), partArray}};
}

TimelineEdit TimelineEdit::fromJson(const QJsonObject& json)
{
    TimelineEdit edit;
    std::vector<qint64> splits;
    for (const auto& value : json.value(QStringLiteral("splits")).toArray()) splits.push_back(qint64(value.toDouble(-1)));
    std::vector<Part> parts;
    for (const auto& value : json.value(QStringLiteral("parts")).toArray()) {
        const QJsonObject object = value.toObject();
        Part part;
        part.deleted = object.value(QStringLiteral("deleted")).toBool(false);
        part.speed = object.value(QStringLiteral("speed")).toDouble(1.0);
        if (!isAllowedSpeed(part.speed)) return edit;
        parts.push_back(part);
    }
    const bool sorted = std::adjacent_find(splits.begin(), splits.end(), std::greater_equal<qint64>()) == splits.end();
    if (parts.size() != splits.size() + 1 || !sorted || (!splits.empty() && splits.front() <= 0)) return edit;
    edit.splits = std::move(splits);
    edit.parts = std::move(parts);
    return edit;
}

} // namespace SnapTray
