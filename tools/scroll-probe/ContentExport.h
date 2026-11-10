#pragma once

#include "scrollcapture/FrameSettleDetector.h"
#include "scrollcapture/RowHashShiftEstimator.h"
#include "scrollcapture/ScrollContentAssembler.h"

#include <QJsonObject>
#include <optional>

namespace snaptray::scrollcapture::probe {

struct ContentRepeat {
    std::optional<ScrollContentPair> pair;
    QString error;
    bool noMotion = false;
};

// Called only after successful capture/injection. Even an unchanged frame must
// pass settle, geometry and position checks before it can end a segment.
ContentRepeat validateContentRepeat(const QImage& before, const QImage& after,
                                    FrameSettleDetector::Outcome settle,
                                    const QString& positionSync, bool sameBounds,
                                    const ShiftEstimate& dominant);

// Checks the parent by opening a temporary sibling, without touching an existing
// destination. Runtime save errors still matter if the filesystem changes later.
QString contentOutputPathError(const QString& path);

struct ContentSaveResult {
    QString stage;
    QString detail;
    bool ok() const { return stage.isEmpty(); }
};
ContentSaveResult saveContentImage(const QImage& image, const QString& path);

class ContentExportSession
{
public:
    explicit ContentExportSession(qint64 maxPixels = kDefaultMaxScrollContentPixels)
        : m_maxPixels(maxPixels) {}
    void add(int repeatIndex, const ContentRepeat& repeat);
    QJsonObject finish(const QString& path, bool interrupted = false) const;
    const QVector<ScrollContentPair>& pairs() const { return m_pairs; }

private:
    qint64 m_maxPixels;
    qint64 m_retainedPixels = 0;
    QVector<ScrollContentPair> m_pairs;
    QString m_error;
    std::optional<int> m_failedRepeat;
    std::optional<int> m_endedAtRepeat;
};

} // namespace snaptray::scrollcapture::probe
