#ifndef SCREENCANVAS_H
#define SCREENCANVAS_H

#include <QWidget>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QVector>
#include <QColor>
#include <QPointer>
#include "annotations/AnnotationLayer.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/LineStyle.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/StepBadgeAnnotation.h"
#include "InlineTextEditor.h"
#include "region/TextAnnotationEditor.h"
#include "tools/ToolId.h"
#include "ToolbarStyle.h"

class QScreen;
class AnnotationLayer;
class ToolManager;
class ColorPickerDialog;
class EmojiPicker;
class ColorAndWidthWidget;
class LaserPointerRenderer;
class ToolbarWidget;

// Canvas background mode
enum class CanvasBackgroundMode {
    Screen,     // Screenshot background (default)
    Whiteboard, // Solid white background
    Blackboard  // Solid black background
};

// Canvas toolbar button IDs (for UI display only)
// These represent button positions in the toolbar
enum class CanvasButton {
    Pencil = 0,
    Marker,
    Arrow,
    Shape,          // Unified Rectangle/Ellipse
    StepBadge,
    Text,
    EmojiSticker,
    LaserPointer,
    Whiteboard,
    Blackboard,
    Undo,
    Redo,
    Clear,
    Exit,
    Count  // Total number of buttons
};

// Helper to map CanvasButton to ToolId
inline ToolId canvasButtonToToolId(CanvasButton btn) {
    switch (btn) {
    case CanvasButton::Pencil:       return ToolId::Pencil;
    case CanvasButton::Marker:       return ToolId::Marker;
    case CanvasButton::Arrow:        return ToolId::Arrow;
    case CanvasButton::Shape:        return ToolId::Shape;
    case CanvasButton::StepBadge:    return ToolId::StepBadge;
    case CanvasButton::Text:         return ToolId::Text;
    case CanvasButton::EmojiSticker: return ToolId::EmojiSticker;
    case CanvasButton::LaserPointer: return ToolId::LaserPointer;
    case CanvasButton::Whiteboard:   return ToolId::Selection;  // Not a tool
    case CanvasButton::Blackboard:   return ToolId::Selection;  // Not a tool
    case CanvasButton::Undo:         return ToolId::Undo;
    case CanvasButton::Redo:         return ToolId::Redo;
    case CanvasButton::Clear:        return ToolId::Clear;
    case CanvasButton::Exit:         return ToolId::Exit;
    default:                         return ToolId::Selection;
    }
}

class ScreenCanvas : public QWidget
{
    Q_OBJECT

public:
    explicit ScreenCanvas(QWidget *parent = nullptr);
    ~ScreenCanvas();

    void initializeForScreen(QScreen *screen);

signals:
    void closed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    // Painting helpers
    void drawAnnotations(QPainter &painter);
    void drawCurrentAnnotation(QPainter &painter);
    void drawCursorDot(QPainter &painter);

    // Toolbar helpers
    void initializeIcons();
    void setupToolbar();
    void updateToolbarPosition();
    QString getIconKeyForButton(CanvasButton button) const;
    void renderIcon(QPainter &painter, const QRect &rect, CanvasButton button, const QColor &color);
    void handleToolbarClick(CanvasButton button);
    bool isDrawingTool(ToolId toolId) const;
    QColor getButtonIconColor(int buttonId) const;
    void setToolCursor();

    // Color palette helpers (legacy)
    bool shouldShowColorPalette() const;
    void onColorSelected(const QColor &color);
    void onMoreColorsRequested();

    // Line width callback
    void onLineWidthChanged(int width);

    // Unified color and width widget helpers
    bool shouldShowColorAndWidthWidget() const;
    bool shouldShowWidthControl() const;

    // Annotation settings persistence
    LineEndStyle loadArrowStyle() const;
    void saveArrowStyle(LineEndStyle style);
    void onArrowStyleChanged(LineEndStyle style);
    LineStyle loadLineStyle() const;
    void saveLineStyle(LineStyle style);
    void onLineStyleChanged(LineStyle style);

    // Text editing handlers
    void onTextEditingFinished(const QString &text, const QPoint &position);

    // Background pixmap (used for Whiteboard/Blackboard modes)
    QPixmap m_backgroundPixmap;
    QPointer<QScreen> m_currentScreen;
    qreal m_devicePixelRatio;

    // Annotation layer and tool manager
    AnnotationLayer *m_annotationLayer;
    ToolManager *m_toolManager;
    ToolId m_currentToolId;

    // Toolbar using ToolbarWidget
    ToolbarWidget *m_toolbar;
    bool m_isDraggingToolbar;
    bool m_toolbarManuallyPositioned;
    QPoint m_toolbarDragOffset;
    bool m_showSubToolbar;

    // Unified color and width widget
    ColorAndWidthWidget *m_colorAndWidthWidget;

    // Emoji picker
    EmojiPicker *m_emojiPicker;

    // Color picker dialog
    ColorPickerDialog *m_colorPickerDialog;

    // Laser pointer renderer
    LaserPointerRenderer *m_laserRenderer;

    // Cursor position for drawing cursor dot
    QPoint m_cursorPos;

    // Toolbar style configuration
    ToolbarStyleConfig m_toolbarStyleConfig;

    // Shape tool state
    ShapeType m_shapeType = ShapeType::Rectangle;
    ShapeFillMode m_shapeFillMode = ShapeFillMode::Outline;

    // StepBadge tool state
    StepBadgeSize m_stepBadgeSize = StepBadgeSize::Medium;

    // Text editing components
    InlineTextEditor *m_textEditor;
    TextAnnotationEditor *m_textAnnotationEditor;

    // Background mode
    CanvasBackgroundMode m_bgMode = CanvasBackgroundMode::Screen;

    // Background mode helpers
    void setBackgroundMode(CanvasBackgroundMode mode);
    QPixmap createSolidBackgroundPixmap(const QColor& color) const;
};

#endif // SCREENCANVAS_H
