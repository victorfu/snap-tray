#include "tools/ToolManager.h"
#include "tools/ToolRegistry.h"
#include "tools/handlers/AllHandlers.h"

#include <QPainter>

ToolManager::ToolManager(QObject* parent)
    : QObject(parent)
    , m_context(std::make_unique<ToolContext>())
{
    // Set up the repaint callback to emit our signal
    m_context->requestRepaint = [this]() {
        emit needsRepaint();
    };
}

ToolManager::~ToolManager() = default;

void ToolManager::registerHandler(std::unique_ptr<IToolHandler> handler) {
    if (handler) {
        ToolId id = handler->toolId();
        m_handlers[id] = std::move(handler);
    }
}

void ToolManager::registerDefaultHandlers() {
    // Register all default tool handlers
    registerHandler(std::make_unique<SelectionToolHandler>());
    registerHandler(std::make_unique<PencilToolHandler>());
    registerHandler(std::make_unique<MarkerToolHandler>());
    registerHandler(std::make_unique<ArrowToolHandler>());
    registerHandler(std::make_unique<ShapeToolHandler>());
    registerHandler(std::make_unique<MosaicToolHandler>());
    registerHandler(std::make_unique<StepBadgeToolHandler>());
    registerHandler(std::make_unique<EraserToolHandler>());
    registerHandler(std::make_unique<LaserPointerToolHandler>());
}

void ToolManager::setCurrentTool(ToolId id) {
    if (m_currentToolId == id) {
        return;
    }

    // Deactivate current handler
    if (auto* current = currentHandler()) {
        current->onDeactivate(m_context.get());
    }

    ToolId oldTool = m_currentToolId;
    m_currentToolId = id;

    // Activate new handler
    if (auto* newHandler = currentHandler()) {
        newHandler->onActivate(m_context.get());
    }

    Q_UNUSED(oldTool);
    emit toolChanged(m_currentToolId);
}

IToolHandler* ToolManager::currentHandler() {
    return handler(m_currentToolId);
}

IToolHandler* ToolManager::handler(ToolId id) {
    auto it = m_handlers.find(id);
    if (it != m_handlers.end()) {
        return it->second.get();
    }
    return nullptr;
}

void ToolManager::handleMousePress(const QPoint& pos) {
    if (auto* h = currentHandler()) {
        m_wasDrawing = h->isDrawing();
        h->onMousePress(m_context.get(), pos);
        if (!m_wasDrawing && h->isDrawing()) {
            emit drawingStarted();
        }
    }
}

void ToolManager::handleMouseMove(const QPoint& pos) {
    if (auto* h = currentHandler()) {
        h->onMouseMove(m_context.get(), pos);
    }
}

void ToolManager::handleMouseRelease(const QPoint& pos) {
    if (auto* h = currentHandler()) {
        bool wasDrawing = h->isDrawing();
        h->onMouseRelease(m_context.get(), pos);
        if (wasDrawing && !h->isDrawing()) {
            emit drawingFinished();
        }
    }
}

void ToolManager::drawCurrentPreview(QPainter& painter) const {
    auto it = m_handlers.find(m_currentToolId);
    if (it != m_handlers.end()) {
        it->second->drawPreview(painter);
    }
}

bool ToolManager::isDrawing() const {
    auto it = m_handlers.find(m_currentToolId);
    if (it != m_handlers.end()) {
        return it->second->isDrawing();
    }
    return false;
}

void ToolManager::cancelDrawing() {
    if (auto* h = currentHandler()) {
        h->cancelDrawing();
    }
}

void ToolManager::setAnnotationLayer(AnnotationLayer* layer) {
    m_context->annotationLayer = layer;
}

void ToolManager::setSourcePixmap(const QPixmap* pixmap) {
    m_context->sourcePixmap = pixmap;
}

void ToolManager::setDevicePixelRatio(qreal dpr) {
    m_context->devicePixelRatio = dpr;
}

void ToolManager::setColor(const QColor& color) {
    m_context->color = color;
}

void ToolManager::setWidth(int width) {
    m_context->width = width;
}

void ToolManager::setArrowStyle(LineEndStyle style) {
    m_context->arrowStyle = style;
}

void ToolManager::setShapeType(int type) {
    m_context->shapeType = type;
}

void ToolManager::setShapeFillMode(int mode) {
    m_context->shapeFillMode = mode;
}

void ToolManager::setMosaicStrength(int strength) {
    m_context->mosaicStrength = strength;
}

void ToolManager::setRepaintCallback(std::function<void()> callback) {
    m_context->requestRepaint = callback;
}
