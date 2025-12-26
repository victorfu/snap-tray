#ifndef RESIZEHANDLER_H
#define RESIZEHANDLER_H

#include <QObject>
#include <QPoint>
#include <QSize>
#include <QRect>

class ResizeHandler : public QObject
{
    Q_OBJECT

public:
    enum class Edge {
        None,
        Left, Right, Top, Bottom,
        TopLeft, TopRight, BottomLeft, BottomRight
    };

    explicit ResizeHandler(QObject* parent = nullptr);
    ResizeHandler(int shadowMargin, int minSize, QObject* parent = nullptr);
    ~ResizeHandler() override = default;

    // Configuration
    void setResizeMargin(int margin) { m_resizeMargin = margin; }
    void setShadowMargin(int margin) { m_shadowMargin = margin; }
    void setMinimumSize(int minSize) { m_minimumSize = minSize; }

    // Hit testing
    Edge getEdgeAt(const QPoint& pos, const QSize& windowSize) const;
    static Qt::CursorShape cursorForEdge(Edge edge);

    // Resize operations
    void startResize(Edge edge, const QPoint& globalPos, const QSize& windowSize, const QPoint& windowPos);
    void updateResize(const QPoint& globalPos, QSize& outSize, QPoint& outPos);
    void finishResize();

    bool isResizing() const { return m_isResizing; }
    Edge currentEdge() const { return m_currentEdge; }

signals:
    void resizeStarted();
    void resizeFinished();

private:
    int m_resizeMargin = 6;
    int m_shadowMargin = 8;
    int m_minimumSize = 50;

    bool m_isResizing = false;
    Edge m_currentEdge = Edge::None;
    QPoint m_startPos;
    QSize m_startSize;
    QPoint m_startWindowPos;
};

#endif // RESIZEHANDLER_H
