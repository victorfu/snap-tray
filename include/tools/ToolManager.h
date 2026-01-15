#ifndef TOOLMANAGER_H
#define TOOLMANAGER_H

#include <QObject>
#include <QPoint>
#include <memory>
#include <map>

#include "ToolId.h"
#include "ToolContext.h"
#include "IToolHandler.h"

class QPainter;
class AnnotationLayer;

/**
 * @brief Central manager for tool handling.
 *
 * Manages tool handlers and dispatches events to the appropriate
 * handler based on the current tool. Also manages shared context
 * and tool state.
 */
class ToolManager : public QObject {
    Q_OBJECT

public:
    explicit ToolManager(QObject* parent = nullptr);
    ~ToolManager() override;

    /**
     * @brief Register a tool handler.
     *
     * The manager takes ownership of the handler.
     */
    void registerHandler(std::unique_ptr<IToolHandler> handler);

    /**
     * @brief Register all default tool handlers.
     *
     * Call this to set up all standard drawing tools.
     */
    void registerDefaultHandlers();

    /**
     * @brief Set the current active tool.
     */
    void setCurrentTool(ToolId id);

    /**
     * @brief Get the current active tool.
     */
    ToolId currentTool() const { return m_currentToolId; }

    /**
     * @brief Get the current tool handler.
     */
    IToolHandler* currentHandler();

    /**
     * @brief Get a specific tool handler.
     */
    IToolHandler* handler(ToolId id);

    // Event dispatch methods
    void handleMousePress(const QPoint& pos);
    void handleMouseMove(const QPoint& pos);
    void handleMouseRelease(const QPoint& pos);
    void handleDoubleClick(const QPoint& pos);

    /**
     * @brief Draw the current tool preview.
     */
    void drawCurrentPreview(QPainter& painter) const;

    /**
     * @brief Check if currently drawing.
     */
    bool isDrawing() const;

    /**
     * @brief Cancel current drawing operation.
     */
    void cancelDrawing();

    // Context management
    ToolContext* context() { return m_context.get(); }

    void setAnnotationLayer(AnnotationLayer* layer);
    void setSourcePixmap(SharedPixmap pixmap);
    void setDevicePixelRatio(qreal dpr);

    // Drawing settings
    void setColor(const QColor& color);
    QColor color() const { return m_context->color; }

    void setWidth(int width);
    int width() const { return m_context->width; }

    void setArrowStyle(LineEndStyle style);
    LineEndStyle arrowStyle() const { return m_context->arrowStyle; }

    void setLineStyle(LineStyle style);
    LineStyle lineStyle() const { return m_context->lineStyle; }

    void setShapeType(int type);
    int shapeType() const { return m_context->shapeType; }

    void setShapeFillMode(int mode);
    int shapeFillMode() const { return m_context->shapeFillMode; }

    void setMosaicBlurType(MosaicStroke::BlurType type);
    MosaicStroke::BlurType mosaicBlurType() const { return m_context->mosaicBlurType; }

    // Callback setup
    void setRepaintCallback(std::function<void()> callback);

signals:
    /**
     * @brief Emitted when the current tool changes.
     */
    void toolChanged(ToolId newTool);

    /**
     * @brief Emitted when drawing starts.
     */
    void drawingStarted();

    /**
     * @brief Emitted when drawing finishes.
     */
    void drawingFinished();

    /**
     * @brief Emitted when a repaint is needed.
     */
    void needsRepaint();

private:
    std::map<ToolId, std::unique_ptr<IToolHandler>> m_handlers;
    std::unique_ptr<ToolContext> m_context;
    ToolId m_currentToolId = ToolId::Selection;
    bool m_wasDrawing = false;
};

#endif // TOOLMANAGER_H
