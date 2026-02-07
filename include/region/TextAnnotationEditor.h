#ifndef TEXTANNOTATIONEDITOR_H
#define TEXTANNOTATIONEDITOR_H

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QColor>
#include <functional>
#include "TextFormattingState.h"
#include "TransformationGizmo.h"

class AnnotationLayer;
class InlineTextEditor;
class ToolOptionsPanel;
class QWidget;

/**
 * @brief Manages text annotation editing and transformation operations.
 *
 * This class encapsulates all text annotation editing functionality including:
 * - Creating and editing text annotations
 * - Text transformation (rotation, scaling, moving)
 * - Text formatting (bold, italic, underline, font size, font family)
 * - Settings persistence
 * - Double-click detection for re-editing
 */
class TextAnnotationEditor : public QObject
{
    Q_OBJECT

public:
    explicit TextAnnotationEditor(QObject* parent = nullptr);
    ~TextAnnotationEditor() override = default;

    // Configuration constants
    static constexpr qint64 kDoubleClickInterval = 500;  // ms
    static constexpr int kDoubleClickDistance = 5;       // pixels

    // Initialization
    void setAnnotationLayer(AnnotationLayer* layer);
    void setTextEditor(InlineTextEditor* editor);
    void setColorAndWidthWidget(ToolOptionsPanel* widget);
    void setParentWidget(QWidget* widget);
    void setCoordinateMappers(const std::function<QPointF(const QPointF&)>& displayToAnnotation,
                              const std::function<QPointF(const QPointF&)>& annotationToDisplay);

    // Editing operations
    void startEditing(const QPoint& pos, const QRect& selectionRect, const QColor& color);
    void startReEditing(int annotationIndex, const QColor& color);
    bool finishEditing(const QString& text, const QPoint& position, const QColor& color);
    void cancelEditing();
    bool isEditing() const;
    int editingIndex() const { return m_editingIndex; }

    // Transformation operations
    void startTransformation(const QPoint& pos, GizmoHandle handle);
    void updateTransformation(const QPoint& pos);
    void finishTransformation();
    bool isTransforming() const { return m_isTransforming; }
    GizmoHandle activeHandle() const { return m_activeGizmoHandle; }

    // Dragging operations (for Body handle or direct text annotation drag)
    void startDragging(const QPoint& pos);
    void updateDragging(const QPoint& pos);
    void finishDragging();
    bool isDragging() const { return m_isDragging; }

    // Formatting operations
    void setBold(bool enabled);
    void setItalic(bool enabled);
    void setUnderline(bool enabled);
    void setFontSize(int size);
    void setFontFamily(const QString& family);
    TextFormattingState formatting() const { return m_formatting; }

    // Settings persistence
    void loadSettings();
    void saveSettings();

    // Double-click detection
    bool isDoubleClick(const QPoint& pos, qint64 currentTime) const;
    void recordClick(const QPoint& pos, qint64 time);

signals:
    void editingFinished();
    void editingCancelled();
    void transformationUpdated();
    void formattingChanged();
    void updateRequested();

private:
    // Dependencies
    AnnotationLayer* m_annotationLayer = nullptr;
    InlineTextEditor* m_textEditor = nullptr;
    ToolOptionsPanel* m_colorAndWidthWidget = nullptr;
    QWidget* m_parentWidget = nullptr;

    // Transformation state
    GizmoHandle m_activeGizmoHandle = GizmoHandle::None;
    bool m_isTransforming = false;
    QPointF m_transformStartCenter;
    qreal m_transformStartRotation = 0.0;
    qreal m_transformStartAngle = 0.0;
    qreal m_transformStartScale = 1.0;
    qreal m_transformStartDistance = 0.0;
    QPoint m_dragStart;

    // Dragging state
    bool m_isDragging = false;

    // Editing state
    TextFormattingState m_formatting;
    int m_editingIndex = -1;

    // Double-click detection
    QPoint m_lastClickPos;
    qint64 m_lastClickTime = 0;

    // Coordinate mapping between editor display coordinates and annotation coordinates.
    std::function<QPointF(const QPointF&)> m_displayToAnnotationMapper;
    std::function<QPointF(const QPointF&)> m_annotationToDisplayMapper;
};

#endif // TEXTANNOTATIONEDITOR_H
