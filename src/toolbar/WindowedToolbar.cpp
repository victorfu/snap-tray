#include "toolbar/WindowedToolbar.h"
#include "toolbar/WindowInteractionUtils.h"
#include "ToolbarStyle.h"
#include "IconRenderer.h"
#include "toolbar/ToolbarRenderer.h"
#include "GlassRenderer.h"
#include "cursor/CursorManager.h"
#include "tools/ToolRegistry.h"
#include "tools/ToolTraits.h"

#include <QPainter>
#include <QMouseEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QApplication>
#include <QShowEvent>
#include <QHideEvent>
#include <QFontMetrics>
#include <array>
#include <utility>

namespace {
bool isValidToolId(int buttonId)
{
    return buttonId >= 0 && buttonId < static_cast<int>(ToolId::Count);
}

using ActionSignal = void (WindowedToolbar::*)();

constexpr std::array<std::pair<int, ActionSignal>, 7> kActionButtonSignals = {{
    {static_cast<int>(ToolId::Undo), &WindowedToolbar::undoClicked},
    {static_cast<int>(ToolId::Redo), &WindowedToolbar::redoClicked},
    {static_cast<int>(ToolId::OCR), &WindowedToolbar::ocrClicked},
    {static_cast<int>(ToolId::QRCode), &WindowedToolbar::qrCodeClicked},
    {static_cast<int>(ToolId::Copy), &WindowedToolbar::copyClicked},
    {static_cast<int>(ToolId::Save), &WindowedToolbar::saveClicked},
    {WindowedToolbar::ButtonDone, &WindowedToolbar::doneClicked}
}};

class ToolbarGlassTooltipWidget : public QWidget
{
public:
    explicit ToolbarGlassTooltipWidget(QWidget* parent = nullptr)
        : QWidget(parent, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    void setTextAndStyle(const QString& text, const ToolbarStyleConfig& style)
    {
        m_text = text;
        m_style = style;
        QFont font = this->font();
        font.setPointSize(11);
        setFont(font);
        updateGeometry();
        update();
    }

    QSize sizeHint() const override
    {
        if (m_text.isEmpty()) {
            return QSize(0, 0);
        }
        QFontMetrics fm(font());
        QRect textRect = fm.boundingRect(m_text);
        textRect.adjust(-8, -4, 8, 4);
        return textRect.size();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        if (m_text.isEmpty()) {
            return;
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        ToolbarStyleConfig tooltipConfig = m_style;
        tooltipConfig.shadowColor.setAlpha(0);
        tooltipConfig.shadowOffsetY = 0;
        tooltipConfig.shadowBlurRadius = 0;
        GlassRenderer::drawGlassPanel(painter, rect(), tooltipConfig, 6);

        painter.setPen(m_style.tooltipText);
        painter.drawText(rect(), Qt::AlignCenter, m_text);
    }

private:
    QString m_text;
    ToolbarStyleConfig m_style;
};
}

WindowedToolbar::WindowedToolbar(QWidget *parent)
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

WindowedToolbar::~WindowedToolbar()
{
    if (m_hoverTooltip) {
        m_hoverTooltip->deleteLater();
        m_hoverTooltip = nullptr;
    }
}

void WindowedToolbar::loadIcons()
{
    auto& renderer = IconRenderer::instance();
    const QVector<ToolId> tools = ToolRegistry::instance().getToolsForToolbar(ToolbarType::PinWindow);
    for (ToolId toolId : tools) {
        const QString iconKey = ToolRegistry::instance().getIconKey(toolId);
        if (!iconKey.isEmpty()) {
            renderer.loadIconByKey(iconKey);
        }
    }

    // Local toolbar-only action icon.
    renderer.loadIconByKey("done");
}

void WindowedToolbar::setupUi()
{
    m_buttons.clear();
    auto& registry = ToolRegistry::instance();
    const QVector<ToolId> tools = registry.getToolsForToolbar(ToolbarType::PinWindow);

    for (ToolId toolId : tools) {
        const auto& def = registry.get(toolId);
        ButtonConfig config(
            static_cast<int>(toolId),
            def.iconKey,
            registry.getTooltipWithShortcut(toolId),
            def.showSeparatorBefore);

        if (toolId == ToolId::OCR) {
            config.separator();
        }

        m_buttons.append(config);
    }

    // Close annotation mode in pin window (local action).
    m_buttons.append(ButtonConfig(ButtonDone, "done", "Done (Space/Esc)").separator());

    m_buttonRects.resize(m_buttons.size());
}

void WindowedToolbar::updateButtonLayout()
{
    int x = MARGIN;
    int buttonY = (TOOLBAR_HEIGHT - BUTTON_HEIGHT) / 2;

    for (int i = 0; i < m_buttons.size(); ++i) {
        const ButtonConfig& config = m_buttons[i];

        // Skip OCR button if not available
        if (config.id == static_cast<int>(ToolId::OCR) && !m_ocrAvailable) {
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

void WindowedToolbar::positionNear(const QRect &pinWindowRect)
{
    QScreen *screen = QGuiApplication::screenAt(pinWindowRect.center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }

    QRect screenGeom = screen->geometry();

    // Position below the pin window, right-aligned (same as RegionSelector toolbar)
    int x = pinWindowRect.right() - width() + 1;
    int y = pinWindowRect.bottom() + MARGIN;

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

void WindowedToolbar::setActiveButton(int buttonId)
{
    m_activeButton = buttonId;
    update();
}

void WindowedToolbar::setCanUndo(bool canUndo)
{
    m_canUndo = canUndo;
    updateHoverTooltip();
    update();
}

void WindowedToolbar::setCanRedo(bool canRedo)
{
    m_canRedo = canRedo;
    updateHoverTooltip();
    update();
}

void WindowedToolbar::setOCRAvailable(bool available)
{
    if (m_ocrAvailable != available) {
        m_ocrAvailable = available;
        updateButtonLayout();
        updateHoverTooltip();
        update();
    }
}

bool WindowedToolbar::isDrawingTool(int buttonId) const
{
    if (!isValidToolId(buttonId)) {
        return false;
    }
    return ToolTraits::isAnnotationTool(static_cast<ToolId>(buttonId));
}

bool WindowedToolbar::dispatchActionButton(int buttonId)
{
    for (const auto& entry : kActionButtonSignals) {
        if (entry.first == buttonId) {
            (this->*entry.second)();
            return true;
        }
    }
    return false;
}

bool WindowedToolbar::isButtonDisabled(const ButtonConfig& config) const
{
    if (config.id == static_cast<int>(ToolId::Undo)) {
        return !m_canUndo;
    }
    if (config.id == static_cast<int>(ToolId::Redo)) {
        return !m_canRedo;
    }
    return false;
}

void WindowedToolbar::updateHoverTooltip()
{
    if (!isVisible() || m_isDragging || m_hoveredButton < 0 || m_hoveredButton >= m_buttons.size()) {
        hideHoverTooltip();
        return;
    }

    const ButtonConfig& config = m_buttons[m_hoveredButton];
    if (config.tooltip.isEmpty()) {
        hideHoverTooltip();
        return;
    }

    const QRect btnRect = m_buttonRects[m_hoveredButton];
    if (btnRect.isNull()) {
        hideHoverTooltip();
        return;
    }

    if (!m_hoverTooltip) {
        auto* tooltipWidget = new ToolbarGlassTooltipWidget(nullptr);
        m_hoverTooltip = tooltipWidget;
        connect(tooltipWidget, &QObject::destroyed, this, [this]() {
            m_hoverTooltip = nullptr;
        });
    }

    auto* tooltipWidget = static_cast<ToolbarGlassTooltipWidget*>(m_hoverTooltip);
    const ToolbarStyleConfig& styleConfig =
        ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
    tooltipWidget->setTextAndStyle(config.tooltip, styleConfig);

    const QSize tooltipSize = tooltipWidget->sizeHint();
    if (tooltipSize.isEmpty()) {
        hideHoverTooltip();
        return;
    }
    tooltipWidget->resize(tooltipSize);

    const QPoint globalAnchorCenter = mapToGlobal(btnRect.center());
    int tooltipX = globalAnchorCenter.x() - tooltipSize.width() / 2;
    int tooltipY = mapToGlobal(btnRect.topLeft()).y() - tooltipSize.height() - 6;

    if (QScreen* screen = QGuiApplication::screenAt(globalAnchorCenter)) {
        const QRect bounds = screen->availableGeometry();
        tooltipX = qBound(bounds.left() + 5, tooltipX, bounds.right() - tooltipSize.width() - 5);
    }

    tooltipWidget->move(tooltipX, tooltipY);
    tooltipWidget->show();
}

void WindowedToolbar::hideHoverTooltip()
{
    if (m_hoverTooltip) {
        m_hoverTooltip->hide();
    }
}

void WindowedToolbar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const ToolbarStyleConfig& styleConfig = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());

    // Draw background using shared renderer for consistent glass effect
    Toolbar::ToolbarRenderer::drawToolbarBackground(painter, rect(), styleConfig, 8);

    // Draw separators using shared renderer
    for (int i = 0; i < m_buttons.size(); ++i) {
        if (m_buttons[i].separatorBefore && !m_buttonRects[i].isNull() && i > 0) {
            int sepX = m_buttonRects[i].left() - SEPARATOR_WIDTH / 2 - BUTTON_SPACING / 2;
            int sepY = (TOOLBAR_HEIGHT - 16) / 2;
            Toolbar::ToolbarRenderer::drawSeparator(painter, sepX, sepY, 16, styleConfig);
        }
    }

    // Draw buttons
    for (int i = 0; i < m_buttons.size(); ++i) {
        if (!m_buttonRects[i].isNull()) {
            drawButton(painter, i);
        }
    }
}

void WindowedToolbar::drawButton(QPainter &painter, int index)
{
    const ToolbarStyleConfig& styleConfig = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
    const ButtonConfig &config = m_buttons[index];
    QRect btnRect = m_buttonRects[index];
    bool isHovered = (index == m_hoveredButton);
    bool isActive = (config.id == m_activeButton);
    bool isDisabled = isButtonDisabled(config);

    // Draw active/hover background using shared renderer
    if (!isDisabled) {
        Toolbar::ToolbarRenderer::drawButtonBackground(painter, btnRect, isActive, isHovered, styleConfig);
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

    // Render icon (padding 8 matches RegionSelector's ToolbarCore)
    IconRenderer::instance().renderIcon(painter, btnRect, config.iconKey, iconColor, 8);
}

int WindowedToolbar::buttonAtPosition(const QPoint &pos) const
{
    for (int i = 0; i < m_buttonRects.size(); ++i) {
        if (!m_buttonRects[i].isNull() && m_buttonRects[i].contains(pos)) {
            return i;
        }
    }
    return -1;
}

void WindowedToolbar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        int btnIndex = buttonAtPosition(event->pos());
        if (btnIndex >= 0) {
            const ButtonConfig& config = m_buttons[btnIndex];

            // Check if button is disabled
            if (isButtonDisabled(config)) {
                return;
            }

            hideHoverTooltip();
            if (isDrawingTool(config.id)) {
                emit toolSelected(config.id);
                return;
            }

            dispatchActionButton(config.id);
            return;
        }

        // Start dragging
        hideHoverTooltip();
        m_isDragging = true;
        m_dragStartPos = event->globalPosition().toPoint();
        m_dragStartWidgetPos = pos();
        // Push drag cursor
        CursorManager::instance().pushCursorForWidget(this, CursorContext::Drag, Qt::ClosedHandCursor);
    }
    QWidget::mousePressEvent(event);
}

void WindowedToolbar::enterEvent(QEnterEvent *event)
{
    // Update hover state when mouse enters
    auto& cm = CursorManager::instance();
    int hovered = buttonAtPosition(event->position().toPoint());

    if (hovered != m_hoveredButton) {
        m_hoveredButton = hovered;
        updateHoverTooltip();
        update();
    } else {
        updateHoverTooltip();
    }

    // Use ArrowCursor for toolbar hover
    cm.pushCursorForWidget(this, CursorContext::Hover, Qt::ArrowCursor);
    QWidget::enterEvent(event);
}

void WindowedToolbar::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging) {
        QPoint delta = event->globalPosition().toPoint() - m_dragStartPos;
        move(m_dragStartWidgetPos + delta);
        hideHoverTooltip();
        // Drag cursor is already pushed in mousePressEvent
        QWidget::mouseMoveEvent(event);
        return;
    }

    // Update hover state
    int newHovered = buttonAtPosition(event->pos());
    if (newHovered != m_hoveredButton) {
        m_hoveredButton = newHovered;
        updateHoverTooltip();
        update();
    } else {
        updateHoverTooltip();
    }

    // Cursor is already set in enterEvent, no need to push on every move
    QWidget::mouseMoveEvent(event);
}

void WindowedToolbar::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_isDragging) {
            m_isDragging = false;
            // Pop drag cursor and restore hover cursor
            auto& cm = CursorManager::instance();
            cm.popCursorForWidget(this, CursorContext::Drag);
            updateHoverTooltip();
        }
    }
    QWidget::mouseReleaseEvent(event);
}

void WindowedToolbar::leaveEvent(QEvent *event)
{
    if (m_hoveredButton >= 0) {
        m_hoveredButton = -1;
        update();
    }
    hideHoverTooltip();
    // Pop hover cursor instead of clearing entire stack
    CursorManager::instance().popCursorForWidget(this, CursorContext::Hover);
    emit cursorRestoreRequested();
    QWidget::leaveEvent(event);
}

void WindowedToolbar::setAssociatedWidgets(QWidget *window, QWidget *subToolbar)
{
    m_associatedWindow = window;
    m_subToolbar = subToolbar;
}

void WindowedToolbar::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    qApp->installEventFilter(this);
    m_showTime.start();
}

void WindowedToolbar::hideEvent(QHideEvent *event)
{
    hideHoverTooltip();
    qApp->removeEventFilter(this);
    QWidget::hideEvent(event);
}

bool WindowedToolbar::eventFilter(QObject *obj, QEvent *event)
{
    // Ignore during first 300ms to prevent accidental close
    if (m_showTime.isValid() && m_showTime.elapsed() < 300) {
        return QWidget::eventFilter(obj, event);
    }

    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        QPoint globalPos = mouseEvent->globalPosition().toPoint();

        if (Toolbar::shouldIgnoreOutsideClick(globalPos, this, m_subToolbar, m_associatedWindow)) {
            return QWidget::eventFilter(obj, event);
        }

        // Click is outside - request close
        hideHoverTooltip();
        emit closeRequested();
        return false;  // Let the event propagate to its actual target
    }

    return QWidget::eventFilter(obj, event);
}
