#include "video/Mp4VideoSamples.h"

#include <QFile>
#include <QtEndian>

#include <algorithm>

namespace SnapTray {

namespace {

constexpr qint64 kUsPerSecond = 1000000;
constexpr qint64 kUsPerMs = 1000;
constexpr qint64 kBoxHeader = 8;
constexpr qint64 kLargeBoxHeader = 16;
// moov and moof are read whole; mdat is skipped. A moov this big is not a
// recording's (an hour of 60 fps is about 1 MB of sample sizes).
constexpr qint64 kMaxMetadataBox = 64 * 1024 * 1024;

constexpr quint32 fourcc(const char (&s)[5])
{
    return (quint32(quint8(s[0])) << 24) | (quint32(quint8(s[1])) << 16) | (quint32(quint8(s[2])) << 8)
           | quint32(quint8(s[3]));
}

// Track-fragment header flags (ISO/IEC 14496-12, 8.8.7 and 8.8.8).
constexpr quint32 kTfhdBaseDataOffset = 0x000001;
constexpr quint32 kTfhdSampleDescriptionIndex = 0x000002;
constexpr quint32 kTfhdDefaultDuration = 0x000008;
constexpr quint32 kTfhdDefaultSize = 0x000010;
constexpr quint32 kTfhdDefaultFlags = 0x000020;
// sample_is_non_sync_sample in the sample flags (8.8.3.1).
constexpr quint32 kSampleNonSync = 0x00010000;
constexpr quint32 kTrunDataOffset = 0x000001;
constexpr quint32 kTrunFirstSampleFlags = 0x000004;
constexpr quint32 kTrunDuration = 0x000100;
constexpr quint32 kTrunSize = 0x000200;
constexpr quint32 kTrunFlags = 0x000400;
constexpr quint32 kTrunCompositionOffset = 0x000800;

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
    const char* here() const { return m_data + m_pos; }

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
    std::vector<qint64> startTicks;
    std::vector<qint64> sizes;
    std::vector<bool> sync;
    qint64 endTicks = 0; // decode end of the last sample read
};

// moov/trak: is it video, and its sample table (empty in fragmented files).
bool readTrak(const Box& trak, VideoTrack& track)
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
    const quint32 count = sizes.u32();
    if (!sizes.ok() || (commonSize == 0 && sizes.remaining() < qint64(count) * 4)) return false;
    Reader times(stts->payload, stts->size);
    times.skip(4);
    const quint32 entries = times.u32();
    track.startTicks.reserve(count);
    track.sizes.reserve(count);
    qint64 ticks = 0;
    for (quint32 e = 0; e < entries && times.ok() && track.startTicks.size() < count; ++e) {
        const quint32 run = times.u32();
        const quint32 delta = times.u32();
        for (quint32 i = 0; i < run && track.startTicks.size() < count; ++i) {
            track.startTicks.push_back(ticks);
            track.sizes.push_back(commonSize ? commonSize : sizes.u32());
            ticks += delta;
        }
    }
    track.endTicks = ticks;
    // Keyframes: those listed in stss (1-based); without stss, every sample.
    const Box* stss = child(stblBoxes, fourcc("stss"));
    track.sync.assign(track.startTicks.size(), stss == nullptr);
    if (stss) {
        Reader keys(stss->payload, stss->size);
        keys.skip(4);
        const quint32 keyCount = keys.u32();
        for (quint32 k = 0; k < keyCount && keys.ok(); ++k) {
            const quint32 number = keys.u32();
            if (keys.ok() && number >= 1 && number <= track.sync.size()) track.sync[number - 1] = true;
        }
        if (!keys.ok()) return false;
    }
    return times.ok() && sizes.ok();
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
            const quint32 count = r.u32();
            if (trunFlags & kTrunDataOffset) r.skip(4);
            const bool hasFirstFlags = trunFlags & kTrunFirstSampleFlags;
            const quint32 firstFlags = hasFirstFlags ? r.u32() : 0;
            for (quint32 i = 0; i < count && r.ok(); ++i) {
                const quint32 sampleDuration = (trunFlags & kTrunDuration) ? r.u32() : duration;
                const quint32 sampleSize = (trunFlags & kTrunSize) ? r.u32() : size;
                quint32 thisFlags = (i == 0 && hasFirstFlags) ? firstFlags : sampleFlags;
                if (trunFlags & kTrunFlags) thisFlags = r.u32();
                if (trunFlags & kTrunCompositionOffset) r.skip(4);
                if (!r.ok()) break;
                track.startTicks.push_back(ticks);
                track.sizes.push_back(sampleSize);
                track.sync.push_back(!(thisFlags & kSampleNonSync));
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

qint64 Mp4VideoSamples::bytesToEncode(qint64 startMs, qint64 endMs) const
{
    if (!isValid() || endMs <= startMs) return 0;
    const auto first = std::lower_bound(m_startUs.begin(), m_startUs.end(), startMs * kUsPerMs);
    const auto last = std::lower_bound(m_startUs.begin(), m_startUs.end(), endMs * kUsPerMs);
    if (first == last) return 0;
    const size_t firstIndex = size_t(first - m_startUs.begin());
    qint64 bytes = m_cumulativeBytes[size_t(last - m_startUs.begin())] - m_cumulativeBytes[firstIndex];
    // The kept part starts mid-GOP: the export must encode its first frame
    // whole, so count the keyframe it was predicted from (PR #213 review).
    const auto key = std::upper_bound(m_keyframes.begin(), m_keyframes.end(), firstIndex);
    if (key != m_keyframes.begin() && *(key - 1) != firstIndex) {
        const size_t keyIndex = *(key - 1);
        bytes += m_cumulativeBytes[keyIndex + 1] - m_cumulativeBytes[keyIndex];
    }
    return bytes;
}

qint64 Mp4VideoSamples::bytesBetween(qint64 startMs, qint64 endMs) const
{
    if (!isValid() || endMs <= startMs) return 0;
    const auto first = std::lower_bound(m_startUs.begin(), m_startUs.end(), startMs * kUsPerMs);
    const auto last = std::lower_bound(m_startUs.begin(), m_startUs.end(), endMs * kUsPerMs);
    return m_cumulativeBytes[size_t(last - m_startUs.begin())] - m_cumulativeBytes[size_t(first - m_startUs.begin())];
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
                for (const Box& box : boxes) {
                    if (box.type != fourcc("trak")) continue;
                    VideoTrack candidate;
                    if (readTrak(box, candidate)) {
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
    if (!haveTrack || track.timescale == 0 || track.startTicks.empty()) return result;
    // Times are decode times from the track's own clock. A fragmented file
    // may start that clock anywhere (tfdt), so count from the first sample,
    // which is where the preview's source time 0 is. Ticks are split into
    // whole seconds and the rest so a large clock cannot overflow.
    const qint64 first = *std::min_element(track.startTicks.begin(), track.startTicks.end());
    const qint64 timescale = track.timescale;
    for (size_t i = 0; i < track.startTicks.size(); ++i) {
        const qint64 ticks = track.startTicks[i] - first;
        result.append(ticks / timescale * kUsPerSecond + ticks % timescale * kUsPerSecond / timescale,
                      track.sizes[i], i < track.sync.size() ? bool(track.sync[i]) : true);
    }
    return result;
}

} // namespace SnapTray
