#ifndef ANNOTATIONPERFTRACE_H
#define ANNOTATIONPERFTRACE_H

#include <QDebug>
#include <QRect>
#include <QString>

#include <map>

#include "tools/ToolId.h"

namespace AnnotationPerfTrace {

enum class UpdateMode {
    Full,
    Rect,
    SurfaceSet
};

struct Sample {
    QString host;
    ToolId tool = ToolId::Selection;
    QString interaction = QStringLiteral("idle");
    UpdateMode updateMode = UpdateMode::Full;
    QRect dirtyBounds;
    qint64 dirtyArea = 0;
    qreal bgMs = 0.0;
    qreal committedMs = 0.0;
    qreal previewMs = 0.0;
    qreal overlayMs = 0.0;
    qreal totalMs = 0.0;
    qreal dpr = 1.0;
    int annoCount = 0;
    bool baseCacheHit = false;
};

inline QString toolName(ToolId tool)
{
    switch (tool) {
    case ToolId::Selection: return QStringLiteral("selection");
    case ToolId::Pencil: return QStringLiteral("pencil");
    case ToolId::Marker: return QStringLiteral("marker");
    case ToolId::Arrow: return QStringLiteral("arrow");
    case ToolId::Polyline: return QStringLiteral("polyline");
    case ToolId::Shape: return QStringLiteral("shape");
    case ToolId::Text: return QStringLiteral("text");
    case ToolId::EmojiSticker: return QStringLiteral("emoji");
    case ToolId::StepBadge: return QStringLiteral("step");
    case ToolId::Mosaic: return QStringLiteral("mosaic");
    case ToolId::Crop: return QStringLiteral("crop");
    case ToolId::Measure: return QStringLiteral("measure");
    default: return QStringLiteral("other");
    }
}

inline const char* updateModeName(UpdateMode mode)
{
    switch (mode) {
    case UpdateMode::Full:
        return "full";
    case UpdateMode::Rect:
        return "rect";
    case UpdateMode::SurfaceSet:
        return "surface-set";
    }

    return "full";
}

inline bool isEnabled(const char* legacyEnv = nullptr)
{
    if (qEnvironmentVariableIntValue("SNAPTRAY_ANNOTATION_PERF_TRACE") > 0) {
        return true;
    }

    return legacyEnv && qEnvironmentVariableIntValue(legacyEnv) > 0;
}

struct Summary {
    int frames = 0;
    int slowFrames = 0;
    qreal totalMsSum = 0.0;
    qreal bgMsSum = 0.0;
    qreal committedMsSum = 0.0;
    qreal previewMsSum = 0.0;
    qreal overlayMsSum = 0.0;
    qreal maxTotalMs = 0.0;
};

inline std::map<QString, Summary>& summaries()
{
    static std::map<QString, Summary> state;
    return state;
}

inline void logFrame(const Sample& sample, const char* legacyEnv = nullptr)
{
    if (!isEnabled(legacyEnv)) {
        return;
    }

    Summary& summary = summaries()[sample.host];
    summary.frames += 1;
    summary.totalMsSum += sample.totalMs;
    summary.bgMsSum += sample.bgMs;
    summary.committedMsSum += sample.committedMs;
    summary.previewMsSum += sample.previewMs;
    summary.overlayMsSum += sample.overlayMs;
    summary.maxTotalMs = qMax(summary.maxTotalMs, sample.totalMs);
    if (sample.totalMs > 16.0) {
        summary.slowFrames += 1;
    }

    qDebug().noquote()
        << QStringLiteral("AnnotationPerf")
        << QStringLiteral("frame=%1").arg(summary.frames)
        << QStringLiteral("host=%1").arg(sample.host)
        << QStringLiteral("tool=%1").arg(toolName(sample.tool))
        << QStringLiteral("interaction=%1").arg(sample.interaction)
        << QStringLiteral("updateMode=%1").arg(QLatin1String(updateModeName(sample.updateMode)))
        << QStringLiteral("dirtyBounds=%1x%2").arg(sample.dirtyBounds.width()).arg(sample.dirtyBounds.height())
        << QStringLiteral("dirtyArea=%1").arg(sample.dirtyArea)
        << QStringLiteral("bgMs=%1").arg(sample.bgMs, 0, 'f', 3)
        << QStringLiteral("committedMs=%1").arg(sample.committedMs, 0, 'f', 3)
        << QStringLiteral("previewMs=%1").arg(sample.previewMs, 0, 'f', 3)
        << QStringLiteral("overlayMs=%1").arg(sample.overlayMs, 0, 'f', 3)
        << QStringLiteral("totalMs=%1").arg(sample.totalMs, 0, 'f', 3)
        << QStringLiteral("annoCount=%1").arg(sample.annoCount)
        << QStringLiteral("dpr=%1").arg(sample.dpr, 0, 'f', 2)
        << QStringLiteral("baseCacheHit=%1").arg(sample.baseCacheHit ? 1 : 0);

    if ((summary.frames % 60) == 0) {
        const qreal frameCount = static_cast<qreal>(summary.frames);
        qDebug().noquote()
            << QStringLiteral("AnnotationPerf summary")
            << QStringLiteral("host=%1").arg(sample.host)
            << QStringLiteral("frames=%1").arg(summary.frames)
            << QStringLiteral("slow=%1").arg(summary.slowFrames)
            << QStringLiteral("avgTotalMs=%1").arg(summary.totalMsSum / frameCount, 0, 'f', 3)
            << QStringLiteral("avgBgMs=%1").arg(summary.bgMsSum / frameCount, 0, 'f', 3)
            << QStringLiteral("avgCommittedMs=%1").arg(summary.committedMsSum / frameCount, 0, 'f', 3)
            << QStringLiteral("avgPreviewMs=%1").arg(summary.previewMsSum / frameCount, 0, 'f', 3)
            << QStringLiteral("avgOverlayMs=%1").arg(summary.overlayMsSum / frameCount, 0, 'f', 3)
            << QStringLiteral("maxTotalMs=%1").arg(summary.maxTotalMs, 0, 'f', 3);
    }
}

} // namespace AnnotationPerfTrace

#endif // ANNOTATIONPERFTRACE_H
