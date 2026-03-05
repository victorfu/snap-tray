#pragma once

#include <QColor>
#include <QQuickPaintedItem>
#include <QUrl>

#include <memory>

class QSvgRenderer;

/**
 * @brief QML SVG icon item that renders with QSvgRenderer and runtime tinting.
 *
 * This avoids runtime dependency on Qt5Compat GraphicalEffects and does not rely
 * on the QML Image SVG plugin path.
 */
class SvgIconItem : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)

public:
    explicit SvgIconItem(QQuickItem* parent = nullptr);
    ~SvgIconItem() override;

    void paint(QPainter* painter) override;

    QUrl source() const { return m_source; }
    void setSource(const QUrl& source);

    QColor color() const { return m_color; }
    void setColor(const QColor& color);

signals:
    void sourceChanged();
    void colorChanged();

private:
    static QString resolveSourcePath(const QUrl& source);

    QUrl m_source;
    QColor m_color = Qt::white;
    std::unique_ptr<QSvgRenderer> m_renderer;
};
