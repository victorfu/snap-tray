#ifndef WINDOWEDTOOLBAR_H
#define WINDOWEDTOOLBAR_H

#include <QWidget>
#include <QVector>
#include <QRect>
#include <QElapsedTimer>
#include "toolbar/ToolbarButtonConfig.h"
#include "tools/ToolId.h"

class WindowedToolbar : public QWidget
{
    Q_OBJECT

public:
    // Compatibility aliases; actual toolbar items come from ToolRegistry.
    enum ButtonId {
        // Tool buttons use ToolId values directly.
        ButtonPencil = static_cast<int>(ToolId::Pencil),
        ButtonMarker = static_cast<int>(ToolId::Marker),
        ButtonArrow = static_cast<int>(ToolId::Arrow),
        ButtonShape = static_cast<int>(ToolId::Shape),
        ButtonText = static_cast<int>(ToolId::Text),
        ButtonMosaic = static_cast<int>(ToolId::Mosaic),
        ButtonEraser = static_cast<int>(ToolId::Eraser),
        ButtonStepBadge = static_cast<int>(ToolId::StepBadge),
        ButtonEmoji = static_cast<int>(ToolId::EmojiSticker),
        ButtonUndo = static_cast<int>(ToolId::Undo),
        ButtonRedo = static_cast<int>(ToolId::Redo),
        ButtonOCR = static_cast<int>(ToolId::OCR),
        ButtonQRCode = static_cast<int>(ToolId::QRCode),
        ButtonCopy = static_cast<int>(ToolId::Copy),
        ButtonSave = static_cast<int>(ToolId::Save),

        // Local toolbar-only action.
        ButtonDone = 10001
    };

    explicit WindowedToolbar(QWidget *parent = nullptr);
    ~WindowedToolbar();

    void positionNear(const QRect &pinWindowRect);
    void setActiveButton(int buttonId);
    int activeButton() const { return m_activeButton; }

    // Undo/Redo state
    void setCanUndo(bool canUndo);
    void setCanRedo(bool canRedo);

    // OCR availability
    void setOCRAvailable(bool available);

    // Associated widgets for click-outside detection
    void setAssociatedWidgets(QWidget *window, QWidget *subToolbar);

signals:
    // Close request from click-outside detection
    void closeRequested();
    // Drawing tool signals
    void toolSelected(int toolId);

    // Action signals
    void undoClicked();
    void redoClicked();
    void ocrClicked();
    void qrCodeClicked();
    void copyClicked();
    void saveClicked();
    void doneClicked();

    // Cursor restore signal - emitted when mouse leaves toolbar
    void cursorRestoreRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    using ButtonConfig = Toolbar::ButtonConfig;

    void setupUi();
    void loadIcons();
    void updateButtonLayout();
    int buttonAtPosition(const QPoint &pos) const;
    void drawButton(QPainter &painter, int index);
    void updateHoverTooltip();
    void hideHoverTooltip();
    bool isButtonDisabled(const ButtonConfig& config) const;
    bool isDrawingTool(int buttonId) const;
    bool dispatchActionButton(int buttonId);

    QVector<ButtonConfig> m_buttons;
    QVector<QRect> m_buttonRects;
    int m_activeButton = -1;
    int m_hoveredButton = -1;

    bool m_canUndo = false;
    bool m_canRedo = false;
    bool m_ocrAvailable = true;

    // Drag support
    QPoint m_dragStartPos;
    QPoint m_dragStartWidgetPos;
    bool m_isDragging = false;

    // Click-outside detection
    QElapsedTimer m_showTime;
    QWidget *m_associatedWindow = nullptr;
    QWidget *m_subToolbar = nullptr;
    QWidget *m_hoverTooltip = nullptr;

    // Match RegionSelector's ToolbarCore dimensions
    static constexpr int TOOLBAR_HEIGHT = 32;
    static constexpr int BUTTON_WIDTH = 28;
    static constexpr int BUTTON_HEIGHT = 24;
    static constexpr int BUTTON_SPACING = 2;
    static constexpr int SEPARATOR_WIDTH = 8;
    static constexpr int MARGIN = 8;
};

#endif // WINDOWEDTOOLBAR_H
