#include "video/Mp4VideoSamples.h"

#include <QFile>
#include <QtEndian>

#include <algorithm>
#include <numeric>

namespace SnapTray {

namespace {

constexpr qint64 kUsPerSecond = 1000000;
constexpr qint64 kUsPerMs = 1000;
constexpr qint64 kBoxHeader = 8;
constexpr qint64 kLargeBoxHeader = 16;
// moov and moof are read whole; mdat is skipped. A moov this big is not a
// recording's (an hour of 60 fps is about 1 MB of sample sizes).
constexpr qint64 kMaxMetadataBox = 64 * 1024 * 1024;
// More samples than a day at 120 fps: a damaged count, not a recording.
constexpr qint64 kMaxSamples = 24LL * 3600 * 120;

constexpr quint32 fourcc(const char (&s)[5])
{
    return (quint32(quint8(s[0])) << 24) | (quint32(quint8(s[1])) << 16) | (quint32(quint8(s[2])) << 8)
           | quint32(quint8(s[3]));
}

// Track-fragment header and run flags (ISO/IEC 14496-12, 8.8.7 and 8.8.8).
constexpr quint32 kTfhdBaseDataOffset = 0x000001;
constexpr quint32 kTfhdSampleDescriptionIndex = 0x000002;
constexpr quint32 kTfhdDefaultDuration = 0x000008;
constexpr quint32 kTfhdDefaultSize = 0x000010;
constexpr quint32 kTfhdDefaultFlags = 0x000020;
constexpr quint32 kTrunDataOffset = 0x000001;
constexpr quint32 kTrunFirstSampleFlags = 0x000004;
constexpr quint32 kTrunDuration = 0x000100;
constexpr quint32 kTrunSize = 0x000200;
constexpr quint32 kTrunFlags = 0x000400;
constexpr quint32 kTrunCompositionOffset = 0x000800;
// sample_is_non_sync_sample in the sample flags (8.8.3.1).
constexpr quint32 kSampleNonSync = 0x00010000;
// An edit with this media time is empty: the track shows nothing for it.
constexpr qint64 kEmptyEdit = -1;

// Bounds-checked big-endian reads over one box's payload.
class Reader
{
public:
    Reader(const char* data, qint64 size) : m_data(data), m_size(size) {}
    bool ok() const { return m_ok; }
    qint64 remaining() const { return m_size - m_pos; }
    bool skip(qint64 n)
    {
        if (n < 0 || n > remaining()) return m_ok = false;
        m_pos += n;
        return true;
    }
    quint32 u32()
    {
        if (remaining() < 4) { m_ok = false; return 0; }
        const quint32 v = qFromBigEndian<quint32>(m_data + m_pos);
        m_pos += 4;
        return v;
    }
    quint64 u64()
    {
        if (remaining() < 8) { m_ok = false; return 0; }
        const quint64 v = qFromBigEndian<quint64>(m_data + m_pos);
        m_pos += 8;
        return v;
    }

private:
    const char* m_data;
    qint64 m_size;
    qint64 m_pos = 0;
    bool m_ok = true;
};

struct Box {
    quint32 type = 0;
    const char* payload = nullptr;
    qint64 size = 0;
};

// The child boxes of a container's payload.
std::vector<Box> children(const char* data, qint64 size)
{
    std::vector<Box> boxes;
    qint64 pos = 0;
    while (size - pos >= kBoxHeader) {
        qint64 boxSize = qFromBigEndian<quint32>(data + pos);
        const quint32 type = qFromBigEndian<quint32>(data + pos + 4);
        qint64 header = kBoxHeader;
        if (boxSize == 1) {
            if (size - pos < kLargeBoxHeader) break;
            boxSize = qint64(qFromBigEndian<quint64>(data + pos + 8));
            header = kLargeBoxHeader;
        } else if (boxSize == 0) {
            boxSize = size - pos;
        }
        if (boxSize < header || boxSize > size - pos) break;
        boxes.push_back({type, data + pos + header, boxSize - header});
        pos += boxSize;
    }
    return boxes;
}

const Box* child(const std::vector<Box>& boxes, quint32 type)
{
    for (const Box& box : boxes) {
        if (box.type == type) return &box;
    }
    return nullptr;
}

struct VideoTrack {
    quint32 id = 0;
    quint32 timescale = 0;
    quint32 defaultDuration = 0; // from trex
    quint32 defaultSize = 0;     // from trex
    quint32 defaultFlags = 0;    // from trex
    // Per sample, in decode order: decode time, composition offset, size.
    std::vector<qint64> decodeTicks;
    std::vector<qint64> compositionOffsets;
    std::vector<qint64> durations;
    std::vector<qint64> sizes;
    std::vector<bool> sync;
    qint64 endTicks = 0; // decode end of the last sample read
    // Edit list: how long the track shows nothing before its media (movie
    // time, in microseconds).
    qint64 emptyEditUs = 0;

    void add(qint64 ticks, qint64 duration, qint64 offset, qint64 size, bool keyframe)
    {
        decodeTicks.push_back(ticks);
        durations.push_back(duration);
        compositionOffsets.push_back(offset);
        sizes.push_back(size);
        sync.push_back(keyframe);
    }
};

qint64 ticksToUs(qint64 ticks, qint64 timescale)
{
    // Whole seconds and the rest, so a large clock cannot overflow.
    return ticks / timescale * kUsPerSecond + ticks % timescale * kUsPerSecond / timescale;
}

void readEdits(const Box& edts, quint32 movieTimescale, VideoTrack& track)
{
    // Held in a local: child() points into the vector.
    const std::vector<Box> boxes = children(edts.payload, edts.size);
    const Box* elst = child(boxes, fourcc("elst"));
    if (!elst || movieTimescale == 0) return;
    Reader r(elst->payload, elst->size);
    const bool wide = (r.u32() >> 24) == 1;
    const quint32 entries = r.u32();
    for (quint32 e = 0; e < entries && r.ok(); ++e) {
        const qint64 duration = wide ? qint64(r.u64()) : qint64(r.u32());
        const qint64 mediaTime = wide ? qint64(r.u64()) : qint64(qint32(r.u32()));
        r.skip(4); // media_rate
        if (!r.ok()) return;
        if (mediaTime == kEmptyEdit) {
            track.emptyEditUs += ticksToUs(duration, movieTimescale);
            continue;
        }
        // The first edit that shows media: where it starts is the first shown
        // sample (see read()).
        return;
    }
}

// moov/trak: is it video, and its sample table (empty in fragmented files).
bool readTrak(const Box& trak, quint32 movieTimescale, VideoTrack& track)
{
    const std::vector<Box> boxes = children(trak.payload, trak.size);
    const Box* tkhd = child(boxes, fourcc("tkhd"));
    const Box* mdia = child(boxes, fourcc("mdia"));
    if (!tkhd || !mdia) return false;
    const std::vector<Box> mdiaBoxes = children(mdia->payload, mdia->size);
    const Box* hdlr = child(mdiaBoxes, fourcc("hdlr"));
    const Box* mdhd = child(mdiaBoxes, fourcc("mdhd"));
    if (!hdlr || !mdhd) return false;
    Reader handler(hdlr->payload, hdlr->size);
    handler.skip(8); // version/flags, pre_defined
    if (handler.u32() != fourcc("vide") || !handler.ok()) return false;

    Reader header(tkhd->payload, tkhd->size);
    const quint32 versionFlags = header.u32();
    header.skip((versionFlags >> 24) == 1 ? 16 : 8); // creation, modification
    track.id = header.u32();
    Reader media(mdhd->payload, mdhd->size);
    media.skip((media.u32() >> 24) == 1 ? 16 : 8);
    track.timescale = media.u32();
    if (!header.ok() || !media.ok() || track.timescale == 0) return false;
    if (const Box* edts = child(boxes, fourcc("edts"))) readEdits(*edts, movieTimescale, track);

    const Box* minf = child(mdiaBoxes, fourcc("minf"));
    const std::vector<Box> minfBoxes = minf ? children(minf->payload, minf->size) : std::vector<Box>();
    const Box* stbl = child(minfBoxes, fourcc("stbl"));
    if (!stbl) return true;
    const std::vector<Box> stblBoxes = children(stbl->payload, stbl->size);
    const Box* stts = child(stblBoxes, fourcc("stts"));
    const Box* stsz = child(stblBoxes, fourcc("stsz"));
    if (!stts || !stsz) return true;

    Reader sizes(stsz->payload, stsz->size);
    sizes.skip(4);
    const quint32 commonSize = sizes.u32();
    const qint64 count = sizes.u32();
    if (!sizes.ok() || count > kMaxSamples || (commonSize == 0 && sizes.remaining() < count * 4)) return false;
    Reader times(stts->payload, stts->size);
    times.skip(4);
    const quint32 entries = times.u32();
    track.decodeTicks.reserve(size_t(count));
    track.compositionOffsets.reserve(size_t(count));
    track.sizes.reserve(size_t(count));
    track.sync.reserve(size_t(count));
    qint64 ticks = 0;
    for (quint32 e = 0; e < entries && times.ok() && qint64(track.sizes.size()) < count; ++e) {
        const quint32 run = times.u32();
        const quint32 delta = times.u32();
        for (quint32 i = 0; i < run && qint64(track.sizes.size()) < count; ++i) {
            track.add(ticks, delta, 0, commonSize ? commonSize : sizes.u32(), true);
            ticks += delta;
        }
    }
    track.endTicks = ticks;
    if (!times.ok() || !sizes.ok()) return false;

    // Composition offsets (B-frames show after they are decoded).
    if (const Box* ctts = child(stblBoxes, fourcc("ctts"))) {
        Reader r(ctts->payload, ctts->size);
        r.skip(4);
        const quint32 cttsEntries = r.u32();
        size_t index = 0;
        for (quint32 e = 0; e < cttsEntries && index < track.sizes.size(); ++e) {
            const quint32 run = r.u32();
            const qint64 offset = qint32(r.u32()); // signed in version 1, small either way
            // A table cut short leaves the rest at 0: a few frames' shift at
            // most, while the sizes stay usable.
            if (!r.ok()) break;
            for (quint32 i = 0; i < run && index < track.sizes.size(); ++i) {
                track.compositionOffsets[index++] = offset;
            }
        }
    }
    // Keyframes: those listed in stss (1-based); without stss, every sample.
    if (const Box* stss = child(stblBoxes, fourcc("stss"))) {
        std::fill(track.sync.begin(), track.sync.end(), false);
        Reader keys(stss->payload, stss->size);
        keys.skip(4);
        const quint32 keyCount = keys.u32();
        for (quint32 k = 0; k < keyCount && keys.ok(); ++k) {
            const quint32 number = keys.u32();
            if (keys.ok() && number >= 1 && number <= track.sync.size()) track.sync[number - 1] = true;
        }
        if (!keys.ok()) return false;
    }
    return true;
}

void readTrex(const Box& mvex, VideoTrack& track)
{
    for (const Box& trex : children(mvex.payload, mvex.size)) {
        if (trex.type != fourcc("trex")) continue;
        Reader r(trex.payload, trex.size);
        r.skip(4);
        if (r.u32() != track.id) continue;
        r.skip(4); // default_sample_description_index
        track.defaultDuration = r.u32();
        track.defaultSize = r.u32();
        track.defaultFlags = r.u32();
    }
}

bool readMoof(const Box& moof, VideoTrack& track)
{
    for (const Box& traf : children(moof.payload, moof.size)) {
        if (traf.type != fourcc("traf")) continue;
        const std::vector<Box> boxes = children(traf.payload, traf.size);
        const Box* tfhd = child(boxes, fourcc("tfhd"));
        if (!tfhd) continue;
        Reader header(tfhd->payload, tfhd->size);
        const quint32 flags = header.u32() & 0xFFFFFF;
        if (header.u32() != track.id) continue;
        if (flags & kTfhdBaseDataOffset) header.skip(8);
        if (flags & kTfhdSampleDescriptionIndex) header.skip(4);
        quint32 duration = track.defaultDuration;
        quint32 size = track.defaultSize;
        quint32 sampleFlags = track.defaultFlags;
        if (flags & kTfhdDefaultDuration) duration = header.u32();
        if (flags & kTfhdDefaultSize) size = header.u32();
        if (flags & kTfhdDefaultFlags) sampleFlags = header.u32();
        if (!header.ok()) return false;

        qint64 ticks = track.endTicks;
        if (const Box* tfdt = child(boxes, fourcc("tfdt"))) {
            Reader r(tfdt->payload, tfdt->size);
            const bool wide = (r.u32() >> 24) == 1;
            ticks = wide ? qint64(r.u64()) : qint64(r.u32());
            if (!r.ok()) return false;
        }
        for (const Box& trun : boxes) {
            if (trun.type != fourcc("trun")) continue;
            Reader r(trun.payload, trun.size);
            const quint32 trunFlags = r.u32() & 0xFFFFFF;
            const qint64 count = r.u32();
            if (!r.ok() || qint64(track.sizes.size()) + count > kMaxSamples) return false;
            if (trunFlags & kTrunDataOffset) r.skip(4);
            const bool hasFirstFlags = trunFlags & kTrunFirstSampleFlags;
            const quint32 firstFlags = hasFirstFlags ? r.u32() : 0;
            for (qint64 i = 0; i < count && r.ok(); ++i) {
                const quint32 sampleDuration = (trunFlags & kTrunDuration) ? r.u32() : duration;
                const quint32 sampleSize = (trunFlags & kTrunSize) ? r.u32() : size;
                quint32 thisFlags = (i == 0 && hasFirstFlags) ? firstFlags : sampleFlags;
                if (trunFlags & kTrunFlags) thisFlags = r.u32();
                const qint64 offset = (trunFlags & kTrunCompositionOffset) ? qint64(qint32(r.u32())) : 0;
                if (!r.ok()) break;
                track.add(ticks, sampleDuration, offset, sampleSize, !(thisFlags & kSampleNonSync));
                ticks += sampleDuration;
            }
            if (!r.ok()) return false;
        }
        track.endTicks = ticks;
    }
    return true;
}

} // namespace

void Mp4VideoSamples::append(qint64 startUs, qint64 bytes, bool keyframe)
{
    if (m_cumulativeBytes.empty()) m_cumulativeBytes.push_back(0);
    m_startUs.push_back(startUs);
    m_cumulativeBytes.push_back(m_cumulativeBytes.back() + std::max<qint64>(0, bytes));
    if (keyframe) m_keyframes.push_back(m_startUs.size() - 1);
}

Mp4VideoSamples::RangeBytes Mp4VideoSamples::rangeBytes(qint64 startMs, qint64 endMs) const
{
    RangeBytes range;
    if (!isValid() || endMs <= startMs) return range;
    const auto first = std::lower_bound(m_startUs.begin(), m_startUs.end(), startMs * kUsPerMs);
    const auto last = std::lower_bound(m_startUs.begin(), m_startUs.end(), endMs * kUsPerMs);
    if (first == last) {
        // No sample starts inside (a slow or variable frame rate): the one
        // still on screen is shown, and the export encodes it whole. Nothing
        // is shown before the first sample or after the last one ends.
        if (first == m_startUs.begin() || startMs >= this->endMs()) return range;
        const size_t shown = size_t(first - m_startUs.begin()) - 1;
        const auto key = std::upper_bound(m_keyframes.begin(), m_keyframes.end(), shown);
        const size_t keyIndex = key == m_keyframes.begin() ? shown : *(key - 1);
        range.leadingKeyframe = m_cumulativeBytes[keyIndex + 1] - m_cumulativeBytes[keyIndex];
        return range;
    }
    const size_t firstIndex = size_t(first - m_startUs.begin());
    range.frames = m_cumulativeBytes[size_t(last - m_startUs.begin())] - m_cumulativeBytes[firstIndex];
    const auto key = std::upper_bound(m_keyframes.begin(), m_keyframes.end(), firstIndex);
    if (key != m_keyframes.begin() && *(key - 1) == firstIndex) {
        // Starts on a keyframe: the whole first frame is in the range.
        range.firstKeyframe = m_cumulativeBytes[firstIndex + 1] - m_cumulativeBytes[firstIndex];
    } else if (key != m_keyframes.begin()) {
        // Starts mid-GOP: the export must encode its first frame whole,
        // about what the keyframe it was predicted from cost.
        const size_t keyIndex = *(key - 1);
        range.leadingKeyframe = m_cumulativeBytes[keyIndex + 1] - m_cumulativeBytes[keyIndex];
    }
    return range;
}

qint64 Mp4VideoSamples::bytesBetween(qint64 startMs, qint64 endMs) const
{
    return rangeBytes(startMs, endMs).frames;
}

qint64 Mp4VideoSamples::bytesToEncode(qint64 startMs, qint64 endMs) const
{
    const RangeBytes range = rangeBytes(startMs, endMs);
    return range.frames + range.leadingKeyframe;
}

qint64 Mp4VideoSamples::leadingKeyframeBytes(qint64 startMs, qint64 endMs) const
{
    return rangeBytes(startMs, endMs).leadingKeyframe;
}

qint64 Mp4VideoSamples::endMs() const
{
    if (m_startUs.empty()) return 0;
    return std::max(m_endUs, m_startUs.back()) / kUsPerMs;
}

Mp4VideoSamples Mp4VideoSamples::read(const QString& path)
{
    Mp4VideoSamples result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return result;
    const qint64 fileSize = file.size();

    VideoTrack track;
    bool haveTrack = false;
    qint64 pos = 0;
    while (fileSize - pos >= kBoxHeader) {
        if (!file.seek(pos)) break;
        const QByteArray head = file.read(kLargeBoxHeader);
        if (head.size() < kBoxHeader) break;
        qint64 boxSize = qFromBigEndian<quint32>(head.constData());
        const quint32 type = qFromBigEndian<quint32>(head.constData() + 4);
        qint64 header = kBoxHeader;
        if (boxSize == 1) {
            if (head.size() < kLargeBoxHeader) break;
            boxSize = qint64(qFromBigEndian<quint64>(head.constData() + 8));
            header = kLargeBoxHeader;
        } else if (boxSize == 0) {
            boxSize = fileSize - pos;
        }
        // A box cut short (a recording still being written) ends the scan.
        if (boxSize < header || boxSize > fileSize - pos) break;

        if (type == fourcc("moov") || (type == fourcc("moof") && haveTrack)) {
            if (boxSize - header > kMaxMetadataBox || !file.seek(pos + header)) break;
            const QByteArray payload = file.read(boxSize - header);
            if (payload.size() != boxSize - header) break;
            if (type == fourcc("moov")) {
                const std::vector<Box> boxes = children(payload.constData(), payload.size());
                quint32 movieTimescale = 0;
                if (const Box* mvhd = child(boxes, fourcc("mvhd"))) {
                    Reader r(mvhd->payload, mvhd->size);
                    r.skip((r.u32() >> 24) == 1 ? 16 : 8); // creation, modification
                    movieTimescale = r.u32();
                }
                for (const Box& box : boxes) {
                    if (box.type != fourcc("trak")) continue;
                    VideoTrack candidate;
                    if (readTrak(box, movieTimescale, candidate)) {
                        track = std::move(candidate);
                        haveTrack = true;
                        break;
                    }
                }
                if (!haveTrack) return result;
                if (const Box* mvex = child(boxes, fourcc("mvex"))) readTrex(*mvex, track);
            } else if (!readMoof({type, payload.constData(), payload.size()}, track)) {
                break;
            }
        }
        pos += boxSize;
    }
    if (!haveTrack || track.timescale == 0 || track.sizes.empty()) return result;

    // Presentation times, as the preview's source time counts them: decode
    // time plus the composition offset, counted from the first shown sample,
    // after any empty edit (video that starts after the audio). The first
    // shown sample is where the edit list starts the media in recordings;
    // taking it rather than the edit's media time also holds when a
    // fragmented file starts its decode clock elsewhere (tfdt). Edits after
    // the first are not followed: recordings have one.
    const qint64 timescale = track.timescale;
    std::vector<qint64> showTicks(track.sizes.size());
    for (size_t i = 0; i < showTicks.size(); ++i) {
        showTicks[i] = track.decodeTicks[i] + track.compositionOffsets[i];
    }
    const qint64 originTicks = *std::min_element(showTicks.begin(), showTicks.end());
    const qint64 originUs = track.emptyEditUs;
    // In presentation order: fragments whose clock restarts, or B-frames, can
    // come out of order, and the ranges are searched by time.
    std::vector<size_t> order(showTicks.size());
    std::iota(order.begin(), order.end(), size_t(0));
    std::stable_sort(order.begin(), order.end(), [&showTicks](size_t a, size_t b) { return showTicks[a] < showTicks[b]; });
    qint64 endTicks = originTicks;
    for (const size_t i : order) {
        result.append(originUs + ticksToUs(showTicks[i] - originTicks, timescale), track.sizes[i], track.sync[i]);
        endTicks = std::max(endTicks, showTicks[i] + track.durations[i]);
    }
    result.setEndUs(originUs + ticksToUs(endTicks - originTicks, timescale));
    return result;
}

} // namespace SnapTray
