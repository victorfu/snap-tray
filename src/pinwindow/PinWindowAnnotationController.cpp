#include "pinwindow/PinWindowAnnotationController.h"
#include "annotations/AnnotationLayer.h"
#include "tools/ToolManager.h"
#include "tools/ToolId.h"
#include "ColorAndWidthWidget.h"
#include "InlineTextEditor.h"
#include "region/TextAnnotationEditor.h"
#include "settings/AnnotationSettingsManager.h"

#include <QWidget>
#include <QPainter>
#include <QPixmap>
#include <QTransform>
#include <QDebug>
#include <QtMath>

PinWindowAnnotationController::PinWindowAnnotationController(QWidget* parent)
    : QObject(parent)
    , m_annotationLayer(std::make_unique<AnnotationLayer>())
    , m_toolManager(std::make_unique<ToolManager>())
    , m_colorAndWidthWidget(std::make_unique<ColorAndWidthWidget>())
    , m_parentWidget(parent)
{
    // Register tool handlers
    m_toolManager->registerDefaultHandlers();
    m_toolManager->setAnnotationLayer(m_annotationLayer.get());

    // Connect tool manager signals
    connect(m_toolManager.get(), &ToolManager::needsRepaint,
            this, &PinWindowAnnotationController::needsRepaint);
    connect(m_toolManager.get(), &ToolManager::toolChanged,
            this, [this](ToolId tool) {
                emit toolChanged(static_cast<int>(tool));
            });

    // Connect annotation layer signals
    connect(m_annotationLayer.get(), &AnnotationLayer::modified,
            this, &PinWindowAnnotationController::undoRedoStateChanged);

    // Load saved settings
    auto& settings = AnnotationSettingsManager::instance();
    m_toolManager->setColor(settings.loadColor());
    m_toolManager->setWidth(settings.loadWidth());

    // Create text editor for text annotations
    if (m_parentWidget) {
        m_textEditor = std::make_unique<InlineTextEditor>(m_parentWidget);
        m_textAnnotationEditor = std::make_unique<TextAnnotationEditor>(this);
        m_textAnnotationEditor->setAnnotationLayer(m_annotationLayer.get());
        m_textAnnotationEditor->setTextEditor(m_textEditor.get());
        m_textAnnotationEditor->setColorAndWidthWidget(m_colorAndWidthWidget.get());
        m_textAnnotationEditor->setParentWidget(m_parentWidget);

        // Connect text editor signals
        connect(m_textEditor.get(), &InlineTextEditor::editingFinished,
                this, [this](const QString& text, const QPoint& position) {
                    m_textAnnotationEditor->finishEditing(text, position, m_toolManager->color());
                    emit needsRepaint();
                });
        connect(m_textEditor.get(), &InlineTextEditor::editingCancelled,
                this, [this]() {
                    m_textAnnotationEditor->cancelEditing();
                    emit needsRepaint();
                });
        connect(m_textAnnotationEditor.get(), &TextAnnotationEditor::updateRequested,
                this, &PinWindowAnnotationController::needsRepaint);
    }

    // Set default tool to Selection
    m_toolManager->setCurrentTool(ToolId::Selection);

    qDebug() << "PinWindowAnnotationController: Initialized";
}

PinWindowAnnotationController::~PinWindowAnnotationController() = default;

void PinWindowAnnotationController::setSourcePixmap(const QPixmap* pixmap)
{
    m_sourcePixmap = pixmap;
    m_toolManager->setSourcePixmap(pixmap);
}

void PinWindowAnnotationController::setDevicePixelRatio(qreal dpr)
{
    m_devicePixelRatio = dpr;
    m_toolManager->setDevicePixelRatio(dpr);
}

void PinWindowAnnotationController::setZoomLevel(qreal zoom)
{
    m_zoomLevel = zoom;
}

void PinWindowAnnotationController::setRotationAngle(int angle)
{
    m_rotationAngle = angle;
}

void PinWindowAnnotationController::setFlipState(bool horizontal, bool vertical)
{
    m_flipHorizontal = horizontal;
    m_flipVertical = vertical;
}

void PinWindowAnnotationController::setAnnotationMode(bool enabled)
{
    if (m_annotationMode == enabled) {
        return;
    }

    m_annotationMode = enabled;

    if (!m_annotationMode) {
        // Exit annotation mode - cancel any in-progress operations
        m_toolManager->cancelDrawing();
        if (m_textEditor && m_textEditor->isEditing()) {
            m_textEditor->cancelEditing();
        }
        m_annotationLayer->clearSelection();
    }

    emit annotationModeChanged(m_annotationMode);
    emit needsRepaint();

    qDebug() << "PinWindowAnnotationController: Annotation mode" << (enabled ? "enabled" : "disabled");
}

void PinWindowAnnotationController::setCurrentTool(int toolId)
{
    m_toolManager->setCurrentTool(static_cast<ToolId>(toolId));
}

int PinWindowAnnotationController::currentTool() const
{
    return static_cast<int>(m_toolManager->currentTool());
}

bool PinWindowAnnotationController::isDrawing() const
{
    return m_toolManager->isDrawing();
}

void PinWindowAnnotationController::setColor(const QColor& color)
{
    m_toolManager->setColor(color);
    m_colorAndWidthWidget->setCurrentColor(color);

    // Save to settings
    AnnotationSettingsManager::instance().saveColor(color);
}

QColor PinWindowAnnotationController::color() const
{
    return m_toolManager->color();
}

void PinWindowAnnotationController::setWidth(int width)
{
    m_toolManager->setWidth(width);
    m_colorAndWidthWidget->setCurrentWidth(width);

    // Save to settings
    AnnotationSettingsManager::instance().saveWidth(width);
}

int PinWindowAnnotationController::width() const
{
    return m_toolManager->width();
}

QPoint PinWindowAnnotationController::screenToImage(const QPoint& screenPos) const
{
    // Convert from screen coordinates to image coordinates
    // Account for zoom, rotation, and flip

    QPointF pos = QPointF(screenPos);

    // Reverse zoom
    pos /= m_zoomLevel;

    // Get image size (after rotation)
    if (!m_sourcePixmap) {
        return pos.toPoint();
    }

    QSize imageSize = m_sourcePixmap->size() / m_sourcePixmap->devicePixelRatio();

    // For rotation, we need to handle width/height swap for 90/270 degrees
    QSize transformedSize = imageSize;
    if (m_rotationAngle == 90 || m_rotationAngle == 270) {
        transformedSize = QSize(imageSize.height(), imageSize.width());
    }

    // Reverse flip
    if (m_flipHorizontal) {
        pos.setX(transformedSize.width() - pos.x());
    }
    if (m_flipVertical) {
        pos.setY(transformedSize.height() - pos.y());
    }

    // Reverse rotation
    if (m_rotationAngle != 0) {
        QTransform transform;
        transform.translate(transformedSize.width() / 2.0, transformedSize.height() / 2.0);
        transform.rotate(-m_rotationAngle);
        transform.translate(-imageSize.width() / 2.0, -imageSize.height() / 2.0);
        pos = transform.map(pos);
    }

    return pos.toPoint();
}

QPoint PinWindowAnnotationController::imageToScreen(const QPoint& imagePos) const
{
    // Convert from image coordinates to screen coordinates
    QPointF pos = QPointF(imagePos);

    if (!m_sourcePixmap) {
        return (pos * m_zoomLevel).toPoint();
    }

    QSize imageSize = m_sourcePixmap->size() / m_sourcePixmap->devicePixelRatio();

    // Apply rotation
    if (m_rotationAngle != 0) {
        QTransform transform;
        transform.translate(imageSize.width() / 2.0, imageSize.height() / 2.0);
        transform.rotate(m_rotationAngle);
        if (m_rotationAngle == 90 || m_rotationAngle == 270) {
            transform.translate(-imageSize.height() / 2.0, -imageSize.width() / 2.0);
        } else {
            transform.translate(-imageSize.width() / 2.0, -imageSize.height() / 2.0);
        }
        pos = transform.map(pos);
    }

    // Get size after rotation
    QSize transformedSize = imageSize;
    if (m_rotationAngle == 90 || m_rotationAngle == 270) {
        transformedSize = QSize(imageSize.height(), imageSize.width());
    }

    // Apply flip
    if (m_flipHorizontal) {
        pos.setX(transformedSize.width() - pos.x());
    }
    if (m_flipVertical) {
        pos.setY(transformedSize.height() - pos.y());
    }

    // Apply zoom
    pos *= m_zoomLevel;

    return pos.toPoint();
}

void PinWindowAnnotationController::setupPainterTransform(QPainter& painter) const
{
    // Set up transform to draw annotations in screen space
    // Annotations are stored in image coordinates, so we need to transform them

    if (!m_sourcePixmap) {
        painter.scale(m_zoomLevel, m_zoomLevel);
        return;
    }

    QSize imageSize = m_sourcePixmap->size() / m_sourcePixmap->devicePixelRatio();
    QSize transformedSize = imageSize;
    if (m_rotationAngle == 90 || m_rotationAngle == 270) {
        transformedSize = QSize(imageSize.height(), imageSize.width());
    }

    // Apply transforms in reverse order
    painter.scale(m_zoomLevel, m_zoomLevel);

    // Apply flip
    if (m_flipHorizontal || m_flipVertical) {
        painter.translate(transformedSize.width() / 2.0, transformedSize.height() / 2.0);
        painter.scale(m_flipHorizontal ? -1 : 1, m_flipVertical ? -1 : 1);
        painter.translate(-transformedSize.width() / 2.0, -transformedSize.height() / 2.0);
    }

    // Apply rotation
    if (m_rotationAngle != 0) {
        painter.translate(transformedSize.width() / 2.0, transformedSize.height() / 2.0);
        painter.rotate(m_rotationAngle);
        painter.translate(-imageSize.width() / 2.0, -imageSize.height() / 2.0);
    }
}

void PinWindowAnnotationController::handleMousePress(const QPoint& screenPos)
{
    if (!m_annotationMode) {
        return;
    }

    // Handle text editor click
    if (m_textEditor && m_textEditor->isEditing()) {
        if (m_textEditor->isConfirmMode() && !m_textEditor->contains(screenPos)) {
            // Click outside text editor in confirm mode - finish editing
            m_textEditor->finishEditing();
            return;
        }
        if (m_textEditor->contains(screenPos)) {
            m_textEditor->handleMousePress(screenPos);
            return;
        }
    }

    // Convert to image coordinates
    QPoint imagePos = screenToImage(screenPos);

    // Handle Text tool specially
    if (m_toolManager->currentTool() == ToolId::Text) {
        if (m_textAnnotationEditor && !m_textEditor->isEditing()) {
            QRect bounds = m_parentWidget ? m_parentWidget->rect() : QRect();
            m_textAnnotationEditor->startEditing(screenPos, bounds, m_toolManager->color());
        }
        return;
    }

    // Dispatch to tool manager
    m_toolManager->handleMousePress(imagePos);
    emit needsRepaint();
}

void PinWindowAnnotationController::handleMouseMove(const QPoint& screenPos)
{
    if (!m_annotationMode) {
        return;
    }

    // Handle text editor drag
    if (m_textEditor && m_textEditor->isDragging()) {
        m_textEditor->handleMouseMove(screenPos);
        return;
    }

    // Convert to image coordinates
    QPoint imagePos = screenToImage(screenPos);

    // Dispatch to tool manager
    m_toolManager->handleMouseMove(imagePos);
    emit needsRepaint();
}

void PinWindowAnnotationController::handleMouseRelease(const QPoint& screenPos)
{
    if (!m_annotationMode) {
        return;
    }

    // Handle text editor release
    if (m_textEditor && m_textEditor->isDragging()) {
        m_textEditor->handleMouseRelease(screenPos);
        return;
    }

    // Convert to image coordinates
    QPoint imagePos = screenToImage(screenPos);

    // Dispatch to tool manager
    m_toolManager->handleMouseRelease(imagePos);
    emit needsRepaint();
}

void PinWindowAnnotationController::handleDoubleClick(const QPoint& screenPos)
{
    if (!m_annotationMode) {
        return;
    }

    // Convert to image coordinates
    QPoint imagePos = screenToImage(screenPos);

    // Dispatch to tool manager
    m_toolManager->handleDoubleClick(imagePos);
    emit needsRepaint();
}

void PinWindowAnnotationController::draw(QPainter& painter) const
{
    if (m_annotationLayer->isEmpty() && !m_toolManager->isDrawing()) {
        return;
    }

    painter.save();

    // Set up transform for drawing annotations
    setupPainterTransform(painter);

    // Draw annotations
    m_annotationLayer->draw(painter);

    // Draw current tool preview
    if (m_toolManager->isDrawing()) {
        m_toolManager->drawCurrentPreview(painter);
    }

    painter.restore();
}

void PinWindowAnnotationController::drawOntoPixmap(QPixmap& pixmap) const
{
    if (m_annotationLayer->isEmpty()) {
        return;
    }

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // Draw annotations directly onto the pixmap (no transform needed since
    // annotations are stored in image coordinates)
    m_annotationLayer->draw(painter);
}

bool PinWindowAnnotationController::isTextEditing() const
{
    return m_textEditor && m_textEditor->isEditing();
}

void PinWindowAnnotationController::cancelTextEditing()
{
    if (m_textEditor && m_textEditor->isEditing()) {
        m_textEditor->cancelEditing();
    }
}

void PinWindowAnnotationController::undo()
{
    m_annotationLayer->undo();
    emit undoRedoStateChanged();
    emit needsRepaint();
}

void PinWindowAnnotationController::redo()
{
    m_annotationLayer->redo();
    emit undoRedoStateChanged();
    emit needsRepaint();
}

bool PinWindowAnnotationController::canUndo() const
{
    return m_annotationLayer->canUndo();
}

bool PinWindowAnnotationController::canRedo() const
{
    return m_annotationLayer->canRedo();
}

bool PinWindowAnnotationController::hasAnnotations() const
{
    return !m_annotationLayer->isEmpty();
}
