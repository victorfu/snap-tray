#include "history/HistoryReplayDocument.h"

#include "history/AnnotationSerializer.h"
#include "utils/CoordinateHelper.h"

#include <QFile>

namespace SnapTray {

std::optional<HistoryReplayDocument> loadHistoryReplayDocument(const HistoryEntry& entry,
                                                               QString* errorMessage)
{
    const auto fail = [errorMessage](const QString& message) {
        if (errorMessage) {
            *errorMessage = message;
        }
        return std::nullopt;
    };

    if (!entry.replayAvailable) {
        return fail(QStringLiteral("entry is not replayable"));
    }
    if (entry.canvasPath.isEmpty()) {
        return fail(QStringLiteral("entry has no canvas"));
    }

    HistoryReplayDocument document;
    document.devicePixelRatio = entry.devicePixelRatio > 0.0 ? entry.devicePixelRatio : 1.0;
    document.canvas = QPixmap(entry.canvasPath);
    if (document.canvas.isNull()) {
        return fail(QStringLiteral("canvas image cannot be read"));
    }
    if (entry.devicePixelRatio > 0.0) {
        document.canvas.setDevicePixelRatio(entry.devicePixelRatio);
    }

    document.canvasLogicalSize = entry.canvasLogicalSize.isValid()
        ? entry.canvasLogicalSize
        : CoordinateHelper::toLogical(document.canvas.size(), document.devicePixelRatio);
    if (document.canvasLogicalSize.isEmpty()) {
        return fail(QStringLiteral("canvas has no size"));
    }
    if (entry.captureRegions.isEmpty() &&
        (!entry.selectionRect.isValid() || entry.selectionRect.isEmpty())) {
        return fail(QStringLiteral("entry has no selection"));
    }

    if (entry.annotationsPath.isEmpty()) {
        return fail(QStringLiteral("entry has no annotations file"));
    }
    QFile annotationsFile(entry.annotationsPath);
    if (!annotationsFile.open(QIODevice::ReadOnly)) {
        return fail(QStringLiteral("annotations file cannot be read: %1")
                        .arg(annotationsFile.errorString()));
    }

    document.sourcePixmap = std::make_shared<const QPixmap>(document.canvas);
    QString decodeError;
    auto annotations = decodeAnnotationItems(
        annotationsFile.readAll(), document.sourcePixmap, &decodeError);
    if (!annotations) {
        return fail(QStringLiteral("annotations cannot be decoded: %1").arg(decodeError));
    }
    document.annotations = std::move(*annotations);
    return document;
}

} // namespace SnapTray
