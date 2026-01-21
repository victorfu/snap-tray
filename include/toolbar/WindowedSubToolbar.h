#ifndef WINDOWEDSUBTOOLBAR_H
#define WINDOWEDSUBTOOLBAR_H

#include <QWidget>
#include <QRect>
#include "annotations/StepBadgeAnnotation.h"  // For StepBadgeSize enum
#include "annotations/ShapeAnnotation.h"      // For ShapeType and ShapeFillMode
#include "annotations/ArrowAnnotation.h"      // For LineEndStyle
#include "annotations/LineStyle.h"            // For LineStyle

class ToolOptionsPanel;
class EmojiPicker;

/**
 * @brief Floating sub-toolbar for PinWindow annotation tools.
 *
 * Wraps ToolOptionsPanel and EmojiPicker in a floating frameless window,
 * similar to WindowedToolbar. This provides tool-specific options like
 * color selection, line width, shape type, step badge size, etc.
 */
class WindowedSubToolbar : public QWidget
{
    Q_OBJECT

public:
    explicit WindowedSubToolbar(QWidget *parent = nullptr);
    ~WindowedSubToolbar();

    // Position the sub-toolbar relative to the main toolbar
    void positionBelow(const QRect &toolbarRect);

    // Visibility control
    void showForTool(int toolId);
    void hide();

    // Color and width access
    ToolOptionsPanel* colorAndWidthWidget() const { return m_colorAndWidthWidget; }
    EmojiPicker* emojiPicker() const { return m_emojiPicker; }

    // Mode: sub-toolbar or emoji picker
    bool isShowingEmojiPicker() const { return m_showingEmojiPicker; }

signals:
    // Color/width signals
    void colorSelected(const QColor &color);
    void customColorPickerRequested();
    void widthChanged(int width);

    // Shape signals
    void shapeTypeChanged(ShapeType type);
    void shapeFillModeChanged(ShapeFillMode mode);

    // Arrow signals
    void arrowStyleChanged(LineEndStyle style);
    void lineStyleChanged(LineStyle style);

    // Step badge signals
    void stepBadgeSizeChanged(StepBadgeSize size);

    // Text signals
    void boldToggled(bool enabled);
    void italicToggled(bool enabled);
    void underlineToggled(bool enabled);
    void fontSizeDropdownRequested(const QPoint &pos);
    void fontFamilyDropdownRequested(const QPoint &pos);

    // Emoji signals
    void emojiSelected(const QString &emoji);

    // Auto blur signal
    void autoBlurRequested();

    // Cursor restore signal - emitted when mouse leaves sub-toolbar
    void cursorRestoreRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void leaveEvent(QEvent *event) override;

private slots:
    void onDropdownStateChanged();

private:
    void setupConnections();
    void updateSize();

    ToolOptionsPanel *m_colorAndWidthWidget;
    EmojiPicker *m_emojiPicker;
    bool m_showingEmojiPicker = false;

    // Match RegionSelector's 4px gap between toolbar and sub-toolbar
    static constexpr int MARGIN = 4;
};

#endif // WINDOWEDSUBTOOLBAR_H
