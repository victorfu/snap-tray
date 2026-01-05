#ifndef VIDEO_ANNOTATION_EDITOR_H
#define VIDEO_ANNOTATION_EDITOR_H

#include "video/AnnotationTrack.h"
#include "video/VideoAnnotation.h"
#include "video/VideoAnnotationRenderer.h"
#include <QWidget>
#include <QVector>
#include <functional>

class VideoPlaybackWidget;
class TrimTimeline;
class AnnotationTimelineWidget;
class ColorAndWidthWidget;
class IVideoPlayer;
class QAbstractButton;
class QToolButton;
class QPushButton;
class QSlider;
class AnnotationCanvas;
class QPainter;
class InlineTextEditor;

class VideoAnnotationEditor : public QWidget {
    Q_OBJECT

public:
    explicit VideoAnnotationEditor(QWidget *parent = nullptr);
    ~VideoAnnotationEditor() override;

    // Video source
    void setVideoPlayer(IVideoPlayer *player);
    void setVideoPath(const QString &path);
    QString videoPath() const;

    // Track management
    AnnotationTrack *track() const;
    void setTrack(AnnotationTrack *track);

    // Tool selection
    enum class Tool {
        Select,
        Arrow,
        Line,
        Rectangle,
        Ellipse,
        Pencil,
        Marker,
        Text,
        StepBadge,
        Blur,
        Highlight
    };
    Q_ENUM(Tool)

    void setCurrentTool(Tool tool);
    Tool currentTool() const;

    // Settings
    void setAnnotationColor(const QColor &color);
    QColor annotationColor() const;

    void setAnnotationLineWidth(int width);
    int annotationLineWidth() const;

    void setDefaultDuration(qint64 durationMs);
    qint64 defaultDuration() const;

    // Playback control
    void play();
    void pause();
    void seek(qint64 timeMs);
    bool isPlaying() const;
    qint64 currentTime() const;
    qint64 duration() const;

    // Export
    bool exportWithAnnotations(const QString &outputPath, int format,
                               std::function<void(int)> progressCallback = nullptr);

    // Project file save/load
    bool saveProject(const QString &projectPath);
    bool loadProject(const QString &projectPath);

signals:
    void toolChanged(Tool tool);
    void colorChanged(const QColor &color);
    void lineWidthChanged(int width);
    void playbackStateChanged(bool isPlaying);
    void positionChanged(qint64 timeMs);
    void durationChanged(qint64 durationMs);
    void modifiedChanged(bool modified);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onVideoPositionChanged(qint64 position);
    void onVideoDurationChanged(qint64 duration);
    void onTimelineSeekRequested(qint64 timeMs);
    void onAnnotationTimingChanged(const QString &id, qint64 startMs, qint64 endMs);
    void onUndoStackIndexChanged();
    void onTextEditingFinished(const QString &text, const QPoint &position);
    void onTextEditingCancelled();

private:
    friend class AnnotationCanvas;
    void setupUI();
    void applyTheme();
    void renderAnnotations(QPainter &painter, const QRect &targetRect);
    void handleCanvasMousePress(const QPoint &pos);
    void handleCanvasMouseMove(const QPoint &pos);
    void handleCanvasMouseRelease(const QPoint &pos);
    void updateLayout();
    void updateVideoRect();
    void updateAnnotationDisplay();

    // Drawing helpers
    QPointF widgetToRelative(const QPoint &widgetPos) const;
    QPoint relativeToWidget(const QPointF &relativePos) const;
    void beginAnnotation(const QPoint &pos);
    void updateAnnotation(const QPoint &pos);
    void finishAnnotation();
    void cancelAnnotation();

    // Text editing helpers
    void startTextEditing(const QPoint &pos);
    bool handleTextEditorPress(const QPoint &pos);

    // Components
    VideoPlaybackWidget *m_videoWidget = nullptr;
    TrimTimeline *m_trimTimeline = nullptr;
    AnnotationTimelineWidget *m_annotationTimeline = nullptr;
    ColorAndWidthWidget *m_colorWidget = nullptr;
    QWidget *m_toolbarWidget = nullptr;
    QWidget *m_videoContainer = nullptr;
    AnnotationCanvas *m_annotationCanvas = nullptr;
    InlineTextEditor *m_textEditor = nullptr;
    QWidget *m_controlsWidget = nullptr;

    QVector<QAbstractButton *> m_toolButtons;
    QToolButton *m_colorButton = nullptr;
    QSlider *m_widthSlider = nullptr;
    QAbstractButton *m_undoButton = nullptr;
    QAbstractButton *m_redoButton = nullptr;
    QPushButton *m_playButton = nullptr;

    // State
    IVideoPlayer *m_videoPlayer = nullptr;
    QString m_videoPath;
    AnnotationTrack *m_track = nullptr;
    bool m_ownsTrack = false;
    VideoAnnotationRenderer m_renderer;

    Tool m_currentTool = Tool::Select;
    QColor m_annotationColor = Qt::red;
    int m_annotationLineWidth = 3;
    qint64 m_defaultDurationMs = 3000; // 3 seconds default

    // Drawing state
    bool m_isDrawing = false;
    VideoAnnotation m_currentAnnotation;
    QList<QPointF> m_currentPath;

    // Video display
    QRect m_videoRect;
    qint64 m_currentTimeMs = 0;
    qint64 m_durationMs = 0;

    // Step badge counter
    int m_nextStepNumber = 1;
};

#endif // VIDEO_ANNOTATION_EDITOR_H
