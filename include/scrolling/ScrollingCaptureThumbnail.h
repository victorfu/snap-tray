#ifndef SCROLLINGCAPTURETHUMBNAIL_H
#define SCROLLINGCAPTURETHUMBNAIL_H

#include <QWidget>
#include <QImage>
#include <QRect>
#include <QPoint>

class ScrollingCaptureThumbnail : public QWidget
{
    Q_OBJECT

public:
    enum class MatchStatus {
        Good,       // Green indicator
        Warning,    // Yellow indicator
        Failed      // Red indicator
    };
    Q_ENUM(MatchStatus)

    explicit ScrollingCaptureThumbnail(QWidget *parent = nullptr);
    ~ScrollingCaptureThumbnail();

    void setStitchedImage(const QImage &image);
    void setViewportRect(const QRect &viewportInStitched);
    void setMatchStatus(MatchStatus status, double confidence);
    void positionNear(const QRect &captureRegion);

    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void updateScaledImage();
    void drawViewportIndicator(QPainter &painter);
    void drawMatchStatusIndicator(QPainter &painter);
    void drawBorder(QPainter &painter);

    QImage m_stitchedImage;
    QImage m_scaledImage;
    QRect m_viewportRect;      // In stitched image coordinates
    MatchStatus m_matchStatus = MatchStatus::Good;
    double m_confidence = 1.0;
    double m_scale = 1.0;

    // Drag support
    QPoint m_dragStartPos;
    QPoint m_dragStartWidgetPos;
    bool m_isDragging = false;

    static constexpr int THUMBNAIL_WIDTH = 150;
    static constexpr int THUMBNAIL_MAX_HEIGHT = 400;
    static constexpr int STATUS_INDICATOR_SIZE = 12;
    static constexpr int MARGIN = 8;
    static constexpr int BORDER_RADIUS = 8;
};

#endif // SCROLLINGCAPTURETHUMBNAIL_H
