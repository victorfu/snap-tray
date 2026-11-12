#include "ContentExport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QSaveFile>
#include <QTemporaryFile>

namespace snaptray::scrollcapture::probe {

QString positionSyncUnavailableReason(bool settled, bool hasOffsetBefore, bool hasOffsetAfter,
                                      const ShiftEstimate& visual)
{
    if (!settled) return QStringLiteral("not_settled");
    if (!hasOffsetBefore) return QStringLiteral("position_missing_offset_before");
    if (!hasOffsetAfter) return QStringLiteral("position_missing_offset_after");
    if (visual.status != ShiftEstimate::Status::Found)
        return QStringLiteral("visual_shift_") + QLatin1String(shiftEstimateStatusName(visual.status));
    return {};
}

ContentRepeat validateContentRepeat(const QImage& before, const QImage& after,
                                    FrameSettleDetector::Outcome settle,
                                    const QString& positionSync, bool sameBounds,
                                    const ShiftEstimate& dominant, const QString& syncUnavailableReason)
{
    const auto rejected = [](const QString& reason) { return ContentRepeat{std::nullopt, reason, false}; };
    if (before.isNull() || after.isNull()) return rejected(QStringLiteral("capture_failed"));
    if (settle != FrameSettleDetector::Outcome::Settled) {
        return rejected(QStringLiteral("settle_") + QLatin1String(settleOutcomeName(settle)));
    }
    if (!sameBounds || before.size() != after.size()) return rejected(QStringLiteral("geometry_changed"));
    // Use the reason recorded by the sync gate, not the later prior-constrained estimate.
    if (positionSync == QLatin1String("not_available")
        && !syncUnavailableReason.isEmpty()) {
        return rejected(syncUnavailableReason);
    }
    if (positionSync != QLatin1String("within_search_window")) {
        return rejected(QStringLiteral("position_sync_") + positionSync);
    }
    if (before == after) return {std::nullopt, {}, true};
    if (dominant.status != ShiftEstimate::Status::Found) {
        return rejected(QStringLiteral("dominant_shift_") + QLatin1String(shiftEstimateStatusName(dominant.status)));
    }
    if (dominant.shift <= 0) return rejected(QStringLiteral("non_positive_shift"));
    if (dominant.shift >= before.height() / 2) return rejected(QStringLiteral("shift_too_large"));
    return {ScrollContentPair{before, after, dominant.shift}, {}, false};
}

QString contentOutputPathError(const QString& path)
{
    const QFileInfo destination(path);
    if (destination.exists() && !destination.isFile()) return QStringLiteral("destination is not a regular file");
    const QDir parent = destination.absoluteDir();
    if (!parent.exists()) return QStringLiteral("parent directory does not exist");
    QTemporaryFile check(parent.filePath(QStringLiteral(".snaptray-content-XXXXXX")));
    if (!check.open()) return check.errorString();
    return {};
}

ContentSaveResult saveContentImage(const QImage& image, const QString& path)
{
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) return {QStringLiteral("open"), output.errorString()};
    QImageWriter writer(&output, "PNG");
    if (!writer.write(image)) {
        const QString detail = writer.errorString();
        output.cancelWriting();
        return {QStringLiteral("encode"), detail};
    }
    if (!output.commit()) return {QStringLiteral("commit"), output.errorString()};
    return {};
}

void ContentExportSession::add(int repeatIndex, const ContentRepeat& repeat)
{
    if (!m_error.isEmpty()) return;
    if (!repeat.error.isEmpty()) {
        m_error = repeat.error;
    } else if (repeat.noMotion) {
        if (m_pairs.isEmpty()) m_error = QStringLiteral("no_scroll_motion");
        else if (!m_endedAtRepeat) m_endedAtRepeat = repeatIndex;
    } else if (m_endedAtRepeat) {
        m_error = QStringLiteral("motion_after_no_motion_repeat");
    } else if (!repeat.pair) {
        m_error = QStringLiteral("invalid_content_repeat");
    } else {
        const qint64 pixels = qint64(repeat.pair->before.width()) * repeat.pair->before.height() * 2;
        if (pixels > m_maxPixels - m_retainedPixels) {
            m_error = QStringLiteral("capture_budget_exceeded");
        } else {
            m_retainedPixels += pixels;
            m_pairs.append(*repeat.pair);
        }
    }
    if (!m_error.isEmpty()) {
        m_failedRepeat = repeatIndex;
        m_pairs.clear();
    }
}

QJsonObject ContentExportSession::finish(const QString& path, bool interrupted) const
{
    QJsonObject content{{QStringLiteral("status"), QStringLiteral("rejected")},
                        {QStringLiteral("path"), path}};
    QString error = interrupted ? QStringLiteral("interrupted") : m_error;
    if (error.isEmpty()) {
        const auto assembled = assembleScrollContent(m_pairs, m_maxPixels);
        if (!assembled.ok()) {
            error = assembled.error;
        } else {
            const auto saved = saveContentImage(assembled.image, path);
            if (!saved.ok()) {
                error = QStringLiteral("save_failed");
                content.insert(QStringLiteral("stage"), saved.stage);
                content.insert(QStringLiteral("detail"), saved.detail);
            } else {
                const QRect& rect = assembled.contentRect;
                content.insert(QStringLiteral("status"), QStringLiteral("exported"));
                content.insert(QStringLiteral("viewportPx"), QJsonObject{{QStringLiteral("x"), rect.x()},
                    {QStringLiteral("y"), rect.y()}, {QStringLiteral("width"), rect.width()},
                    {QStringLiteral("height"), rect.height()}});
                content.insert(QStringLiteral("outputWidth"), assembled.image.width());
                content.insert(QStringLiteral("outputHeight"), assembled.image.height());
                content.insert(QStringLiteral("initialFrameFullWidth"), true);
                content.insert(QStringLiteral("excludedColumns"), QStringLiteral("transparent_after_initial_frame"));
                content.insert(QStringLiteral("overlapValidation"), QStringLiteral("exact_pixels_all_pairs"));
            }
        }
    }
    if (!error.isEmpty()) {
        content.insert(QStringLiteral("reason"), error);
        content.insert(QStringLiteral("staleFileAtPath"), QFile::exists(path));
        if (m_failedRepeat) content.insert(QStringLiteral("repeatIndex"), *m_failedRepeat);
    }
    if (m_endedAtRepeat) content.insert(QStringLiteral("endedAtNoMotionRepeat"), *m_endedAtRepeat);
    return content;
}

} // namespace snaptray::scrollcapture::probe
