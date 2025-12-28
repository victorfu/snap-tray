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

class RecordingControlBar : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief Annotation tools available during recording.
     */
    enum class AnnotationTool {
        None = 0,
        Pencil,
        Marker,
        Arrow,
        Rectangle
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

    // Annotation support
    void setAnnotationEnabled(bool enabled);
    bool isAnnotationEnabled() const { return m_annotationEnabled; }
    void setCurrentTool(AnnotationTool tool);
    AnnotationTool currentTool() const { return m_currentTool; }
    void setAnnotationColor(const QColor &color);
    QColor annotationColor() const { return m_annotationColor; }
    void setAnnotationWidth(int width);
    int annotationWidth() const { return m_annotationWidth; }

signals:
    void stopRequested();
    void cancelRequested();
    void pauseRequested();
    void resumeRequested();

    // Annotation signals
    void toolChanged(AnnotationTool tool);
    void colorChangeRequested();
    void widthChangeRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    enum ButtonId {
        ButtonNone = -1,
        ButtonPause = 0,
        ButtonStop = 1,
        ButtonCancel = 2,
        // Annotation tool buttons
        ButtonPencil = 10,
        ButtonMarker = 11,
        ButtonArrow = 12,
        ButtonRectangle = 13,
        ButtonColor = 20
    };

    void setupUi();
    QString formatDuration(qint64 ms) const;
    void updateIndicatorGradient();
    void updateButtonRects();
    int buttonAtPosition(const QPoint &pos) const;
    void drawButtons(QPainter &painter);
    void drawAnnotationButtons(QPainter &painter);
    void drawTooltip(QPainter &painter);
    QString tooltipForButton(int button) const;

    // Info labels
    QLabel *m_recordingIndicator;
    QLabel *m_audioIndicator;
    QLabel *m_durationLabel;
    QLabel *m_sizeLabel;
    QLabel *m_fpsLabel;
    bool m_audioEnabled;

    // Button rectangles (recording controls)
    QRect m_pauseRect;
    QRect m_stopRect;
    QRect m_cancelRect;
    int m_hoveredButton;

    // Annotation tool button rectangles
    QRect m_pencilRect;
    QRect m_markerRect;
    QRect m_arrowRect;
    QRect m_rectangleRect;
    QRect m_colorRect;

    // Annotation state
    bool m_annotationEnabled;
    AnnotationTool m_currentTool;
    QColor m_annotationColor;
    int m_annotationWidth;

    // Drag support
    QPoint m_dragStartPos;
    QPoint m_dragStartWidgetPos;
    bool m_isDragging;

    // Animation timer for indicator gradient
    QTimer *m_blinkTimer;
    qreal m_gradientOffset;
    bool m_indicatorVisible;
    bool m_isPaused;
    bool m_isPreparing;

    // Global shortcut for ESC key
    QShortcut *m_escShortcut;

    // Layout constants (matching ToolbarWidget)
    static const int TOOLBAR_HEIGHT = 32;
    static const int BUTTON_WIDTH = 28;
    static const int BUTTON_SPACING = 2;
    static const int SEPARATOR_MARGIN = 8;
};

#endif // RECORDINGCONTROLBAR_H
