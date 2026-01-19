#include "pinwindow/PinWindowToolbar.h"
#include "ToolbarStyle.h"
#include "IconRenderer.h"
#include "toolbar/ToolbarRenderer.h"
#include "cursor/CursorManager.h"

#include <QPainter>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QApplication>
#include <QShowEvent>
#include <QHideEvent>

PinWindowToolbar::PinWindowToolbar(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
#ifdef Q_OS_MACOS
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
#endif
    setMouseTracking(true);
    setFixedHeight(TOOLBAR_HEIGHT);

    loadIcons();
    setupUi();
    updateButtonLayout();
}

PinWindowToolbar::~PinWindowToolbar()
{
}

void PinWindowToolbar::loadIcons()
{
    auto& renderer = IconRenderer::instance();
    // Drawing tools
    renderer.loadIcon("pencil", ":/icons/icons/pencil.svg");
    renderer.loadIcon("marker", ":/icons/icons/marker.svg");
    renderer.loadIcon("arrow", ":/icons/icons/arrow.svg");
    renderer.loadIcon("shape", ":/icons/icons/shape.svg");
    renderer.loadIcon("text", ":/icons/icons/text.svg");
    renderer.loadIcon("mosaic", ":/icons/icons/mosaic.svg");
    renderer.loadIcon("eraser", ":/icons/icons/eraser.svg");
    renderer.loadIcon("step-badge", ":/icons/icons/step-badge.svg");
    renderer.loadIcon("emoji", ":/icons/icons/emoji.svg");

    // History
    renderer.loadIcon("undo", ":/icons/icons/undo.svg");
    renderer.loadIcon("redo", ":/icons/icons/redo.svg");

    // Export
    renderer.loadIcon("ocr", ":/icons/icons/ocr.svg");
    renderer.loadIcon("copy", ":/icons/icons/copy.svg");
    renderer.loadIcon("save", ":/icons/icons/save.svg");

    // Close
    renderer.loadIcon("done", ":/icons/icons/done.svg");
}

void PinWindowToolbar::setupUi()
{
    // Configure buttons using Toolbar::ButtonConfig builder pattern
    // Order matches RegionSelector: Shape → Arrow → Pencil → Marker → Text → ...
    m_buttons = {
        // Drawing tools (order matches RegionSelector toolbar)
        ButtonConfig(ButtonShape, "shape", "Shape"),
        ButtonConfig(ButtonArrow, "arrow", "Arrow"),
        ButtonConfig(ButtonPencil, "pencil", "Pencil"),
        ButtonConfig(ButtonMarker, "marker", "Marker"),
        ButtonConfig(ButtonText, "text", "Text"),
        ButtonConfig(ButtonMosaic, "mosaic", "Mosaic"),
        ButtonConfig(ButtonEraser, "eraser", "Eraser"),
        ButtonConfig(ButtonStepBadge, "step-badge", "Step Badge"),
        ButtonConfig(ButtonEmoji, "emoji", "Emoji"),

        // History (with separator)
        ButtonConfig(ButtonUndo, "undo", "Undo (Ctrl+Z)").separator(),
        ButtonConfig(ButtonRedo, "redo", "Redo (Ctrl+Y)"),

        // Export (with separator)
        ButtonConfig(ButtonOCR, "ocr", "OCR Text Recognition").separator(),
        ButtonConfig(ButtonSave, "save", "Save (Ctrl+S)"),
        ButtonConfig(ButtonCopy, "copy", "Copy (Ctrl+C)"),

        // Close (with separator)
        ButtonConfig(ButtonDone, "done", "Done (Space/Esc)").separator()
    };

    m_buttonRects.resize(m_buttons.size());
}

void PinWindowToolbar::updateButtonLayout()
{
    int x = MARGIN;
    int buttonY = (TOOLBAR_HEIGHT - BUTTON_HEIGHT) / 2;

    for (int i = 0; i < m_buttons.size(); ++i) {
        const ButtonConfig& config = m_buttons[i];

        // Skip OCR button if not available
        if (config.id == ButtonOCR && !m_ocrAvailable) {
            m_buttonRects[i] = QRect();
            continue;
        }

        // Add separator space before button if configured
        if (config.separatorBefore && i > 0) {
            x += SEPARATOR_WIDTH;
        }

        m_buttonRects[i] = QRect(x, buttonY, BUTTON_WIDTH, BUTTON_HEIGHT);
        x += BUTTON_WIDTH + BUTTON_SPACING;
    }

    // Calculate total width and resize
    int totalWidth = x - BUTTON_SPACING + MARGIN;
    setFixedWidth(totalWidth);
}

void PinWindowToolbar::positionNear(const QRect &pinWindowRect)
{
    qDebug() << "PinWindowToolbar::positionNear - pinWindowRect:" << pinWindowRect
             << "toolbar size:" << size();

    QScreen *screen = QGuiApplication::screenAt(pinWindowRect.center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }

    QRect screenGeom = screen->geometry();
    qDebug() << "PinWindowToolbar: screenGeom:" << screenGeom;

    // Position below the pin window, right-aligned (same as RegionSelector toolbar)
    int x = pinWindowRect.right() - width() + 1;
    int y = pinWindowRect.bottom() + MARGIN;
    qDebug() << "PinWindowToolbar: calculated x:" << x << "y:" << y;

    // If toolbar would go off bottom, position above
    if (y + height() > screenGeom.bottom() - 10) {
        y = pinWindowRect.top() - height() - MARGIN;
    }

    // If still off screen (above), position inside at bottom
    if (y < screenGeom.top() + 10) {
        y = pinWindowRect.bottom() - height() - 10;
    }

    // Keep on screen horizontally
    x = qBound(screenGeom.left() + 10, x, screenGeom.right() - width() - 10);

    move(x, y);
}

void PinWindowToolbar::setActiveButton(int buttonId)
{
    m_activeButton = buttonId;
    update();
}

void PinWindowToolbar::setCanUndo(bool canUndo)
{
    m_canUndo = canUndo;
    update();
}

void PinWindowToolbar::setCanRedo(bool canRedo)
{
    m_canRedo = canRedo;
    update();
}

void PinWindowToolbar::setOCRAvailable(bool available)
{
    if (m_ocrAvailable != available) {
        m_ocrAvailable = available;
        updateButtonLayout();
        update();
    }
}

bool PinWindowToolbar::isDrawingTool(int buttonId) const
{
    return buttonId >= ButtonPencil && buttonId <= ButtonEmoji;
}

void PinWindowToolbar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    ToolbarStyleConfig styleConfig = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());

    QRect bgRect = rect();

    // Draw background with gradient
    QLinearGradient gradient(bgRect.topLeft(), bgRect.bottomLeft());
    gradient.setColorAt(0, styleConfig.backgroundColorTop);
    gradient.setColorAt(1, styleConfig.backgroundColorBottom);

    painter.setBrush(gradient);
    painter.setPen(QPen(styleConfig.borderColor, 1));
    painter.drawRoundedRect(bgRect, 8, 8);

    // Draw separators
    painter.setPen(QPen(styleConfig.separatorColor, 1));
    for (int i = 0; i < m_buttons.size(); ++i) {
        if (m_buttons[i].separatorBefore && !m_buttonRects[i].isNull() && i > 0) {
            int sepX = m_buttonRects[i].left() - SEPARATOR_WIDTH / 2 - BUTTON_SPACING / 2;
            int sepY1 = (TOOLBAR_HEIGHT - 16) / 2;
            int sepY2 = sepY1 + 16;
            painter.drawLine(sepX, sepY1, sepX, sepY2);
        }
    }

    // Draw buttons
    for (int i = 0; i < m_buttons.size(); ++i) {
        if (!m_buttonRects[i].isNull()) {
            drawButton(painter, i);
        }
    }

    // Draw tooltip if hovering
    if (m_hoveredButton >= 0 && m_hoveredButton < m_buttons.size() && !m_buttonRects[m_hoveredButton].isNull()) {
        drawTooltip(painter);
    }
}

void PinWindowToolbar::drawButton(QPainter &painter, int index)
{
    ToolbarStyleConfig styleConfig = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
    const ButtonConfig &config = m_buttons[index];
    QRect btnRect = m_buttonRects[index];
    bool isHovered = (index == m_hoveredButton);
    bool isActive = (config.id == m_activeButton);
    bool isDisabled = false;

    // Check disabled state for undo/redo
    if (config.id == ButtonUndo && !m_canUndo) {
        isDisabled = true;
    } else if (config.id == ButtonRedo && !m_canRedo) {
        isDisabled = true;
    }

    // Draw active/hover background
    if (isActive) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(styleConfig.activeBackgroundColor);
        painter.drawRoundedRect(btnRect.adjusted(2, 2, -2, -2), 4, 4);
    } else if (isHovered && !isDisabled) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(styleConfig.hoverBackgroundColor);
        painter.drawRoundedRect(btnRect.adjusted(2, 2, -2, -2), 4, 4);
    }

    // Determine icon color
    QColor iconColor;
    if (isDisabled) {
        iconColor = styleConfig.iconNormalColor;
        iconColor.setAlpha(80);
    } else if (isActive) {
        iconColor = styleConfig.iconActiveColor;
    } else {
        iconColor = Toolbar::ToolbarRenderer::getIconColor(config, false, isHovered, styleConfig);
    }

    // Render icon (padding 8 matches RegionSelector's ToolbarWidget)
    IconRenderer::instance().renderIcon(painter, btnRect, config.iconKey, iconColor, 8);
}

void PinWindowToolbar::drawTooltip(QPainter &painter)
{
    ToolbarStyleConfig styleConfig = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
    QString tooltip = m_buttons[m_hoveredButton].tooltip;
    if (tooltip.isEmpty()) return;

    QFont font = painter.font();
    font.setPointSize(11);
    painter.setFont(font);

    QFontMetrics fm(font);
    QRect textRect = fm.boundingRect(tooltip);
    textRect.adjust(-8, -4, 8, 4);

    // Position tooltip above the button
    QRect btnRect = m_buttonRects[m_hoveredButton];
    int tooltipX = btnRect.center().x() - textRect.width() / 2;
    int tooltipY = -textRect.height() - 6;

    // Keep on screen horizontally
    if (tooltipX < 5) tooltipX = 5;
    if (tooltipX + textRect.width() > width() - 5) {
        tooltipX = width() - textRect.width() - 5;
    }

    textRect.moveTo(tooltipX, tooltipY);

    // Draw tooltip background
    painter.setPen(Qt::NoPen);
    painter.setBrush(styleConfig.tooltipBackground);
    painter.drawRoundedRect(textRect, 4, 4);

    // Draw tooltip border
    painter.setPen(styleConfig.tooltipBorder);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(textRect, 4, 4);

    // Draw tooltip text
    painter.setPen(styleConfig.tooltipText);
    painter.drawText(textRect, Qt::AlignCenter, tooltip);
}

int PinWindowToolbar::buttonAtPosition(const QPoint &pos) const
{
    for (int i = 0; i < m_buttonRects.size(); ++i) {
        if (!m_buttonRects[i].isNull() && m_buttonRects[i].contains(pos)) {
            return i;
        }
    }
    return -1;
}

void PinWindowToolbar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        int btnIndex = buttonAtPosition(event->pos());
        if (btnIndex >= 0) {
            const ButtonConfig& config = m_buttons[btnIndex];

            // Check if button is disabled
            if (config.id == ButtonUndo && !m_canUndo) {
                return;
            }
            if (config.id == ButtonRedo && !m_canRedo) {
                return;
            }

            // Handle button click
            switch (config.id) {
            // Drawing tools
            case ButtonPencil:
            case ButtonMarker:
            case ButtonArrow:
            case ButtonShape:
            case ButtonText:
            case ButtonMosaic:
            case ButtonEraser:
            case ButtonStepBadge:
            case ButtonEmoji:
                emit toolSelected(config.id);
                break;

            // History
            case ButtonUndo: emit undoClicked(); break;
            case ButtonRedo: emit redoClicked(); break;

            // Export
            case ButtonOCR: emit ocrClicked(); break;
            case ButtonCopy: emit copyClicked(); break;
            case ButtonSave: emit saveClicked(); break;

            // Close
            case ButtonDone: emit doneClicked(); break;

            default: break;
            }
            return;
        }

        // Start dragging
        m_isDragging = true;
        m_dragStartPos = event->globalPosition().toPoint();
        m_dragStartWidgetPos = pos();
    }
    QWidget::mousePressEvent(event);
}

void PinWindowToolbar::enterEvent(QEnterEvent *event)
{
    // Set initial cursor and hover state when mouse enters
    auto& cm = CursorManager::instance();
    int hovered = buttonAtPosition(event->position().toPoint());

    if (hovered != m_hoveredButton) {
        m_hoveredButton = hovered;
        update();
    }

    if (hovered >= 0) {
        cm.pushCursorForWidget(this, CursorContext::Hover, Qt::PointingHandCursor);
    } else {
        cm.pushCursorForWidget(this, CursorContext::Hover, Qt::OpenHandCursor);
    }
    QWidget::enterEvent(event);
}

void PinWindowToolbar::mouseMoveEvent(QMouseEvent *event)
{
    auto& cm = CursorManager::instance();

    if (m_isDragging) {
        QPoint delta = event->globalPosition().toPoint() - m_dragStartPos;
        move(m_dragStartWidgetPos + delta);
        cm.pushCursorForWidget(this, CursorContext::Drag, Qt::ClosedHandCursor);
        QWidget::mouseMoveEvent(event);
        return;
    }

    // Update hover state
    int newHovered = buttonAtPosition(event->pos());
    if (newHovered != m_hoveredButton) {
        m_hoveredButton = newHovered;
        update();
    }

    // Pop drag cursor if we're not dragging
    cm.popCursorForWidget(this, CursorContext::Drag);

    // Set hover cursor
    if (newHovered >= 0) {
        cm.pushCursorForWidget(this, CursorContext::Hover, Qt::PointingHandCursor);
    } else {
        cm.pushCursorForWidget(this, CursorContext::Hover, Qt::OpenHandCursor);
    }
    QWidget::mouseMoveEvent(event);
}

void PinWindowToolbar::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_isDragging = false;
    }
    QWidget::mouseReleaseEvent(event);
}

void PinWindowToolbar::leaveEvent(QEvent *event)
{
    if (m_hoveredButton >= 0) {
        m_hoveredButton = -1;
        update();
    }
    CursorManager::instance().clearAllForWidget(this);
    emit cursorRestoreRequested();
    QWidget::leaveEvent(event);
}

void PinWindowToolbar::setAssociatedWidgets(QWidget *window, QWidget *subToolbar)
{
    m_associatedWindow = window;
    m_subToolbar = subToolbar;
}

void PinWindowToolbar::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    qApp->installEventFilter(this);
    m_showTime.start();
}

void PinWindowToolbar::hideEvent(QHideEvent *event)
{
    qApp->removeEventFilter(this);
    QWidget::hideEvent(event);
}

bool PinWindowToolbar::eventFilter(QObject *obj, QEvent *event)
{
    // Ignore during first 300ms to prevent accidental close
    if (m_showTime.isValid() && m_showTime.elapsed() < 300) {
        return QWidget::eventFilter(obj, event);
    }

    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        QPoint globalPos = mouseEvent->globalPosition().toPoint();

        // Check if click is inside toolbar
        if (frameGeometry().contains(globalPos)) {
            return QWidget::eventFilter(obj, event);
        }

        // Check if click is inside sub-toolbar
        if (m_subToolbar && m_subToolbar->isVisible() &&
            m_subToolbar->frameGeometry().contains(globalPos)) {
            return QWidget::eventFilter(obj, event);
        }

        // Check if click is inside associated PinWindow
        if (m_associatedWindow && m_associatedWindow->frameGeometry().contains(globalPos)) {
            return QWidget::eventFilter(obj, event);
        }

        // Click is outside - request close
        emit closeRequested();
    }

    return QWidget::eventFilter(obj, event);
}
