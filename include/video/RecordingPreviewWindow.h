#ifndef RECORDINGPREVIEWWINDOW_H
#define RECORDINGPREVIEWWINDOW_H

#include "video/IVideoPlayer.h"
#include "video/FormatSelectionWidget.h"
#include <QWidget>

class VideoPlaybackWidget;
class VideoTimeline;
class QPushButton;
class QSlider;
class QLabel;
class QComboBox;
class QProgressDialog;

class RecordingPreviewWindow : public QWidget
{
    Q_OBJECT

public:
    explicit RecordingPreviewWindow(const QString &videoPath,
                                    QWidget *parent = nullptr);
    ~RecordingPreviewWindow() override;

    QString videoPath() const { return m_videoPath; }

signals:
    void saveRequested(const QString &videoPath);
    void discardRequested();
    void closed(bool saved);

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

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

    // Video
    VideoPlaybackWidget *m_videoWidget;
    QString m_videoPath;

    // Timeline
    VideoTimeline *m_timeline;

    // Controls
    QPushButton *m_playPauseBtn;
    QLabel *m_timeLabel;
    QPushButton *m_muteBtn;
    QSlider *m_volumeSlider;
    QComboBox *m_speedCombo;

    // Format selection
    FormatSelectionWidget *m_formatWidget;

    // Action buttons
    QPushButton *m_saveBtn;
    QPushButton *m_discardBtn;

    // State
    bool m_saved;
    bool m_wasPlayingBeforeScrub;
    qint64 m_duration;

    // Speed options
    static constexpr float kSpeedOptions[] = {0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f};
    static constexpr int kDefaultSpeedIndex = 3;  // 1.0x
};

#endif // RECORDINGPREVIEWWINDOW_H
