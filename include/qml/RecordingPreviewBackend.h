#pragma once

#include "encoding/EncoderFactory.h"
#include "utils/VideoCropGeometry.h"
#include "recording/WindowTimeline.h"
#include <QObject>
#include <QString>
#include <QRect>
#include <QRectF>
#include <QSize>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

class QQuickView;
class QEvent;
class IVideoTranscoder;
class tst_RecordingPreviewExport;

/**
 * @brief C++ backend for the QML Recording Preview window.
 *
 * Manages the QQuickView lifecycle, exposes video/trim/format state
 * to QML, and handles save/discard/trim/format-conversion logic.
 *
 * The signal interface preserves the same flow as the old preview window:
 * notify caller to save/discard, then report closed(saved).
 *
 * Usage:
 *   auto* backend = new RecordingPreviewBackend(videoPath, this);
 *   backend->setDefaultOutputFormat(OutputFormat::MP4);
 *   connect(backend, &RecordingPreviewBackend::saveRequested, ...);
 *   connect(backend, &RecordingPreviewBackend::discardRequested, ...);
 *   connect(backend, &RecordingPreviewBackend::closed, ...);
 *   backend->show();
 */
class RecordingPreviewBackend : public QObject
{
    Q_OBJECT

    // Video state
    Q_PROPERTY(QString videoPath READ videoPath CONSTANT)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY stateChanged)

    // Trim state
    Q_PROPERTY(qint64 trimStart READ trimStart WRITE setTrimStart NOTIFY trimRangeChanged)
    Q_PROPERTY(qint64 trimEnd READ trimEnd WRITE setTrimEnd NOTIFY trimRangeChanged)
    Q_PROPERTY(bool hasTrim READ hasTrim NOTIFY trimRangeChanged)
    Q_PROPERTY(qint64 trimmedDuration READ trimmedDuration NOTIFY trimRangeChanged)

    // Crop (video pixels; empty = no crop)
    Q_PROPERTY(QRect cropRect READ cropRect NOTIFY cropRectChanged)
    Q_PROPERTY(bool hasCrop READ hasCrop NOTIFY cropRectChanged)
    Q_PROPERTY(QSize videoSize READ videoSize NOTIFY videoSizeChanged)
    // Smallest side a committed crop can have, so the editor never offers less.
    Q_PROPERTY(int minCropSide READ minCropSide CONSTANT)
    // Where the top-level windows were while recording (from the sidecar), for snapping.
    Q_PROPERTY(bool hasWindowTimeline READ hasWindowTimeline NOTIFY windowTimelineChanged)

    // Format
    Q_PROPERTY(int selectedFormat READ selectedFormat WRITE setSelectedFormat NOTIFY formatChanged)

    // Processing progress
    Q_PROPERTY(bool isProcessing READ isProcessing NOTIFY processingChanged)
    Q_PROPERTY(bool canCancelExport READ canCancelExport NOTIFY processingChanged)
    Q_PROPERTY(int processProgress READ processProgress NOTIFY processProgressChanged)
    Q_PROPERTY(QString processStatus READ processStatus NOTIFY processStatusChanged)

    // Error state
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    enum OutputFormat {
        MP4  = 0,
        GIF  = 1,
        WebP = 2
    };
    Q_ENUM(OutputFormat)

    // A recording made at the selected quality (not as a high-quality
    // intermediate) is saved as recorded when unedited; this overload treats
    // the recording as an intermediate.
    explicit RecordingPreviewBackend(const QString &videoPath, QObject *parent = nullptr);
    RecordingPreviewBackend(const QString &videoPath, bool recordedAsIntermediate, QObject *parent = nullptr);
    ~RecordingPreviewBackend() override;

    // Window management
    void show();
    void close();
    void setDefaultOutputFormat(int formatInt);

    // Property getters
    QString videoPath() const { return m_videoPath; }
    qint64 duration() const { return m_duration; }
    qint64 position() const { return m_position; }
    bool isPlaying() const { return m_isPlaying; }

    qint64 trimStart() const { return m_trimStart; }
    qint64 trimEnd() const;
    bool hasTrim() const;
    qint64 trimmedDuration() const;

    QRect cropRect() const { return m_cropRect; }
    bool hasCrop() const { return !m_cropRect.isEmpty(); }
    QSize videoSize() const { return m_videoSize; }
    int minCropSide() const { return SnapTray::VideoCropGeometry::kMinCropSide; }

    int selectedFormat() const { return m_selectedFormat; }

    bool isProcessing() const { return m_isProcessing; }
    bool canCancelExport() const;
    int processProgress() const { return m_processProgress; }
    QString processStatus() const { return m_processStatus; }

    QString errorMessage() const { return m_errorMessage; }

    // Property setters
    void setTrimStart(qint64 ms);
    void setTrimEnd(qint64 ms);
    void setSelectedFormat(int format);

    // QML actions
    Q_INVOKABLE void save();
    // Stops a running MP4 export; the source is kept and no error is shown.
    Q_INVOKABLE void cancelExport();
    Q_INVOKABLE void discard();
    Q_INVOKABLE void toggleTrim();
    Q_INVOKABLE QString formatTime(qint64 ms) const;
    Q_INVOKABLE void clearError();
    Q_INVOKABLE void reportPlaybackError(const QString &message);

    Q_INVOKABLE void updateVideoSize(const QSize &size);
    Q_INVOKABLE void setCropRect(const QRect &videoRect);
    Q_INVOKABLE void setCropFromView(const QRectF &viewRect, const QRectF &contentRect);
    Q_INVOKABLE QRectF cropRectInView(const QRectF &contentRect) const;
    bool hasWindowTimeline() const { return m_windowTimeline.has_value(); }
    // Window under `viewPoint` at `positionMs`, in view coordinates; empty when none.
    Q_INVOKABLE QRectF windowRectInViewAt(const QPointF &viewPoint, const QRectF &contentRect, qint64 positionMs) const;
    Q_INVOKABLE QString windowAppAt(const QPointF &viewPoint, const QRectF &contentRect, qint64 positionMs) const;
    Q_INVOKABLE void clearCrop();

    // Called by QML VideoPlaybackItem position/duration updates
    Q_INVOKABLE void updatePosition(qint64 ms);
    Q_INVOKABLE void updateDuration(qint64 ms);
    Q_INVOKABLE void updatePlayingState(bool playing);

signals:
    void windowTimelineChanged();
    // External interface consumed by MainApplication. `outputSize` is the
    // pixel size of a cropped export (for the filename's {w}x{h}); it is
    // empty when the output is not cropped.
    void saveRequested(const QString &videoPath, const QSize &outputSize);
    void discardRequested(const QString &videoPath);
    void closed(bool saved);

    // Property change notifications
    void durationChanged();
    void positionChanged();
    void stateChanged();
    void trimRangeChanged();
    void formatChanged();
    void processingChanged();
    void processProgressChanged();
    void processStatusChanged();
    void errorMessageChanged();
    void cropRectChanged();
    void videoSizeChanged();

private:
    friend class tst_RecordingPreviewExport;
    friend class tst_RecordingPreviewCrop;

    // Test seam: when set, replaces IVideoTranscoder::create() for MP4 edit
    // exports. Empty (the default, and always in production) = native factory.
    using TranscoderFactory = std::function<std::unique_ptr<IVideoTranscoder>()>;
    static TranscoderFactory& transcoderFactoryOverride();
    // Test seam for the user's output quality; empty means read
    // RecordingSettingsManager at save time.
    static std::function<int()>& outputQualityOverride();

    void finishClose();
    bool eventFilter(QObject* watched, QEvent* event) override;

    void ensureView();
    void applyPlatformWindowFlags();
    void syncCursorSurface();
    void performFormatConversion(OutputFormat format);
    // smartSave: no edits -- move the file if it already meets the user's
    // target bitrate, otherwise re-encode the full range at that bitrate.
    void performTranscode(bool smartSave);
    void finishProcessing();

    void setErrorMessage(const QString &msg);

    // View
    QQuickView *m_view = nullptr;

    // Video
    QString m_videoPath;
    int m_outputQuality = -1; // snapshot taken when save() begins
    // False when the recording was captured at the selected quality, so an
    // unedited save never needs a re-encode.
    bool m_recordedAsIntermediate = true;
    qint64 m_duration = 0;
    qint64 m_position = 0;
    bool m_isPlaying = false;

    // Trim
    qint64 m_trimStart = 0;
    qint64 m_trimEnd = -1;  // -1 means end of video

    // Crop
    QRect m_cropRect;
    QSize m_videoSize;
    std::optional<SnapTray::WindowTimeline> m_windowTimeline;
    std::optional<SnapTray::WindowSample> windowAt(const QPointF &viewPoint, const QRectF &contentRect,
                                                   qint64 positionMs) const;

    // Format
    OutputFormat m_selectedFormat = MP4;

    // Processing
    enum class ExportKind { None, MP4, Animated };
    ExportKind m_exportKind = ExportKind::None;
    bool m_isProcessing = false;
    // Shared with the MP4 export worker; set by cancelExport() or on
    // destruction to cancel it. Each export starts with a fresh token.
    std::shared_ptr<std::atomic_bool> m_exportCancelToken = std::make_shared<std::atomic_bool>(false);
    int m_processProgress = 0;
    QString m_processStatus;

    // Error
    QString m_errorMessage;

    // State
    bool m_saved = false;
    bool m_closeHandled = false;
    QString m_cursorSurfaceId;
    QString m_cursorOwnerId;
};
