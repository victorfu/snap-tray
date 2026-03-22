#include "tools/ToolManager.h"
#include "tools/ToolRegistry.h"
#include "tools/handlers/AllHandlers.h"

#include <QPainter>
#include <utility>

ToolManager::ToolManager(QObject* parent)
    : QObject(parent)
    , m_context(std::make_unique<ToolContext>())
{
    m_context->requestRepaint = [this]() {
        emitFullRepaint();
    };
    m_context->requestRectRepaint = [this](const QRect& rect) {
        emitAnnotationRepaint(rect);
    };
}

ToolManager::~ToolManager() = default;

namespace {
QRect combineDirtyBounds(const QRect& first, const QRect& second)
{
    if (!first.isValid()) {
        return second;
    }
    if (!second.isValid()) {
        return first;
    }
    return first.united(second);
}
} // namespace

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
    registerHandler(std::make_unique<PolylineToolHandler>());
    registerHandler(std::make_unique<ShapeToolHandler>());
    registerHandler(std::make_unique<MosaicToolHandler>());
    registerHandler(std::make_unique<EraserToolHandler>());
    registerHandler(std::make_unique<StepBadgeToolHandler>());
    registerHandler(std::make_unique<EmojiStickerToolHandler>());
    registerHandler(std::make_unique<TextToolHandler>());
    registerHandler(std::make_unique<CropToolHandler>());
    registerHandler(std::make_unique<MeasureToolHandler>());
}

void ToolManager::setCurrentTool(ToolId id) {
    if (m_currentToolId == id) {
        return;
    }

    const QRect oldPreviewBounds = previewBoundsForHandler(currentHandler());

    // Deactivate current handler
    if (auto* current = currentHandler()) {
        current->onDeactivate(m_context.get());
    }

    m_currentToolId = id;

    // Activate new handler
    if (auto* newHandler = currentHandler()) {
        newHandler->onActivate(m_context.get());
    }

    const QRect newPreviewBounds = previewBoundsForHandler(currentHandler());
    emitBoundsDiff(oldPreviewBounds, newPreviewBounds);
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

void ToolManager::handleMousePress(const QPoint& pos, Qt::KeyboardModifiers modifiers) {
    m_context->shiftPressed = modifiers & Qt::ShiftModifier;
    if (auto* h = currentHandler()) {
        const QRect oldPreviewBounds = previewBoundsForHandler(h);
        m_wasDrawing = h->isDrawing();
        h->onMousePress(m_context.get(), pos);
        if (!m_wasDrawing && h->isDrawing()) {
            emit drawingStarted();
        }
        emitBoundsDiff(oldPreviewBounds, previewBoundsForHandler(h));
    }
}

void ToolManager::handleMouseMove(const QPoint& pos, Qt::KeyboardModifiers modifiers) {
    m_context->shiftPressed = modifiers & Qt::ShiftModifier;
    if (auto* h = currentHandler()) {
        const QRect oldPreviewBounds = previewBoundsForHandler(h);
        h->onMouseMove(m_context.get(), pos);
        emitBoundsDiff(oldPreviewBounds, previewBoundsForHandler(h));
    }
}

void ToolManager::handleMouseRelease(const QPoint& pos, Qt::KeyboardModifiers modifiers) {
    m_context->shiftPressed = modifiers & Qt::ShiftModifier;
    if (auto* h = currentHandler()) {
        const QRect oldPreviewBounds = previewBoundsForHandler(h);
        bool wasDrawing = h->isDrawing();
        h->onMouseRelease(m_context.get(), pos);
        if (wasDrawing && !h->isDrawing()) {
            emit drawingFinished();
        }
        emitBoundsDiff(oldPreviewBounds, previewBoundsForHandler(h));
    }
}

void ToolManager::handleDoubleClick(const QPoint& pos) {
    if (auto* h = currentHandler()) {
        const QRect oldPreviewBounds = previewBoundsForHandler(h);
        bool wasDrawing = h->isDrawing();
        h->onDoubleClick(m_context.get(), pos);
        if (wasDrawing && !h->isDrawing()) {
            emit drawingFinished();
        }
        emitBoundsDiff(oldPreviewBounds, previewBoundsForHandler(h));
    }
}

bool ToolManager::handleTextInteractionPress(const QPoint& pos, Qt::KeyboardModifiers modifiers)
{
    return dispatchInteraction(ToolId::Text, [this, pos, modifiers](IToolHandler* handler) {
        m_context->shiftPressed = modifiers & Qt::ShiftModifier;
        return handler->handleInteractionPress(m_context.get(), pos, modifiers);
    });
}

bool ToolManager::handleTextInteractionMove(const QPoint& pos, Qt::KeyboardModifiers modifiers)
{
    return dispatchInteraction(ToolId::Text, [this, pos, modifiers](IToolHandler* handler) {
        m_context->shiftPressed = modifiers & Qt::ShiftModifier;
        return handler->handleInteractionMove(m_context.get(), pos, modifiers);
    });
}

bool ToolManager::handleTextInteractionRelease(const QPoint& pos, Qt::KeyboardModifiers modifiers)
{
    return dispatchInteraction(ToolId::Text, [this, pos, modifiers](IToolHandler* handler) {
        m_context->shiftPressed = modifiers & Qt::ShiftModifier;
        return handler->handleInteractionRelease(m_context.get(), pos, modifiers);
    });
}

bool ToolManager::handleTextInteractionDoubleClick(const QPoint& pos)
{
    return dispatchInteraction(ToolId::Text, [this, pos](IToolHandler* handler) {
        return handler->handleInteractionDoubleClick(m_context.get(), pos);
    });
}

bool ToolManager::handleShapeInteractionPress(const QPoint& pos, Qt::KeyboardModifiers modifiers)
{
    return dispatchInteraction(ToolId::Shape, [this, pos, modifiers](IToolHandler* handler) {
        m_context->shiftPressed = modifiers & Qt::ShiftModifier;
        return handler->handleInteractionPress(m_context.get(), pos, modifiers);
    });
}

bool ToolManager::handleShapeInteractionMove(const QPoint& pos, Qt::KeyboardModifiers modifiers)
{
    return dispatchInteraction(ToolId::Shape, [this, pos, modifiers](IToolHandler* handler) {
        m_context->shiftPressed = modifiers & Qt::ShiftModifier;
        return handler->handleInteractionMove(m_context.get(), pos, modifiers);
    });
}

bool ToolManager::handleShapeInteractionRelease(const QPoint& pos, Qt::KeyboardModifiers modifiers)
{
    return dispatchInteraction(ToolId::Shape, [this, pos, modifiers](IToolHandler* handler) {
        m_context->shiftPressed = modifiers & Qt::ShiftModifier;
        return handler->handleInteractionRelease(m_context.get(), pos, modifiers);
    });
}

bool ToolManager::handleArrowInteractionPress(const QPoint& pos, Qt::KeyboardModifiers modifiers)
{
    return dispatchInteraction(ToolId::Arrow, [this, pos, modifiers](IToolHandler* handler) {
        m_context->shiftPressed = modifiers & Qt::ShiftModifier;
        return handler->handleInteractionPress(m_context.get(), pos, modifiers);
    });
}

bool ToolManager::handleArrowInteractionMove(const QPoint& pos, Qt::KeyboardModifiers modifiers)
{
    return dispatchInteraction(ToolId::Arrow, [this, pos, modifiers](IToolHandler* handler) {
        m_context->shiftPressed = modifiers & Qt::ShiftModifier;
        return handler->handleInteractionMove(m_context.get(), pos, modifiers);
    });
}

bool ToolManager::handleArrowInteractionRelease(const QPoint& pos, Qt::KeyboardModifiers modifiers)
{
    return dispatchInteraction(ToolId::Arrow, [this, pos, modifiers](IToolHandler* handler) {
        m_context->shiftPressed = modifiers & Qt::ShiftModifier;
        return handler->handleInteractionRelease(m_context.get(), pos, modifiers);
    });
}

bool ToolManager::handlePolylineInteractionPress(const QPoint& pos, Qt::KeyboardModifiers modifiers)
{
    return dispatchInteraction(ToolId::Polyline, [this, pos, modifiers](IToolHandler* handler) {
        m_context->shiftPressed = modifiers & Qt::ShiftModifier;
        return handler->handleInteractionPress(m_context.get(), pos, modifiers);
    });
}

bool ToolManager::handlePolylineInteractionMove(const QPoint& pos, Qt::KeyboardModifiers modifiers)
{
    return dispatchInteraction(ToolId::Polyline, [this, pos, modifiers](IToolHandler* handler) {
        m_context->shiftPressed = modifiers & Qt::ShiftModifier;
        return handler->handleInteractionMove(m_context.get(), pos, modifiers);
    });
}

bool ToolManager::handlePolylineInteractionRelease(const QPoint& pos, Qt::KeyboardModifiers modifiers)
{
    return dispatchInteraction(ToolId::Polyline, [this, pos, modifiers](IToolHandler* handler) {
        m_context->shiftPressed = modifiers & Qt::ShiftModifier;
        return handler->handleInteractionRelease(m_context.get(), pos, modifiers);
    });
}

bool ToolManager::handleEmojiInteractionPress(const QPoint& pos, Qt::KeyboardModifiers modifiers)
{
    return dispatchInteraction(ToolId::EmojiSticker, [this, pos, modifiers](IToolHandler* handler) {
        m_context->shiftPressed = modifiers & Qt::ShiftModifier;
        return handler->handleInteractionPress(m_context.get(), pos, modifiers);
    });
}

bool ToolManager::handleEmojiInteractionMove(const QPoint& pos, Qt::KeyboardModifiers modifiers)
{
    return dispatchInteraction(ToolId::EmojiSticker, [this, pos, modifiers](IToolHandler* handler) {
        m_context->shiftPressed = modifiers & Qt::ShiftModifier;
        return handler->handleInteractionMove(m_context.get(), pos, modifiers);
    });
}

bool ToolManager::handleEmojiInteractionRelease(const QPoint& pos, Qt::KeyboardModifiers modifiers)
{
    return dispatchInteraction(ToolId::EmojiSticker, [this, pos, modifiers](IToolHandler* handler) {
        m_context->shiftPressed = modifiers & Qt::ShiftModifier;
        return handler->handleInteractionRelease(m_context.get(), pos, modifiers);
    });
}

bool ToolManager::hasActiveInteraction() const
{
    return activeInteractionKind() != AnnotationInteractionKind::None;
}

AnnotationInteractionKind ToolManager::activeInteractionKind() const
{
    for (const auto& [toolId, handler] : m_handlers) {
        Q_UNUSED(toolId);
        if (!handler) {
            continue;
        }

        const AnnotationInteractionKind kind = interactionKindForHandler(handler.get());
        if (kind != AnnotationInteractionKind::None) {
            return kind;
        }
    }

    return AnnotationInteractionKind::None;
}

ToolId ToolManager::activeInteractionTool() const
{
    for (const auto& [toolId, handler] : m_handlers) {
        if (!handler) {
            continue;
        }

        if (interactionKindForHandler(handler.get()) != AnnotationInteractionKind::None) {
            return toolId;
        }
    }

    return m_currentToolId;
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

bool ToolManager::handleEscape() {
    auto dispatchEscape = [this](IToolHandler* handler) {
        if (!handler) {
            return false;
        }

        const QRect oldBounds = combineDirtyBounds(
            previewBoundsForHandler(handler),
            interactionBoundsForHandler(handler));
        if (handler->handleEscape(m_context.get())) {
            const QRect newBounds = combineDirtyBounds(
                previewBoundsForHandler(handler),
                interactionBoundsForHandler(handler));
            emitBoundsDiff(oldBounds, newBounds);
            return true;
        }
        return false;
    };

    if (dispatchEscape(currentHandler())) {
        return true;
    }

    const ToolId interactionTool = activeInteractionTool();
    if (interactionTool != m_currentToolId &&
        dispatchEscape(handler(interactionTool))) {
        return true;
    }

    if (m_context && m_context->annotationLayer &&
        m_context->annotationLayer->selectedIndex() >= 0) {
        m_context->annotationLayer->clearSelection();
        m_context->repaint();
        return true;
    }

    return false;
}

void ToolManager::setAnnotationLayer(AnnotationLayer* layer) {
    m_context->annotationLayer = layer;
}

void ToolManager::setSourcePixmap(SharedPixmap pixmap) {
    m_context->sourcePixmap = std::move(pixmap);
}

void ToolManager::setDevicePixelRatio(qreal dpr) {
    m_context->devicePixelRatio = dpr;
}

void ToolManager::setInlineTextEditor(InlineTextEditor* editor)
{
    m_context->inlineTextEditor = editor;
}

void ToolManager::setTextAnnotationEditor(TextAnnotationEditor* editor)
{
    m_context->textAnnotationEditor = editor;
}

void ToolManager::setShapeAnnotationEditor(ShapeAnnotationEditor* editor)
{
    m_context->shapeAnnotationEditor = editor;
}

void ToolManager::setTextEditingBounds(const QRect& bounds)
{
    m_context->textEditingBounds = bounds;
}

void ToolManager::setTextColorSyncCallback(std::function<void(const QColor&)> callback)
{
    m_context->syncColorToHost = std::move(callback);
}

void ToolManager::setHostFocusCallback(std::function<void()> callback)
{
    m_context->requestHostFocus = std::move(callback);
}

void ToolManager::setTextReEditStartedCallback(std::function<void()> callback)
{
    m_context->notifyTextReEditStarted = std::move(callback);
}

void ToolManager::setAnnotationSurfaceAdapter(AnnotationSurfaceAdapter* adapter)
{
    m_context->annotationSurface = adapter;
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

void ToolManager::setLineStyle(LineStyle style) {
    m_context->lineStyle = style;
}

void ToolManager::setShapeType(int type) {
    m_context->shapeType = type;
}

void ToolManager::setShapeFillMode(int mode) {
    m_context->shapeFillMode = mode;
}

void ToolManager::setMosaicBlurType(MosaicBlurType type) {
    m_context->mosaicBlurType = type;
}

void ToolManager::emitFullRepaint()
{
    emit needsRepaint();
}

void ToolManager::emitAnnotationRepaint(const QRect& annotationRect)
{
    if (!annotationRect.isValid()) {
        emitFullRepaint();
        return;
    }

    if (m_context && m_context->annotationSurface) {
        if (!m_context->annotationSurface->supportsAnnotationRectUpdate()) {
            emitFullRepaint();
            return;
        }

        const QRect hostRect = m_context->annotationSurface->mapAnnotationRectToHostUpdate(annotationRect)
                                   .intersected(m_context->annotationSurface->annotationViewport());
        if (!hostRect.isValid()) {
            emitFullRepaint();
            return;
        }
        emit needsRepaint(hostRect);
        return;
    }

    emit needsRepaint(annotationRect);
}

void ToolManager::emitBoundsDiff(const QRect& before, const QRect& after)
{
    if (before.isNull() && after.isNull()) {
        return;
    }

    if (!before.isValid() && !after.isValid()) {
        return;
    }

    if (!before.isValid()) {
        emitAnnotationRepaint(after);
        return;
    }

    if (!after.isValid()) {
        emitAnnotationRepaint(before);
        return;
    }

    emitAnnotationRepaint(before.united(after));
}

QRect ToolManager::previewBoundsForHandler(const IToolHandler* handler) const
{
    if (!handler || !m_context) {
        return QRect();
    }
    return handler->previewBounds(m_context.get());
}

QRect ToolManager::interactionBoundsForHandler(const IToolHandler* handler) const
{
    if (!handler || !m_context) {
        return QRect();
    }
    return handler->interactionBounds(m_context.get());
}

AnnotationInteractionKind ToolManager::interactionKindForHandler(const IToolHandler* handler) const
{
    if (!handler || !m_context) {
        return AnnotationInteractionKind::None;
    }

    return handler->activeInteractionKind(m_context.get());
}

bool ToolManager::dispatchInteraction(ToolId id,
                                      const std::function<bool(IToolHandler*)>& callback)
{
    auto it = m_handlers.find(id);
    if (it == m_handlers.end() || !callback) {
        return false;
    }

    IToolHandler* handler = it->second.get();
    const QRect oldBounds = interactionBoundsForHandler(handler);
    const bool handled = callback(handler);
    if (!handled) {
        return false;
    }

    emitBoundsDiff(oldBounds, interactionBoundsForHandler(handler));
    return true;
}
