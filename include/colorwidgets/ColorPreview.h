#ifndef SNAPTRAY_COLOR_PREVIEW_H
#define SNAPTRAY_COLOR_PREVIEW_H

#include <QWidget>

namespace snaptray {
namespace colorwidgets {

class ColorPreview : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(QColor comparisonColor READ comparisonColor WRITE setComparisonColor)
    Q_PROPERTY(DisplayMode displayMode READ displayMode WRITE setDisplayMode)

public:
    enum DisplayMode {
        NoAlpha,           // Don't show transparency
        AllAlpha,          // Show transparency (checkerboard background)
        SplitAlpha,        // Left half opaque, right half with transparency
        SplitColor,        // Left half current color, right half comparison color
        SplitColorReverse  // Left half comparison color, right half current color
    };
    Q_ENUM(DisplayMode)

    explicit ColorPreview(QWidget* parent = nullptr);

    QColor color() const;
    QColor comparisonColor() const;
    DisplayMode displayMode() const;

    QSize sizeHint() const override;

public slots:
    void setColor(const QColor& color);
    void setComparisonColor(const QColor& color);
    void setDisplayMode(DisplayMode mode);

signals:
    void colorChanged(const QColor& color);
    void clicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void drawCheckerboard(QPainter& painter, const QRect& rect);

    QColor m_color = Qt::red;
    QColor m_comparisonColor = Qt::white;
    DisplayMode m_displayMode = NoAlpha;
};

}  // namespace colorwidgets
}  // namespace snaptray

#endif  // SNAPTRAY_COLOR_PREVIEW_H
