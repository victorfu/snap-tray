#ifndef ANNOTATIONLAYER_H
#define ANNOTATIONLAYER_H

#include <QObject>
#include <QPainter>
#include <QPoint>
#include <QRect>
#include <QColor>
#include <QFont>
#include <QPixmap>
#include <memory>
#include <vector>

// Abstract base class for all annotation items
class AnnotationItem
{
public:
    virtual ~AnnotationItem() = default;
    virtual void draw(QPainter &painter) const = 0;
    virtual QRect boundingRect() const = 0;
    virtual std::unique_ptr<AnnotationItem> clone() const = 0;
};

// Freehand pencil stroke
class PencilStroke : public AnnotationItem
{
public:
    PencilStroke(const QVector<QPoint> &points, const QColor &color, int width);
    void draw(QPainter &painter) const override;
    QRect boundingRect() const override;
    std::unique_ptr<AnnotationItem> clone() const override;

    void addPoint(const QPoint &point);

private:
    QVector<QPoint> m_points;
    QColor m_color;
    int m_width;
};

// Semi-transparent marker/highlighter stroke
class MarkerStroke : public AnnotationItem
{
public:
    MarkerStroke(const QVector<QPoint> &points, const QColor &color, int width);
    void draw(QPainter &painter) const override;
    QRect boundingRect() const override;
    std::unique_ptr<AnnotationItem> clone() const override;

    void addPoint(const QPoint &point);

private:
    QVector<QPoint> m_points;
    QColor m_color;
    int m_width;
};

// Arrow annotation (line with arrowhead)
class ArrowAnnotation : public AnnotationItem
{
public:
    ArrowAnnotation(const QPoint &start, const QPoint &end, const QColor &color, int width);
    void draw(QPainter &painter) const override;
    QRect boundingRect() const override;
    std::unique_ptr<AnnotationItem> clone() const override;

    void setEnd(const QPoint &end);

private:
    void drawArrowhead(QPainter &painter, const QPoint &start, const QPoint &end) const;

    QPoint m_start;
    QPoint m_end;
    QColor m_color;
    int m_width;
};

// Rectangle annotation
class RectangleAnnotation : public AnnotationItem
{
public:
    RectangleAnnotation(const QRect &rect, const QColor &color, int width, bool filled = false);
    void draw(QPainter &painter) const override;
    QRect boundingRect() const override;
    std::unique_ptr<AnnotationItem> clone() const override;

    void setRect(const QRect &rect);

private:
    QRect m_rect;
    QColor m_color;
    int m_width;
    bool m_filled;
};

// Text annotation
class TextAnnotation : public AnnotationItem
{
public:
    TextAnnotation(const QPoint &position, const QString &text, const QFont &font, const QColor &color);
    void draw(QPainter &painter) const override;
    QRect boundingRect() const override;
    std::unique_ptr<AnnotationItem> clone() const override;

    void setText(const QString &text);

private:
    QPoint m_position;
    QString m_text;
    QFont m_font;
    QColor m_color;
};

// Step badge annotation (auto-incrementing numbered circle)
class StepBadgeAnnotation : public AnnotationItem
{
public:
    StepBadgeAnnotation(const QPoint &position, const QColor &color, int number = 1);
    void draw(QPainter &painter) const override;
    QRect boundingRect() const override;
    std::unique_ptr<AnnotationItem> clone() const override;

    void setNumber(int number);
    int number() const { return m_number; }
    static constexpr int kBadgeRadius = 14;

private:
    QPoint m_position;
    QColor m_color;
    int m_number;
};

// Mosaic (pixelation) annotation - rectangle-based (legacy)
class MosaicAnnotation : public AnnotationItem
{
public:
    MosaicAnnotation(const QRect &rect, const QPixmap &sourcePixmap, int blockSize = 10);
    void draw(QPainter &painter) const override;
    QRect boundingRect() const override;
    std::unique_ptr<AnnotationItem> clone() const override;

    void setRect(const QRect &rect);
    void updateSource(const QPixmap &sourcePixmap);

private:
    void generateMosaic();

    QRect m_rect;
    QPixmap m_sourcePixmap;
    QPixmap m_mosaicPixmap;
    int m_blockSize;
    qreal m_devicePixelRatio;
};

// Freehand mosaic stroke (eraser-like UX)
class MosaicStroke : public AnnotationItem
{
public:
    MosaicStroke(const QVector<QPoint> &points, const QPixmap &sourcePixmap, int width = 30, int blockSize = 8);
    void draw(QPainter &painter) const override;
    QRect boundingRect() const override;
    std::unique_ptr<AnnotationItem> clone() const override;

    void addPoint(const QPoint &point);
    void updateSource(const QPixmap &sourcePixmap);

private:
    void generateMosaic();

    QVector<QPoint> m_points;
    QPixmap m_sourcePixmap;
    mutable QImage m_sourceImageCache;
    int m_width;      // Brush width
    int m_blockSize;  // Mosaic block size
    qreal m_devicePixelRatio;
};

// Annotation layer that manages all annotations with undo/redo
class AnnotationLayer : public QObject
{
    Q_OBJECT

public:
    explicit AnnotationLayer(QObject *parent = nullptr);
    ~AnnotationLayer();

    void addItem(std::unique_ptr<AnnotationItem> item);
    void undo();
    void redo();
    void clear();
    void draw(QPainter &painter) const;

    bool canUndo() const;
    bool canRedo() const;
    bool isEmpty() const;

    // For rendering annotations onto the final image
    void drawOntoPixmap(QPixmap &pixmap) const;

    // Step badge helpers
    int countStepBadges() const;

signals:
    void changed();

private:
    static constexpr size_t kMaxHistorySize = 50;

    void trimHistory();
    void renumberStepBadges();

    std::vector<std::unique_ptr<AnnotationItem>> m_items;
    std::vector<std::unique_ptr<AnnotationItem>> m_redoStack;
};

#endif // ANNOTATIONLAYER_H
