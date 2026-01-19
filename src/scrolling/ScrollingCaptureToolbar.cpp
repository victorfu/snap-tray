#include "scrolling/ScrollingCaptureToolbar.h"
#include "ToolbarStyle.h"
#include "IconRenderer.h"
#include "toolbar/ToolbarRenderer.h"

#include <QLabel>
#include <QHBoxLayout>
#include <QPainter>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QScreen>
#include <QGuiApplication>

ScrollingCaptureToolbar::ScrollingCaptureToolbar(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
#ifdef Q_OS_MACOS
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
#endif
    setMouseTracking(true);
    setFixedHeight(TOOLBAR_HEIGHT);

    loadIcons();
    setupUi();
    updateButtonLayout();
}

ScrollingCaptureToolbar::~ScrollingCaptureToolbar()
{
}

void ScrollingCaptureToolbar::loadIcons()
{
    auto& renderer = IconRenderer::instance();
    renderer.loadIcon("play", ":/icons/icons/play.svg");
    renderer.loadIcon("stop", ":/icons/icons/stop.svg");
    renderer.loadIcon("pin", ":/icons/icons/pin.svg");
    renderer.loadIcon("save", ":/icons/icons/save.svg");
    renderer.loadIcon("copy", ":/icons/icons/copy.svg");
    renderer.loadIcon("close", ":/icons/icons/close.svg");
    renderer.loadIcon("cancel", ":/icons/icons/cancel.svg");
    renderer.loadIcon("arrow-vertical", ":/icons/icons/arrow-vertical.svg");
    renderer.loadIcon("arrow-horizontal", ":/icons/icons/arrow-horizontal.svg");
}

void ScrollingCaptureToolbar::setupUi()
{
    // Configure buttons using Toolbar::ButtonConfig builder pattern
    // Direction button icon will be updated dynamically based on m_direction
    m_buttons = {
        ButtonConfig(ButtonDirection, "arrow-vertical", "Scroll Direction: Vertical ↕ (Click to toggle)"),
        ButtonConfig(ButtonStart, "play", "Start Capture (Enter/Space)").action(),
        ButtonConfig(ButtonStop, "stop", "Stop Capture (Enter/Space)").action(),
        ButtonConfig(ButtonPin, "pin", "Pin to Screen").action(),
        ButtonConfig(ButtonSave, "save", "Save to File (⌘S)"),
        ButtonConfig(ButtonCopy, "copy", "Copy to Clipboard (⌘C)"),
        ButtonConfig(ButtonClose, "close", "Close"),
        ButtonConfig(ButtonCancel, "cancel", "Cancel (Esc)").cancel()
    };

    m_buttonRects.resize(m_buttons.size());
    m_buttonVisible.resize(m_buttons.size());

    // Create size label
    ToolbarStyleConfig styleConfig = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());

    m_sizeLabel = new QLabel("0 × 0 px", this);
    m_sizeLabel->setStyleSheet(QString("QLabel { color: %1; font-size: 12px; background: transparent; }")
        .arg(styleConfig.textColor.name()));

    // Status indicator
    m_statusIndicator = new QLabel(this);
    m_statusIndicator->setFixedSize(12, 12);
    m_statusIndicator->setStyleSheet("QLabel { background-color: #4CAF50; border-radius: 6px; }");
}

void ScrollingCaptureToolbar::setMode(Mode mode)
{
    m_mode = mode;
    updateButtonLayout();
    update();
}

void ScrollingCaptureToolbar::updateButtonLayout()
{
    // Determine which buttons are visible in current mode
    for (int i = 0; i < m_buttons.size(); ++i) {
        bool visible = false;
        int id = m_buttons[i].id;

        switch (m_mode) {
        case Mode::Adjusting:
            visible = (id == ButtonDirection || id == ButtonStart || id == ButtonCancel);
            break;
        case Mode::Capturing:
            visible = (id == ButtonStop || id == ButtonCancel);
            break;
        case Mode::Finished:
            visible = (id == ButtonPin || id == ButtonSave ||
                      id == ButtonCopy || id == ButtonClose);
            break;
        }
        m_buttonVisible[i] = visible;
    }

    // Update label visibility - always show size label
    m_sizeLabel->setVisible(true);
    m_statusIndicator->setVisible(m_mode == Mode::Capturing);

    // Calculate positions
    int x = MARGIN;

    // Position labels first
    if (m_sizeLabel->isVisible()) {
        m_sizeLabel->adjustSize();  // Force recalculate size after text change
        m_sizeLabel->move(x, (TOOLBAR_HEIGHT - m_sizeLabel->height()) / 2);
        x += m_sizeLabel->width() + BUTTON_SPACING;
    }

    if (m_statusIndicator->isVisible()) {
        m_statusIndicator->move(x, (TOOLBAR_HEIGHT - 12) / 2);
        x += 12 + BUTTON_SPACING * 2;
    }

    // Add spacing before buttons
    x += BUTTON_SPACING * 2;

    // Position buttons
    int buttonY = (TOOLBAR_HEIGHT - BUTTON_SIZE) / 2;
    for (int i = 0; i < m_buttons.size(); ++i) {
        if (m_buttonVisible[i]) {
            m_buttonRects[i] = QRect(x, buttonY, BUTTON_SIZE, BUTTON_SIZE);
            x += BUTTON_SIZE + BUTTON_SPACING;
        } else {
            m_buttonRects[i] = QRect();
        }
    }

    // Calculate total width and resize
    int totalWidth = x + MARGIN;
    setFixedWidth(totalWidth);
}

void ScrollingCaptureToolbar::positionNear(const QRect &region)
{
    QScreen *screen = QGuiApplication::screenAt(region.center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }

    if (!screen) {
        qWarning() << "ScrollingCaptureToolbar: No screen available for positioning";
        return;
    }

    QRect screenGeom = screen->geometry();

    int x = region.center().x() - width() / 2;
    int y = region.bottom() + 8;

    if (y + height() > screenGeom.bottom() - 10) {
        y = region.top() - height() - 8;
    }

    if (y < screenGeom.top() + 10) {
        y = region.bottom() - height() - 10;
    }

    x = qBound(screenGeom.left() + 10, x, screenGeom.right() - width() - 10);

    move(x, y);
}

void ScrollingCaptureToolbar::updateSize(int width, int height)
{
    m_sizeLabel->setText(QString("%1 × %2 px").arg(width).arg(height));
    updateButtonLayout();
}

void ScrollingCaptureToolbar::setMatchStatus(bool matched, double confidence)
{
    if (!m_statusIndicator->isVisible()) {
        return;
    }

    QString color;
    if (!matched) {
        color = "#FF3B30";
    } else if (confidence >= 0.70) {
        color = "#4CAF50";
    } else if (confidence >= 0.50) {
        color = "#FFC107";
    } else {
        color = "#FF9800";
    }

    m_statusIndicator->setStyleSheet(
        QString("QLabel { background-color: %1; border-radius: 6px; }").arg(color));
}

void ScrollingCaptureToolbar::setDirection(Direction direction)
{
    if (m_direction == direction) {
        return;
    }

    m_direction = direction;

    // Update direction button icon and tooltip
    for (int i = 0; i < m_buttons.size(); ++i) {
        if (m_buttons[i].id == ButtonDirection) {
            if (direction == Direction::Vertical) {
                m_buttons[i].iconKey = "arrow-vertical";
                m_buttons[i].tooltip = "Scroll Direction: Vertical ↕ (Click to toggle)";
            } else {
                m_buttons[i].iconKey = "arrow-horizontal";
                m_buttons[i].tooltip = "Scroll Direction: Horizontal ↔ (Click to toggle)";
            }
            break;
        }
    }

    update();
}


void ScrollingCaptureToolbar::paintEvent(QPaintEvent *event)
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

    // Draw buttons
    for (int i = 0; i < m_buttons.size(); ++i) {
        if (m_buttonVisible[i]) {
            drawButton(painter, i);
        }
    }

    // Draw tooltip if hovering
    if (m_hoveredButton >= 0 && m_hoveredButton < m_buttons.size() && m_buttonVisible[m_hoveredButton]) {
        drawTooltip(painter);
    }
}

void ScrollingCaptureToolbar::drawButton(QPainter &painter, int index)
{
    ToolbarStyleConfig styleConfig = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
    const ButtonConfig &config = m_buttons[index];
    QRect btnRect = m_buttonRects[index];
    bool isHovered = (index == m_hoveredButton);

    // Draw hover background
    if (isHovered) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(styleConfig.hoverBackgroundColor);
        painter.drawRoundedRect(btnRect.adjusted(2, 2, -2, -2), 4, 4);
    }

    // Get icon color using shared utility
    QColor iconColor = Toolbar::ToolbarRenderer::getIconColor(config, false, isHovered, styleConfig);

    // Render icon
    IconRenderer::instance().renderIcon(painter, btnRect, config.iconKey, iconColor, 4);
}

void ScrollingCaptureToolbar::drawTooltip(QPainter &painter)
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

int ScrollingCaptureToolbar::buttonAtPosition(const QPoint &pos) const
{
    for (int i = 0; i < m_buttonRects.size(); ++i) {
        if (m_buttonVisible[i] && m_buttonRects[i].contains(pos)) {
            return i;
        }
    }
    return -1;
}

void ScrollingCaptureToolbar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        int btnIndex = buttonAtPosition(event->pos());
        if (btnIndex >= 0) {
            // Handle button click
            switch (m_buttons[btnIndex].id) {
            case ButtonDirection:  emit directionToggled(); break;
            case ButtonStart:      emit startClicked(); break;
            case ButtonStop:       emit stopClicked(); break;
            case ButtonPin:        emit pinClicked(); break;
            case ButtonSave:       emit saveClicked(); break;
            case ButtonCopy:       emit copyClicked(); break;
            case ButtonClose:      emit closeClicked(); break;
            case ButtonCancel:     emit cancelClicked(); break;
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

void ScrollingCaptureToolbar::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging) {
        QPoint delta = event->globalPosition().toPoint() - m_dragStartPos;
        move(m_dragStartWidgetPos + delta);
    } else {
        // Update hover state
        int newHovered = buttonAtPosition(event->pos());
        if (newHovered != m_hoveredButton) {
            m_hoveredButton = newHovered;
            update();
        }
    }
    QWidget::mouseMoveEvent(event);
}

void ScrollingCaptureToolbar::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_isDragging = false;
    }
    QWidget::mouseReleaseEvent(event);
}

void ScrollingCaptureToolbar::leaveEvent(QEvent *event)
{
    if (m_hoveredButton >= 0) {
        m_hoveredButton = -1;
        update();
    }
    QWidget::leaveEvent(event);
}
