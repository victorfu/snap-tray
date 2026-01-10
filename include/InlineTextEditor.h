#ifndef INLINETEXTEDITOR_H
#define INLINETEXTEDITOR_H

#include <QObject>
#include <QPoint>
#include <QRect>
#include <QColor>
#include <QFont>

class QWidget;
class QTextEdit;
class QMouseEvent;

/**
 * @brief Inline text editing overlay for annotation text input.
 *
 * Wraps QTextEdit with positioning, auto-resize, and styling.
 */
class InlineTextEditor : public QObject
{
    Q_OBJECT

public:
    explicit InlineTextEditor(QWidget* parent);
    ~InlineTextEditor();

    /**
     * @brief Start editing at the given position.
     * @param pos Position to start editing
     * @param bounds Bounding rect to constrain the editor
     */
    void startEditing(const QPoint& pos, const QRect& bounds);

    /**
     * @brief Start editing existing text (for re-editing).
     * @param pos Position for the text
     * @param bounds Bounding rect to constrain the editor
     * @param existingText The existing text to edit
     */
    void startEditingExisting(const QPoint& pos, const QRect& bounds, const QString& existingText);

    /**
     * @brief Finish editing and return the text.
     * @return The edited text, or empty if cancelled
     */
    QString finishEditing();

    /**
     * @brief Cancel editing without returning text.
     */
    void cancelEditing();

    /**
     * @brief Check if currently editing (typing or confirming).
     */
    bool isEditing() const { return m_isEditing; }

    /**
     * @brief Check if in confirm/drag mode.
     */
    bool isConfirmMode() const { return m_isConfirmMode; }

    /**
     * @brief Check if currently dragging.
     */
    bool isDragging() const { return m_isDragging; }

    /**
     * @brief Enter confirm mode (allows dragging, click outside to finish).
     */
    void enterConfirmMode();

    /**
     * @brief Get the position where text was placed.
     */
    QPoint textPosition() const { return m_textPosition; }

    /**
     * @brief Set the text color.
     */
    void setColor(const QColor& color);

    /**
     * @brief Get the current color.
     */
    QColor color() const { return m_color; }

    /**
     * @brief Set the font.
     */
    void setFont(const QFont& font);

    /**
     * @brief Get the current font.
     */
    QFont font() const { return m_font; }

    // Text formatting methods
    /**
     * @brief Set bold style.
     */
    void setBold(bool enabled);

    /**
     * @brief Set italic style.
     */
    void setItalic(bool enabled);

    /**
     * @brief Set underline style.
     */
    void setUnderline(bool enabled);

    /**
     * @brief Set font size.
     */
    void setFontSize(int size);

    /**
     * @brief Set font family.
     */
    void setFontFamily(const QString& family);

    /**
     * @brief Get the underlying QTextEdit for event filtering.
     */
    QTextEdit* textEdit() const { return m_textEdit; }

    /**
     * @brief Check if the given position is inside the text editor.
     */
    bool contains(const QPoint& pos) const;

    /**
     * @brief Handle mouse events for dragging.
     */
    void handleMousePress(const QPoint& pos);
    void handleMouseMove(const QPoint& pos);
    void handleMouseRelease(const QPoint& pos);

signals:
    /**
     * @brief Emitted when editing is finished with text.
     */
    void editingFinished(const QString& text, const QPoint& position);

    /**
     * @brief Emitted when editing is cancelled.
     */
    void editingCancelled();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onContentsChanged();

private:
    void startEditingInternal(const QPoint& pos, const QRect& bounds, const QString& existingText);
    void updateStyle();
    void adjustSize();

    QWidget* m_parentWidget;
    QTextEdit* m_textEdit;
    bool m_isEditing;
    bool m_isConfirmMode;
    bool m_isDragging;
    QPoint m_textPosition;
    QPoint m_dragStartPos;
    QPoint m_dragStartTextPos;
    QRect m_bounds;
    QColor m_color;
    QFont m_font;

    static const int MIN_WIDTH = 150;
    static const int MIN_HEIGHT = 36;
    static const int PADDING = 6;
};

#endif // INLINETEXTEDITOR_H
