#ifndef RECORDINGCONTROLBAR_H
#define RECORDINGCONTROLBAR_H

#include <QWidget>
#include <QPoint>
#include <QRect>
#include <QTimer>
#include <QShortcut>
#include <QColor>
#include <QVector>

class QLabel;
class QHBoxLayout;
class QFont;
class QFontMetrics;
class QEnterEvent;
class GlassTooltipWidget;
class RecordingControlBar : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief Control bar mode - Recording or Preview
     */
    enum class Mode {
        Recording,   // Recording mode with pause/stop/cancel
        Preview      // Preview mode with play/timeline/save/discard
    };

    /**
     * @brief Output format for preview mode
     */
    enum class OutputFormat {
        MP4 = 0,
        GIF = 1,
        WebP = 2
    };

    explicit RecordingControlBar(QWidget *parent = nullptr);
    ~RecordingControlBar();

    void positionNear(const QRect &recordingRegion);
    void updateDuration(qint64 elapsedMs);
    void setPaused(bool paused);
    void setPreparing(bool preparing);
    void setPreparingStatus(const QString &status);
    void updateRegionSize(int width, int height);
    void updateFps(double fps);
    void setAudioEnabled(bool enabled);

    // Mode support
    Mode mode() const { return m_mode; }

    // Preview mode updates
    void updatePreviewPosition(qint64 positionMs);
    void updatePreviewDuration(qint64 durationMs);
    void setPlaying(bool playing);
    void setVolume(float volume);
    void setMuted(bool muted);
    void setSelectedFormat(OutputFormat format);
    OutputFormat selectedFormat() const { return m_selectedFormat; }

signals:
    // Recording mode signals
    void stopRequested();
    void cancelRequested();
    void pauseRequested();
    void resumeRequested();

    // Preview mode signals
    void playRequested();
    void seekRequested(qint64 positionMs);
    void volumeToggled();
    void formatSelected(OutputFormat format);
    void annotateRequested();
    void savePreviewRequested();
    void discardPreviewRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    enum ButtonId {
        ButtonNone = -1,
        // Recording mode buttons
        ButtonPause = 0,
        ButtonStop = 1,
        ButtonCancel = 2,
        // Preview mode buttons
        ButtonPlayPause = 40,
        ButtonVolume = 41,
        ButtonFormatMP4 = 42,
        ButtonFormatGIF = 43,
        ButtonFormatWebP = 44,
        ButtonAnnotate = 45,
        ButtonSave = 46,
        ButtonDiscard = 47,
        // Timeline area (special - not a button but a drag area)
        AreaTimeline = 60
    };

    void setupUi();
    QString formatDuration(qint64 ms) const;
    QString formatPreviewTime(qint64 ms) const;
    void updateIndicatorGradient();
    void updateButtonRects();
    void updatePreviewButtonRects();
    int buttonAtPosition(const QPoint &pos) const;
    void drawButtons(QPainter &painter);
    void updateButtonSpacerWidth();
    void drawPreviewModeUI(QPainter &painter);
    void drawTimeline(QPainter &painter);
    void drawFormatButtons(QPainter &painter);
    QString tooltipForButton(int button) const;
    qint64 positionFromTimelineX(int x) const;
    QRect backgroundRect() const;
    QRect anchorRectForButton(int button) const;
    int previewWidth() const;
    int annotateButtonWidth() const;
    void updateFixedWidth();
    void applyFixedWidth(int targetWidth);
    void showTooltipForButton(int buttonId);
    void showTooltip(const QString &text, const QRect &anchorRect);
    void hideTooltip();

    // Info labels
    QLabel *m_recordingIndicator;
    QLabel *m_audioIndicator;
    QLabel *m_durationLabel;
    QLabel *m_sizeLabel;
    QLabel *m_fpsLabel;
    QLabel *m_separator1;
    QLabel *m_separator2;
    QWidget *m_buttonSpacer;
    QHBoxLayout *m_layout;
    bool m_audioEnabled;

    // Button rectangles (recording controls)
    QRect m_pauseRect;
    QRect m_stopRect;
    QRect m_cancelRect;
    int m_hoveredButton;

    // Visual effects state
    // (Removed)

    // Mode state
    Mode m_mode;

    // Preview mode button rectangles
    QRect m_playPauseRect;
    QRect m_timelineRect;
    QRect m_volumeRect;
    QRect m_formatMP4Rect;
    QRect m_formatGIFRect;
    QRect m_formatWebPRect;
    QRect m_annotateRect;
    QRect m_saveRect;
    QRect m_discardRect;
    QRect m_indicatorRect;
    QRect m_durationRect;

    // Preview mode state
    qint64 m_previewDuration;
    qint64 m_previewPosition;
    bool m_isPlaying;
    float m_volume;
    bool m_isMuted;
    OutputFormat m_selectedFormat;
    bool m_isScrubbing;
    qint64 m_scrubPosition;

    // Drag support
    QPoint m_dragStartPos;
    QPoint m_dragStartWidgetPos;
    bool m_isDragging;

    // Animation timer for indicator gradient
    QTimer *m_blinkTimer;
    qreal m_gradientOffset;
    bool m_isPaused;
    bool m_isPreparing;

    // Global shortcut for ESC key
    QShortcut *m_escShortcut;
    GlassTooltipWidget *m_tooltip;

    // Layout constants (matching ToolbarCore)
    static const int TOOLBAR_HEIGHT = 32;
    static const int BUTTON_WIDTH = 28;
    static const int BUTTON_SPACING = 2;
    static const int SEPARATOR_MARGIN = 8;
    static const int SHADOW_MARGIN = 0;
    static const int SHADOW_MARGIN_X = 0;
};

#endif // RECORDINGCONTROLBAR_H
