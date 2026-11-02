#pragma once

#include "recording/PreviewEditHistory.h"
#include "recording/CursorEffects.h"
#include "encoding/EncoderFactory.h"
#include "utils/VideoCropGeometry.h"
#include "recording/WindowTimeline.h"
#include <QObject>
#include <QString>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QVariantList>

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
    // No trustworthy total for this phase (finishing or saving the file):
    // show an indeterminate indicator instead of a percentage.
    Q_PROPERTY(bool processIndeterminate READ processIndeterminate NOTIFY processProgressChanged)
    // The window was closed during an export: ask whether to keep exporting.
    Q_PROPERTY(bool exportCloseQuestionPending READ exportCloseQuestionPending NOTIFY exportCloseQuestionChanged)
    // The last export or save failed; Save can be tried again.
    Q_PROPERTY(bool canRetrySave READ canRetrySave NOTIFY errorMessageChanged)

    // Error state
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

    // Crop aspect preset: 0 free, 1 original, 2 16:9, 3 4:3, 4 1:1, 5 9:16.
    Q_PROPERTY(int cropAspectIndex READ cropAspectIndex WRITE setCropAspectIndex NOTIFY cropAspectChanged)
    // Width / height of the locked aspect in video pixels; 0 when free.
    Q_PROPERTY(qreal cropAspectRatio READ cropAspectRatio NOTIFY cropAspectChanged)

    // Session undo/redo over applied crop and trim edits.
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY editHistoryChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY editHistoryChanged)
    Q_PROPERTY(bool hasEdits READ hasEdits NOTIFY editHistoryChanged)

    // Pointer effects (#160), drawn the same way by the preview and exports.
    Q_PROPERTY(bool hasPointerData READ hasPointerData NOTIFY cursorEffectsChanged)
    Q_PROPERTY(bool cursorEffectsAvailable READ cursorEffectsAvailable NOTIFY cursorEffectsChanged)
    Q_PROPERTY(bool clickEffectsAvailable READ clickEffectsAvailable NOTIFY cursorEffectsChanged)
    Q_PROPERTY(bool cursorEffectsActive READ cursorEffectsActive NOTIFY cursorEffectsChanged)
    Q_PROPERTY(bool showCursor READ showCursor WRITE setShowCursor NOTIFY cursorEffectsChanged)
    Q_PROPERTY(qreal cursorScale READ cursorScale WRITE setCursorScale NOTIFY cursorEffectsChanged)
    Q_PROPERTY(int cursorSmoothing READ cursorSmoothing WRITE setCursorSmoothing NOTIFY cursorEffectsChanged)
    Q_PROPERTY(bool clickRipple READ clickRipple WRITE setClickRipple NOTIFY cursorEffectsChanged)

    // Zoom and pan (#161).
    Q_PROPERTY(bool zoomAvailable READ zoomAvailable NOTIFY zoomChanged)
    Q_PROPERTY(bool autoZoomAvailable READ autoZoomAvailable NOTIFY zoomChanged)
    Q_PROPERTY(bool zoomEnabled READ zoomEnabled WRITE setZoomEnabled NOTIFY zoomChanged)
    Q_PROPERTY(bool zoomReduceMotion READ zoomReduceMotion WRITE setZoomReduceMotion NOTIFY zoomChanged)
    Q_PROPERTY(QVariantList zoomSegments READ zoomSegments NOTIFY zoomChanged)
    Q_PROPERTY(int selectedZoom READ selectedZoom WRITE setSelectedZoom NOTIFY selectedZoomChanged)

    // Masks (#162).
    Q_PROPERTY(bool masksAvailable READ masksAvailable NOTIFY masksChanged)
    Q_PROPERTY(QVariantList masks READ masks NOTIFY masksChanged)
    Q_PROPERTY(int selectedMask READ selectedMask WRITE setSelectedMask NOTIFY selectedMaskChanged)

    // True while the "discard this recording?" confirmation is shown.
    Q_PROPERTY(bool discardConfirmationPending READ discardConfirmationPending NOTIFY discardConfirmationChanged)

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

    // What happened to a file handed over with saveRequested.
    enum class SaveOutcome { Saved, Cancelled, Failed };
    Q_ENUM(SaveOutcome)
    // The owner of saveRequested reports the result. Until then the preview
    // stays open and keeps the source recording. Saved removes the source
    // (unless it is the saved file) and closes; Cancelled and Failed remove
    // only the derived output and keep editing, so Save can be retried.
    // `savedPath`: where it was saved, when known; the source is never
    // removed when it is that file (the user saved over the recording).
    void completeSave(SaveOutcome outcome, const QString &savedPath = QString());
    bool processIndeterminate() const { return m_processIndeterminate; }
    bool exportCloseQuestionPending() const { return m_exportCloseQuestionPending; }
    bool canRetrySave() const { return m_canRetrySave && !m_errorMessage.isEmpty(); }
    // Answers to the close-during-export question.
    Q_INVOKABLE void keepExporting();
    Q_INVOKABLE void cancelExportFromClose();
    // Stops a running MP4 export; the source is kept and no error is shown.
    Q_INVOKABLE void cancelExport();
    // Deletes the recording immediately. User-facing paths (trash button, Esc,
    // title-bar close) go through requestDiscard() so they ask first.
    Q_INVOKABLE void discard();
    Q_INVOKABLE void requestDiscard();
    Q_INVOKABLE void cancelDiscard();
    bool discardConfirmationPending() const { return m_discardConfirmationPending; }
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

    // Aspect presets and exact entry.
    int cropAspectIndex() const { return m_cropAspectIndex; }
    void setCropAspectIndex(int index);
    qreal cropAspectRatio() const;
    // The legal (even, minimum-size, aspect-locked) crop for a view rect, in
    // video pixels; the full frame when it would cover it.
    Q_INVOKABLE QRect legalCropForView(const QRectF &viewRect, const QRectF &contentRect) const;
    // A view rect for exact video-pixel input, after legalizing it.
    Q_INVOKABLE QRectF legalViewRectForVideo(int x, int y, int width, int height,
                                             const QRectF &contentRect) const;
    // The draft re-fitted to the current aspect preset (the whole frame when empty).
    Q_INVOKABLE QRectF fitViewRectToAspect(const QRectF &viewRect, const QRectF &contentRect) const;

    // Trim entry: "m:ss.mmm" text and parsing ("ss", "ss.mmm", "m:ss",
    // "m:ss.mmm", "h:mm:ss.mmm"); parseTime() returns -1 when invalid.
    Q_INVOKABLE QString formatPreciseTime(qint64 ms) const;
    Q_INVOKABLE qint64 parseTime(const QString &text) const;
    // Applies a typed trim range; false (and nothing changes) when invalid.
    Q_INVOKABLE bool setTrimFromInput(qint64 startMs, qint64 endMs);
    // Records the trim after a handle drag as one undo step.
    Q_INVOKABLE void commitTrimEdit();

    bool canUndo() const { return m_editHistory.canUndo(); }
    bool canRedo() const { return m_editHistory.canRedo(); }
    bool hasEdits() const { return !currentEditState().isUnedited(); }
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    // Back to the unedited recording, as one undoable step.
    Q_INVOKABLE void resetEdits();

    // A usable pointer sidecar exists for this recording.
    bool hasPointerData() const;
    // The options can be used with the selected format on this platform.
    bool cursorEffectsAvailable() const;
    bool clickEffectsAvailable() const;
    // Effects will be drawn into the export (and so are shown in the preview).
    bool cursorEffectsActive() const;
    bool showCursor() const { return m_cursorEffects.showCursor; }
    void setShowCursor(bool value);
    qreal cursorScale() const { return m_cursorEffects.scale; }
    void setCursorScale(qreal value);
    int cursorSmoothing() const { return int(m_cursorEffects.smoothing); }
    void setCursorSmoothing(int value);
    bool clickRipple() const { return m_cursorEffects.clickRipple; }
    void setClickRipple(bool value);
    const SnapTray::CursorTimeline* cursorTimeline() const { return m_cursorTimeline.get(); }

    // Zoom: drawn by the same paths as the pointer effects.
    bool zoomAvailable() const;
    bool autoZoomAvailable() const;
    bool zoomEnabled() const { return m_zoom.enabled; }
    void setZoomEnabled(bool value);
    bool zoomReduceMotion() const { return m_zoom.reduceMotion; }
    void setZoomReduceMotion(bool value);
    QVariantList zoomSegments() const;
    int selectedZoom() const { return m_selectedZoom; }
    void setSelectedZoom(int index);
    const SnapTray::ZoomTrack& zoomTrack() const { return m_zoom; }
    // Replaces the automatic segments with new ones from the pointer
    // activity; segments the user added or changed are kept. Returns how
    // many were generated (0 without clicks: nothing is invented).
    Q_INVOKABLE int generateAutoZoom();
    // A 2x, two-second zoom at the playhead, centred on the pointer when
    // known; returns its index (or -1).
    Q_INVOKABLE int addZoomAt(qint64 positionMs);
    Q_INVOKABLE void removeZoom(int index);
    Q_INVOKABLE void setZoomScale(int index, qreal scale);
    Q_INVOKABLE bool setZoomRange(int index, qint64 startMs, qint64 endMs);
    Q_INVOKABLE void setZoomFocusFromView(int index, const QPointF& viewPoint, const QRectF& contentRect);
    // The part of the picture shown at positionMs, in view coordinates.
    Q_INVOKABLE QRectF zoomViewportInView(qint64 positionMs, const QRectF& contentRect) const;
    // Masks: burned into every export, never only shown in the preview.
    bool masksAvailable() const { return formatCanDrawEffects(); }
    QVariantList masks() const;
    int selectedMask() const { return m_selectedMask; }
    void setSelectedMask(int index);
    const SnapTray::RedactionTrack& redaction() const { return m_redaction; }
    // A solid mask over the middle of the picture for the whole recording;
    // returns its index (or -1 when masks cannot be exported).
    Q_INVOKABLE int addMask();
    Q_INVOKABLE void removeMask(int index);
    Q_INVOKABLE void setMaskRectFromView(int index, const QRectF& viewRect, const QRectF& contentRect);
    Q_INVOKABLE QRectF maskRectInView(int index, const QRectF& contentRect) const;
    Q_INVOKABLE void setMaskStyle(int index, int style);
    // endMs -1 = to the end; false (nothing changes) when invalid.
    Q_INVOKABLE bool setMaskRange(int index, qint64 startMs, qint64 endMs);

    // Anything drawn into the export: masks, pointer effects or zoom.
    bool effectsActive() const;
    SnapTray::CursorEffectSettings cursorEffects() const { return m_cursorEffects; }

    // Called by QML VideoPlaybackItem position/duration updates
    Q_INVOKABLE void updatePosition(qint64 ms);
    Q_INVOKABLE void updateDuration(qint64 ms);
    Q_INVOKABLE void updatePlayingState(bool playing);

signals:
    void masksChanged();
    void selectedMaskChanged();
    void windowTimelineChanged();
    void cursorEffectsChanged();
    void zoomChanged();
    void selectedZoomChanged();
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
    void discardConfirmationChanged();
    void cropAspectChanged();
    void exportCloseQuestionChanged();
    void editHistoryChanged();
    void cropRectChanged();
    void videoSizeChanged();

private:
    friend class tst_RecordingPreviewExport;
    friend class tst_RecordingPreviewCrop;
    friend class tst_RecordingPreviewDiscard;
    friend class tst_RecordingPreviewZoom;
    friend class tst_RecordingPreviewMasks;

    // Test seam: when set, replaces IVideoTranscoder::create() for MP4 edit
    // exports. Empty (the default, and always in production) = native factory.
    using TranscoderFactory = std::function<std::unique_ptr<IVideoTranscoder>()>;
    static TranscoderFactory& transcoderFactoryOverride();
    // Test seam for the user's output quality; empty means read
    // RecordingSettingsManager at save time.
    static std::function<int()>& outputQualityOverride();
    // Test seam for free space next to the recording; empty means QStorageInfo.
    static std::function<qint64(const QString&)>& freeBytesForPathOverride();

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
    std::shared_ptr<const SnapTray::CursorTimeline> m_cursorTimeline;
    SnapTray::CursorEffectSettings m_cursorEffects;
    mutable std::optional<bool> m_mp4FrameFilterSupported;
    void setCursorEffects(const SnapTray::CursorEffectSettings& settings);
    // Draws the current zoom and pointer effects on an output frame
    // (already cropped); a snapshot of the settings.
    std::function<void(QImage&, qint64)> effectsFrameFilter() const;
    SnapTray::ZoomTrack m_zoom;
    int m_selectedZoom = -1;
    SnapTray::RedactionTrack m_redaction;
    int m_selectedMask = -1;
    void editRedaction(const std::function<void(SnapTray::RedactionTrack&)>& change);
    void installPreviewFrameProcessor();
    QRectF zoomBounds() const;
    // The selected format's export can draw onto frames on this platform.
    bool formatCanDrawEffects() const;
    void editZoom(const std::function<void(SnapTray::ZoomTrack&)>& change);
    void editCursorEffects(const std::function<void(SnapTray::CursorEffectSettings&)>& change);
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
    bool m_discardConfirmationPending = false;
    // Save hand-off: the output waiting for completeSave().
    bool m_awaitingSave = false;
    QString m_pendingSaveOutput;
    bool m_processIndeterminate = false;
    bool m_exportCloseQuestionPending = false;
    bool m_canRetrySave = false;
    void handOffForSave(const QString &outputPath, const QSize &croppedSize);
    void setProcessPhase(const QString &status, bool indeterminate);
    int m_cropAspectIndex = 0;
    SnapTray::PreviewEditHistory m_editHistory;
    SnapTray::PreviewEditState currentEditState() const;
    void applyEditState(const SnapTray::PreviewEditState &state);
    void recordEdit();
    // The locked aspect as integers; {0, 0} when free.
    QSize aspectUnits() const;
    // Set when SnapTray itself closes the window (save, confirmed discard).
    bool m_closeApproved = false;
    bool m_applicationQuitting = false;
    QString m_cursorSurfaceId;
    QString m_cursorOwnerId;
};
