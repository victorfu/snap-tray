#include "AnnotationController.h"
#include "AnnotationLayer.h"

#include <QPainter>
#include <QDebug>

AnnotationController::AnnotationController(QObject* parent)
    : QObject(parent)
    , m_annotationLayer(nullptr)
    , m_currentTool(Tool::None)
    , m_color(Qt::red)
    , m_width(3)
    , m_isDrawing(false)
{
}

AnnotationController::~AnnotationController()
{
}

void AnnotationController::setAnnotationLayer(AnnotationLayer* layer)
{
    m_annotationLayer = layer;
}

void AnnotationController::setCurrentTool(Tool tool)
{
    m_currentTool = tool;
}

void AnnotationController::setColor(const QColor& color)
{
    m_color = color;
}

void AnnotationController::setWidth(int width)
{
    m_width = width;
}

void AnnotationController::startDrawing(const QPoint& pos)
{
    if (m_currentTool == Tool::None) return;

    m_isDrawing = true;
    m_startPoint = pos;
    m_currentPath.clear();
    m_currentPath.append(pos);

    switch (m_currentTool) {
    case Tool::Pencil:
        m_currentPencil = std::make_unique<PencilStroke>(m_currentPath, m_color, m_width);
        break;

    case Tool::Marker:
        m_currentMarker = std::make_unique<MarkerStroke>(m_currentPath, m_color, 20);
        break;

    case Tool::Arrow:
        m_currentArrow = std::make_unique<ArrowAnnotation>(pos, pos, m_color, m_width);
        break;

    case Tool::Rectangle:
        m_currentRectangle = std::make_unique<RectangleAnnotation>(QRect(pos, pos), m_color, m_width);
        break;

    case Tool::Ellipse:
        m_currentEllipse = std::make_unique<EllipseAnnotation>(QRect(pos, pos), m_color, m_width);
        break;

    default:
        m_isDrawing = false;
        break;
    }

    emit drawingStateChanged(m_isDrawing);
}

void AnnotationController::updateDrawing(const QPoint& pos)
{
    if (!m_isDrawing) return;

    switch (m_currentTool) {
    case Tool::Pencil:
        if (m_currentPencil) {
            m_currentPath.append(pos);
            m_currentPencil->addPoint(pos);
        }
        break;

    case Tool::Marker:
        if (m_currentMarker) {
            m_currentPath.append(pos);
            m_currentMarker->addPoint(pos);
        }
        break;

    case Tool::Arrow:
        if (m_currentArrow) {
            m_currentArrow->setEnd(pos);
        }
        break;

    case Tool::Rectangle:
        if (m_currentRectangle) {
            m_currentRectangle->setRect(QRect(m_startPoint, pos));
        }
        break;

    case Tool::Ellipse:
        if (m_currentEllipse) {
            m_currentEllipse->setRect(QRect(m_startPoint, pos));
        }
        break;

    default:
        break;
    }
}

void AnnotationController::finishDrawing()
{
    if (!m_isDrawing) return;

    m_isDrawing = false;

    if (!m_annotationLayer) {
        cancelDrawing();
        return;
    }

    switch (m_currentTool) {
    case Tool::Pencil:
        if (m_currentPencil && m_currentPath.size() > 1) {
            m_annotationLayer->addItem(std::move(m_currentPencil));
        }
        m_currentPencil.reset();
        break;

    case Tool::Marker:
        if (m_currentMarker && m_currentPath.size() > 1) {
            m_annotationLayer->addItem(std::move(m_currentMarker));
        }
        m_currentMarker.reset();
        break;

    case Tool::Arrow:
        if (m_currentArrow) {
            m_annotationLayer->addItem(std::move(m_currentArrow));
        }
        m_currentArrow.reset();
        break;

    case Tool::Rectangle:
        if (m_currentRectangle) {
            m_annotationLayer->addItem(std::move(m_currentRectangle));
        }
        m_currentRectangle.reset();
        break;

    case Tool::Ellipse:
        if (m_currentEllipse) {
            m_annotationLayer->addItem(std::move(m_currentEllipse));
        }
        m_currentEllipse.reset();
        break;

    default:
        break;
    }

    m_currentPath.clear();
    emit drawingStateChanged(false);
    emit annotationCompleted();
}

void AnnotationController::cancelDrawing()
{
    m_isDrawing = false;
    m_currentPath.clear();
    m_currentPencil.reset();
    m_currentMarker.reset();
    m_currentArrow.reset();
    m_currentRectangle.reset();
    m_currentEllipse.reset();
    emit drawingStateChanged(false);
}

void AnnotationController::drawCurrentAnnotation(QPainter& painter) const
{
    if (!m_isDrawing) return;

    if (m_currentPencil) {
        m_currentPencil->draw(painter);
    } else if (m_currentMarker) {
        m_currentMarker->draw(painter);
    } else if (m_currentArrow) {
        m_currentArrow->draw(painter);
    } else if (m_currentRectangle) {
        m_currentRectangle->draw(painter);
    } else if (m_currentEllipse) {
        m_currentEllipse->draw(painter);
    }
}

bool AnnotationController::supportsTool(Tool tool) const
{
    switch (tool) {
    case Tool::Pencil:
    case Tool::Marker:
    case Tool::Arrow:
    case Tool::Rectangle:
    case Tool::Ellipse:
        return true;
    default:
        return false;
    }
}
