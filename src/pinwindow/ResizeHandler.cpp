#include "pinwindow/ResizeHandler.h"

ResizeHandler::ResizeHandler(QObject* parent)
    : QObject(parent)
{
}

ResizeHandler::ResizeHandler(int shadowMargin, int minSize, QObject* parent)
    : QObject(parent)
    , m_shadowMargin(shadowMargin)
    , m_minimumSize(minSize)
{
}

ResizeHandler::Edge ResizeHandler::getEdgeAt(const QPoint& pos, const QSize& windowSize) const
{
    int margin = m_resizeMargin + m_shadowMargin;

    bool onLeft = pos.x() < margin;
    bool onRight = pos.x() > windowSize.width() - margin;
    bool onTop = pos.y() < margin;
    bool onBottom = pos.y() > windowSize.height() - margin;

    if (onTop && onLeft) return Edge::TopLeft;
    if (onTop && onRight) return Edge::TopRight;
    if (onBottom && onLeft) return Edge::BottomLeft;
    if (onBottom && onRight) return Edge::BottomRight;
    if (onLeft) return Edge::Left;
    if (onRight) return Edge::Right;
    if (onTop) return Edge::Top;
    if (onBottom) return Edge::Bottom;

    return Edge::None;
}

void ResizeHandler::startResize(Edge edge, const QPoint& globalPos, const QSize& windowSize, const QPoint& windowPos)
{
    m_isResizing = true;
    m_currentEdge = edge;
    m_startPos = globalPos;
    m_startSize = windowSize;
    m_startWindowPos = windowPos;
    emit resizeStarted();
}

void ResizeHandler::updateResize(const QPoint& globalPos, QSize& outSize, QPoint& outPos)
{
    if (!m_isResizing) {
        outSize = m_startSize;
        outPos = m_startWindowPos;
        return;
    }

    QPoint delta = globalPos - m_startPos;
    QSize newSize = m_startSize;
    QPoint newPos = m_startWindowPos;

    // Calculate new size based on which edge is being dragged
    switch (m_currentEdge) {
        case Edge::Right:
            newSize.setWidth(m_startSize.width() + delta.x());
            break;
        case Edge::Bottom:
            newSize.setHeight(m_startSize.height() + delta.y());
            break;
        case Edge::Left:
            newSize.setWidth(m_startSize.width() - delta.x());
            newPos.setX(m_startWindowPos.x() + delta.x());
            break;
        case Edge::Top:
            newSize.setHeight(m_startSize.height() - delta.y());
            newPos.setY(m_startWindowPos.y() + delta.y());
            break;
        case Edge::TopLeft:
            newSize.setWidth(m_startSize.width() - delta.x());
            newSize.setHeight(m_startSize.height() - delta.y());
            newPos.setX(m_startWindowPos.x() + delta.x());
            newPos.setY(m_startWindowPos.y() + delta.y());
            break;
        case Edge::TopRight:
            newSize.setWidth(m_startSize.width() + delta.x());
            newSize.setHeight(m_startSize.height() - delta.y());
            newPos.setY(m_startWindowPos.y() + delta.y());
            break;
        case Edge::BottomLeft:
            newSize.setWidth(m_startSize.width() - delta.x());
            newSize.setHeight(m_startSize.height() + delta.y());
            newPos.setX(m_startWindowPos.x() + delta.x());
            break;
        case Edge::BottomRight:
            newSize.setWidth(m_startSize.width() + delta.x());
            newSize.setHeight(m_startSize.height() + delta.y());
            break;
        default:
            break;
    }

    // Enforce minimum size
    int minWindowSize = m_minimumSize + m_shadowMargin * 2;
    if (newSize.width() < minWindowSize) {
        if (m_currentEdge == Edge::Left ||
            m_currentEdge == Edge::TopLeft ||
            m_currentEdge == Edge::BottomLeft) {
            newPos.setX(m_startWindowPos.x() + m_startSize.width() - minWindowSize);
        }
        newSize.setWidth(minWindowSize);
    }
    if (newSize.height() < minWindowSize) {
        if (m_currentEdge == Edge::Top ||
            m_currentEdge == Edge::TopLeft ||
            m_currentEdge == Edge::TopRight) {
            newPos.setY(m_startWindowPos.y() + m_startSize.height() - minWindowSize);
        }
        newSize.setHeight(minWindowSize);
    }

    outSize = newSize;
    outPos = newPos;
}

void ResizeHandler::finishResize()
{
    if (m_isResizing) {
        m_isResizing = false;
        m_currentEdge = Edge::None;
        emit resizeFinished();
    }
}
