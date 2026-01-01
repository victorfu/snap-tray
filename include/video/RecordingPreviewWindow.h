#ifndef RECORDINGPREVIEWWINDOW_H
#define RECORDINGPREVIEWWINDOW_H

#include "video/IVideoPlayer.h"
#include "video/FormatSelectionWidget.h"
#include <QWidget>

class VideoPlaybackWidget;
class TrimTimeline;
class VideoTrimmer;
class VideoAnnotationEditor;
class PreviewIconButton;
class PreviewPillButton;
class PreviewVolumeButton;
class QPaintEvent;
class QSlider;
class QLabel;
class QComboBox;
class QProgressDialog;
class QStackedWidget;

class RecordingPreviewWindow : public QWidget
{
    Q_OBJECT

public:
    enum class Mode { Preview, Annotate };
    Q_ENUM(Mode)

    explicit RecordingPreviewWindow(const QString &videoPath,
                                    QWidget *parent = nullptr);
    ~RecordingPreviewWindow() override;

    QString videoPath() const { return m_videoPath; }

    // Mode switching
    Mode currentMode() const { return m_currentMode; }
    void switchToAnnotateMode();
    void switchToPreviewMode();

signals:
    void saveRequested(const QString &videoPath);
    void discardRequested();
    void closed(bool saved);

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onPlayPauseClicked();
    void onPositionChanged(qint64 positionMs);
    void onDurationChanged(qint64 durationMs);
    void onVideoLoaded();
    void onVideoError(const QString &message);
    void onStateChanged(IVideoPlayer::State state);
    void onSaveClicked();
    void onDiscardClicked();
    void onVolumeChanged(int value);
    void onMuteToggled();

    // Timeline slots
    void onTimelineSeek(qint64 positionMs);
    void onScrubbingStarted();
    void onScrubbingEnded();

    // Trim slots
    void onTrimRangeChanged(qint64 startMs, qint64 endMs);
    void onTrimHandleDoubleClicked(bool isStartHandle);
    void onTrimPreviewToggled(bool enabled);
    void onTrimProgress(int percent);
    void onTrimFinished(bool success, const QString &outputPath);

    // Speed control slots
    void onSpeedChanged(int index);

private:
    void setupUI();
    void updateTimeLabel();
    void updatePlayPauseButton();
    QString formatTime(qint64 ms) const;

    // Keyboard shortcut helpers
    void stepForward();
    void stepBackward();
    void adjustSpeed(int delta);
    void seekRelative(qint64 deltaMs);

    // Format conversion
    QString convertToFormat(FormatSelectionWidget::Format format);

    // Trim helpers
    void showTrimTimeInputDialog(bool isStartHandle);
    void performTrim();

    // Video
    VideoPlaybackWidget *m_videoWidget;
    QString m_videoPath;

    // Timeline with trim
    TrimTimeline *m_timeline;

    // Controls
    PreviewIconButton *m_playPauseBtn;
    QLabel *m_timeLabel;
    PreviewVolumeButton *m_muteBtn;
    QSlider *m_volumeSlider;
    QComboBox *m_speedCombo;

    // Format selection
    FormatSelectionWidget *m_formatWidget;

    // Action buttons
    PreviewIconButton *m_saveBtn;
    PreviewIconButton *m_discardBtn;

    // Trim controls
    PreviewPillButton *m_trimPreviewToggle;
    VideoTrimmer *m_trimmer;
    QProgressDialog *m_trimProgressDialog;

    // State
    bool m_saved;
    bool m_wasPlayingBeforeScrub;
    bool m_trimPreviewEnabled;
    qint64 m_duration;

    // Mode switching
    Mode m_currentMode = Mode::Preview;
    QStackedWidget *m_stackedWidget = nullptr;
    QWidget *m_previewModeWidget = nullptr;
    VideoAnnotationEditor *m_annotationEditor = nullptr;
    PreviewPillButton *m_annotateBtn = nullptr;
    PreviewPillButton *m_doneEditingBtn = nullptr;

    // Speed options
    static constexpr float kSpeedOptions[] = {0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f};
    static constexpr int kDefaultSpeedIndex = 3;  // 1.0x
};

#endif // RECORDINGPREVIEWWINDOW_H
