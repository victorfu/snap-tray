#ifndef PINWINDOWTOOLBAR_H
#define PINWINDOWTOOLBAR_H

#include <QWidget>
#include <QVector>
#include <QColor>
#include "tools/ToolId.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/LineStyle.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/StepBadgeAnnotation.h"
#include "ui/sections/MosaicBlurTypeSection.h"

class ToolbarWidget;
class ColorAndWidthWidget;
class QPaintEvent;
class QMouseEvent;
class QWheelEvent;

/**
 * @brief Floating toolbar for PinWindow annotations.
 *
 * Provides tool selection buttons and color/width settings for
 * annotating pinned screenshots.
 *
 * Supported tools:
 * - Shape (Rectangle/Ellipse)
 * - Arrow
 * - Pencil
 * - Marker
 * - Text
 * - Mosaic
 * - Step Badge
 * - Eraser
 * - Undo/Redo
 */
class PinWindowToolbar : public QWidget
{
    Q_OBJECT

public:
    explicit PinWindowToolbar(QWidget* parent = nullptr);
    ~PinWindowToolbar() override;

    /**
     * @brief Set the current active tool.
     */
    void setCurrentTool(ToolId tool);

    /**
     * @brief Get the current active tool.
     */
    ToolId currentTool() const { return m_currentTool; }

    /**
     * @brief Set the current color.
     */
    void setColor(const QColor& color);

    /**
     * @brief Get the current color.
     */
    QColor color() const;

    /**
     * @brief Set the current width.
     */
    void setWidth(int width);

    /**
     * @brief Get the current width.
     */
    int width() const;

    /**
     * @brief Set undo/redo availability.
     */
    void setUndoEnabled(bool enabled);
    void setRedoEnabled(bool enabled);

    /**
     * @brief Position the toolbar relative to the parent window.
     * @param parentRect The parent window's rect
     */
    void positionRelativeTo(const QRect& parentRect);

    /**
     * @brief Show/hide the color and width widget.
     */
    void setColorWidthWidgetVisible(bool visible);

signals:
    /**
     * @brief Emitted when a tool is selected.
     */
    void toolSelected(ToolId tool);

    /**
     * @brief Emitted when color changes.
     */
    void colorChanged(const QColor& color);

    /**
     * @brief Emitted when width changes.
     */
    void widthChanged(int width);

    /**
     * @brief Emitted when undo is requested.
     */
    void undoRequested();

    /**
     * @brief Emitted when redo is requested.
     */
    void redoRequested();

    /**
     * @brief Emitted when close/done is requested.
     */
    void closeRequested();

    /**
     * @brief Emitted when arrow style changes.
     */
    void arrowStyleChanged(LineEndStyle style);

    /**
     * @brief Emitted when line style changes.
     */
    void lineStyleChanged(LineStyle style);

    /**
     * @brief Emitted when shape type changes.
     */
    void shapeTypeChanged(ShapeType type);

    /**
     * @brief Emitted when shape fill mode changes.
     */
    void shapeFillModeChanged(ShapeFillMode mode);

    /**
     * @brief Emitted when step badge size changes.
     */
    void stepBadgeSizeChanged(StepBadgeSize size);

    /**
     * @brief Emitted when mosaic blur type changes.
     */
    void mosaicBlurTypeChanged(MosaicBlurTypeSection::BlurType type);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void setupUI();
    void setupToolbar();
    void setupColorWidthWidget();
    void connectSignals();
    void updateToolbarButtons();
    void updateColorWidthVisibility();
    QColor getIconColor(int buttonId, bool isActive, bool isHovered) const;

    // Components
    ToolbarWidget* m_toolbar;
    ColorAndWidthWidget* m_colorAndWidthWidget;

    // State
    ToolId m_currentTool = ToolId::Pencil;
    bool m_undoEnabled = false;
    bool m_redoEnabled = false;

    // Dragging
    bool m_isDragging = false;
    QPoint m_dragStartPos;

    // Layout
    static constexpr int kToolbarSpacing = 8;
    static constexpr int kMargin = 10;
};

#endif // PINWINDOWTOOLBAR_H
