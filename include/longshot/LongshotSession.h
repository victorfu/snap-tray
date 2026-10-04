#pragma once

#include "longshot/LongshotFrameSource.h"
#include "longshot/LongshotPipeline.h"
#include "longshot/LongshotRenderer.h"
#include "longshot/LongshotTypes.h"

#include <QImage>
#include <QRect>
#include <QString>

#include <functional>
#include <list>
#include <memory>
#include <optional>

namespace SnapTray::Longshot {

constexpr qint64 kDecodeCacheBudgetBytes = qint64(128) * 1024 * 1024;

struct SourceIdentity {
    QString path;
    qint64 sizeBytes = 0;
    qint64 modifiedMsSinceEpoch = 0;
    static SourceIdentity fromFile(const QString& path);
    bool operator==(const SourceIdentity& o) const
    {
        return path == o.path && sizeBytes == o.sizeBytes && modifiedMsSinceEpoch == o.modifiedMsSinceEpoch;
    }
    bool operator!=(const SourceIdentity& o) const { return !(*this == o); }
};

struct AnalysisKey {
    SourceIdentity source;
    QRect normalizedCrop;
    qint64 startMs = 0;
    qint64 endMs = -1;
    int analysisVersion = kAnalysisVersion;
    bool sameSourceAndCrop(const AnalysisKey& o) const
    {
        return source == o.source && normalizedCrop == o.normalizedCrop && analysisVersion == o.analysisVersion;
    }
    bool operator==(const AnalysisKey& o) const { return sameSourceAndCrop(o) && startMs == o.startMs && endMs == o.endMs; }
};

// Bounded LRU of decoded full frames keyed by (source identity, media time).
// Separate from analysis: crop changes do not touch it. Single-thread only.
class DecodedFrameCache
{
public:
    explicit DecodedFrameCache(qint64 budgetBytes);
    std::optional<QImage> find(const SourceIdentity& source, qint64 tMs) const;
    void store(const SourceIdentity& source, qint64 tMs, const QImage& frame);
    qint64 bytes() const { return m_bytes; }
    int count() const { return int(m_entries.size()); }
    void clear();

private:
    struct Entry { SourceIdentity source; qint64 tMs; QImage frame; };
    mutable std::list<Entry> m_entries; // front = most recent
    qint64 m_budgetBytes;
    qint64 m_bytes = 0;
};

struct RunReport {
    LongshotError error = LongshotError::None;
    bool reusedFeatures = false;
    bool reusedSolve = false;
    bool reusedRender = false;
    int framesAnalyzed = 0;
    // Copies of the session's cached results (also on pure reuse); the
    // PipelineParams::maxAnalyzedFrames budget bounds their size.
    AnalysisResult analysis;
    RenderResult render;
};

// Owns the cache contract between the preview's edits and the engine.
class LongshotSession
{
public:
    using SourceFactory = std::function<std::unique_ptr<LongshotFrameSource>()>;

    explicit LongshotSession(SourceFactory factory, qint64 decodeCacheBudgetBytes = kDecodeCacheBudgetBytes);

    void setRecording(const QString& path);
    void setCrop(const QRect& cropVideoPixels);
    void setTrim(qint64 startMs, qint64 endMs);
    void setOptions(const LongshotOptions& options);
    void setPipelineParams(const PipelineParams& params);

    RunReport run(const ProgressFn& progress, const StageFn& stage = {});

    const DecodedFrameCache& decodeCache() const { return m_decodeCache; }
    DecodedFrameCache& decodeCache() { return m_decodeCache; }

private:
    SourceFactory m_factory;
    DecodedFrameCache m_decodeCache;
    QString m_path;
    QRect m_crop;
    qint64 m_startMs = 0;
    qint64 m_endMs = -1;
    LongshotOptions m_options;
    PipelineParams m_params;

    std::optional<AnalysisKey> m_analysisKey;
    AnalysisResult m_analysis;
    std::optional<LongshotOptions> m_renderOptions;
    RenderResult m_render;
};

} // namespace SnapTray::Longshot
