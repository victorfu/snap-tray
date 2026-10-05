#include "longshot/LongshotSession.h"

#include "utils/VideoCropGeometry.h"

#include <QDateTime>
#include <QDebug>
#include <QFileInfo>

#include <algorithm>
#include <limits>

namespace SnapTray::Longshot {

SourceIdentity SourceIdentity::fromFile(const QString& path)
{
    SourceIdentity id;
    id.path = path;
    const QFileInfo info(path);
    if (info.exists()) {
        id.sizeBytes = info.size();
        id.modifiedMsSinceEpoch = info.lastModified().toMSecsSinceEpoch();
    }
    return id;
}

DecodedFrameCache::DecodedFrameCache(qint64 budgetBytes) : m_budgetBytes(budgetBytes) {}

std::optional<QImage> DecodedFrameCache::find(const SourceIdentity& source, qint64 tMs) const
{
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it->tMs == tMs && it->source == source) {
            m_entries.splice(m_entries.begin(), m_entries, it); // most recent first
            return m_entries.front().frame;
        }
    }
    return std::nullopt;
}

void DecodedFrameCache::store(const SourceIdentity& source, qint64 tMs, const QImage& frame)
{
    const qint64 size = qint64(frame.sizeInBytes());
    if (size > m_budgetBytes) return; // one frame may not evict the whole cache
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it->tMs == tMs && it->source == source) {
            m_bytes -= qint64(it->frame.sizeInBytes());
            m_entries.erase(it);
            break;
        }
    }
    while (!m_entries.empty() && m_bytes + size > m_budgetBytes) {
        m_bytes -= qint64(m_entries.back().frame.sizeInBytes());
        m_entries.pop_back();
    }
    m_entries.push_front({source, tMs, frame});
    m_bytes += size;
}

void DecodedFrameCache::clear()
{
    m_entries.clear();
    m_bytes = 0;
}

LongshotSession::LongshotSession(SourceFactory factory, qint64 decodeCacheBudgetBytes)
    : m_factory(std::move(factory)), m_decodeCache(decodeCacheBudgetBytes)
{
}

void LongshotSession::setRecording(const QString& path) { if (m_path != path) m_candidateKey.reset(); m_path = path; }
void LongshotSession::setCrop(const QRect& crop) { if (m_crop != crop) m_candidateKey.reset(); m_crop = crop; }
void LongshotSession::setTrim(qint64 startMs, qint64 endMs) { if (m_startMs != startMs || m_endMs != endMs) m_candidateKey.reset(); m_startMs = startMs; m_endMs = endMs; }
void LongshotSession::setOptions(const LongshotOptions& options) { if (!(m_options == options)) m_candidateKey.reset(); m_options = options; }
void LongshotSession::setPipelineParams(const PipelineParams& params)
{
    m_candidateKey.reset();
    m_params = params;
    m_analysisKey.reset(); // parameters shape every cached analysis result
    m_renderOptions.reset();
}

RunReport LongshotSession::run(const ProgressFn& progress, const StageFn& stage)
{
    m_candidates.clear();
    m_candidateKey.reset();
    return runAnalysis(progress, stage, true);
}

AnalysisReport LongshotSession::analyze(const ProgressFn& progress, const StageFn& stage)
{
    m_candidates.clear();
    m_candidateKey.reset();
    m_render = {};
    m_renderOptions.reset();
    const auto report = runAnalysis(progress, stage, false);
    AnalysisReport result;
    result.error = report.error;
    if (result.error != LongshotError::None) return result;
    auto options = m_options;
    options.splitOversize = true;
    result.candidates = LongshotRenderer::candidates(m_analysis, options, progress);
    if (progress && !progress(100)) { result = {}; result.error = LongshotError::Cancelled; return result; }
    result.noScrolling = result.candidates.empty() && !m_analysis.frames.empty()
        && std::all_of(m_analysis.edges.begin(), m_analysis.edges.end(), [](const PairShift& edge) { return edge.dy == 0; })
        && !m_analysis.edges.empty();
    m_candidates = result.candidates;
    m_candidateKey = m_analysisKey;
    return result;
}

RenderResult LongshotSession::renderCandidate(const QString& id, const ProgressFn& progress)
{
    RenderResult result;
    // File replacement and input changes must never render an obsolete plan.
    if (!m_candidateKey || !m_analysisKey || !(*m_candidateKey == *m_analysisKey)
        || !(m_candidateKey->source == SourceIdentity::fromFile(m_path))
        || m_candidateKey->startMs != m_startMs || m_candidateKey->endMs != m_endMs) {
        result.error = LongshotError::SourceUnavailable; return result;
    }
    const auto found = std::find_if(m_candidates.begin(), m_candidates.end(), [&](const auto& candidate) { return candidate.id == id; });
    if (found == m_candidates.end()) { result.error = LongshotError::NoReliableContent; return result; }
    auto source = m_factory ? m_factory() : nullptr;
    if (!source) { result.error = LongshotError::SourceUnavailable; return result; }
    return LongshotRenderer::renderCandidate(*source, m_path, m_startMs, m_endMs, m_crop, m_analysis, *found, progress);
}

RunReport LongshotSession::runAnalysis(const ProgressFn& progress, const StageFn& stage, bool renderOutput)
{
    RunReport report;
    if (stage) stage(WorkStage::Analyze);
    if (progress && !progress(0)) { report.error = LongshotError::Cancelled; return report; }
    if (!m_factory) { report.error = LongshotError::SourceUnavailable; return report; }
    std::unique_ptr<LongshotFrameSource> source = m_factory();
    if (!source) { report.error = LongshotError::SourceUnavailable; return report; }
    if (!source->open(m_path, m_startMs, m_endMs, m_crop)) {
        qWarning() << "LongshotSession: open failed:" << source->lastError();
        // The crop is to blame only when the file itself was probed and the
        // resulting frame is below the analysis minimum.
        const QSize probed = source->videoSize();
        const QSize frame = source->frameSize();
        const bool cropTooSmall = !probed.isEmpty()
                                  && (frame.width() < kMinAnalysisSide || frame.height() < kMinAnalysisSide);
        report.error = cropTooSmall ? LongshotError::CropTooSmall : LongshotError::SourceUnavailable;
        return report;
    }

    AnalysisKey key;
    key.source = SourceIdentity::fromFile(m_path);
    key.normalizedCrop = m_crop.isEmpty() ? QRect() : VideoCropGeometry::normalizeCropRect(m_crop, source->videoSize());
    key.startMs = m_startMs;
    key.endMs = m_endMs;

    const bool analysisValid = m_analysisKey.has_value() && m_analysis.error == LongshotError::None;
    const bool sameAnalysis = analysisValid && *m_analysisKey == key;
    const bool sameSourceCrop = analysisValid && m_analysisKey->sameSourceAndCrop(key);

    if (sameAnalysis) {
        report.reusedFeatures = true;
        report.reusedSolve = true;
    } else if (sameSourceCrop) {
        // Trim change: per-frame features inside the new range are reused,
        // only newly included frames are analysed, edges/solve are rebuilt.
        const qint64 newEnd = m_endMs < 0 ? std::numeric_limits<qint64>::max() : m_endMs;
        std::vector<FrameFeatures> known;
        std::vector<QImage> knownThumbs;
        for (size_t i = 0; i < m_analysis.frames.size() && i < m_analysis.thumbnails.size(); ++i) {
            const qint64 t = m_analysis.frames[i].tMs;
            if (t >= m_startMs && t < newEnd) {
                known.push_back(m_analysis.frames[i]);
                knownThumbs.push_back(m_analysis.thumbnails[i]);
            }
        }
        AnalysisResult updated = LongshotPipeline::analyzeIncremental(*source, m_path, m_startMs, m_endMs, m_crop, m_params,
                                                                      progress, known, knownThumbs, &report.framesAnalyzed, stage);
        if (updated.error != LongshotError::None) {
            // Keep the previous analysis and key: a cancelled trim edit must not discard reusable work.
            report.error = updated.error;
            report.analysis = std::move(updated);
            return report;
        }
        // Reused = frames of the new analysis whose features came from the old one.
        report.reusedFeatures = int(updated.frames.size()) - report.framesAnalyzed > 0;
        m_analysis = std::move(updated);
        // The previous render belongs to the old trim, even if cancellation
        // prevents rendering this newly committed analysis.
        m_renderOptions.reset();
    } else {
        // Source or crop changed: nothing crop-dependent survives.
        m_analysis = LongshotPipeline::analyze(*source, m_path, m_startMs, m_endMs, m_crop, m_params, progress, stage);
        report.framesAnalyzed = int(m_analysis.frames.size());
        m_renderOptions.reset();
    }
    m_analysisKey = key;
    report.analysis = m_analysis;
    if (m_analysis.error != LongshotError::None) {
        report.error = m_analysis.error;
        m_renderOptions.reset();
        return report;
    }

    if (!renderOutput) return report;
    if (stage) stage(WorkStage::Render);
    if (progress && !progress(0)) { report.error = LongshotError::Cancelled; return report; }
    // Rendered tiles survive only when analysis and options are both unchanged.
    const bool sameRender = sameAnalysis && m_renderOptions.has_value() && *m_renderOptions == m_options
                            && m_render.error == LongshotError::None;
    if (sameRender) {
        report.reusedRender = true;
    } else {
        m_render = LongshotRenderer::renderSections(*source, m_path, m_startMs, m_endMs, m_crop, m_analysis, m_options, progress);
        m_renderOptions = m_options;
    }
    report.render = m_render;
    report.error = m_render.error;
    qDebug() << "LongshotSession: reused features" << report.reusedFeatures << "solve" << report.reusedSolve
             << "render" << report.reusedRender << "analysed" << report.framesAnalyzed;
    return report;
}

} // namespace SnapTray::Longshot
