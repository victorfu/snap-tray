#include "toolbar/WindowedSubToolbar.h"
#include "toolbar/ToolOptionsPanel.h"
#include "EmojiPicker.h"
#include "toolbar/WindowedToolbar.h"
#include "tools/ToolSectionConfig.h"
#include "tools/ToolId.h"
#include "cursor/CursorManager.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QScreen>
#include <QGuiApplication>

// Reserve space above content for tooltips (AutoBlurSection draws tooltip above button)
static constexpr int TOOLTIP_HEIGHT = 28;

namespace {

// Map WindowedToolbar::ButtonId to ToolId for section configuration
ToolId buttonIdToToolId(int buttonId)
{
    using ButtonId = WindowedToolbar::ButtonId;
    switch (buttonId) {
        case ButtonId::ButtonPencil:    return ToolId::Pencil;
        case ButtonId::ButtonMarker:    return ToolId::Marker;
        case ButtonId::ButtonArrow:     return ToolId::Arrow;
        case ButtonId::ButtonShape:     return ToolId::Shape;
        case ButtonId::ButtonText:      return ToolId::Text;
        case ButtonId::ButtonMosaic:    return ToolId::Mosaic;
        case ButtonId::ButtonStepBadge: return ToolId::StepBadge;
        case ButtonId::ButtonEmoji:     return ToolId::EmojiSticker;
        default:                        return ToolId::Selection; // No sub-toolbar
    }
}

} // anonymous namespace

WindowedSubToolbar::WindowedSubToolbar(QWidget *parent)
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

    // Create the color and width widget
    // Let ToolOptionsPanel draw its own glass panel background (like RegionSelector)
    m_colorAndWidthWidget = new ToolOptionsPanel(this);

    // Create emoji picker
    m_emojiPicker = new EmojiPicker(this);

    setupConnections();

    // Initially hidden
    QWidget::hide();
}

WindowedSubToolbar::~WindowedSubToolbar()
{
}

void WindowedSubToolbar::setupConnections()
{
    // Color/width signals
    connect(m_colorAndWidthWidget, &ToolOptionsPanel::colorSelected,
            this, &WindowedSubToolbar::colorSelected);
    connect(m_colorAndWidthWidget, &ToolOptionsPanel::customColorPickerRequested,
            this, &WindowedSubToolbar::customColorPickerRequested);
    connect(m_colorAndWidthWidget, &ToolOptionsPanel::widthChanged,
            this, &WindowedSubToolbar::widthChanged);

    // Shape signals
    connect(m_colorAndWidthWidget, &ToolOptionsPanel::shapeTypeChanged,
            this, &WindowedSubToolbar::shapeTypeChanged);
    connect(m_colorAndWidthWidget, &ToolOptionsPanel::shapeFillModeChanged,
            this, &WindowedSubToolbar::shapeFillModeChanged);

    // Arrow signals
    connect(m_colorAndWidthWidget, &ToolOptionsPanel::arrowStyleChanged,
            this, &WindowedSubToolbar::arrowStyleChanged);
    connect(m_colorAndWidthWidget, &ToolOptionsPanel::lineStyleChanged,
            this, &WindowedSubToolbar::lineStyleChanged);

    // Step badge signals
    connect(m_colorAndWidthWidget, &ToolOptionsPanel::stepBadgeSizeChanged,
            this, &WindowedSubToolbar::stepBadgeSizeChanged);

    // Text signals
    connect(m_colorAndWidthWidget, &ToolOptionsPanel::boldToggled,
            this, &WindowedSubToolbar::boldToggled);
    connect(m_colorAndWidthWidget, &ToolOptionsPanel::italicToggled,
            this, &WindowedSubToolbar::italicToggled);
    connect(m_colorAndWidthWidget, &ToolOptionsPanel::underlineToggled,
            this, &WindowedSubToolbar::underlineToggled);
    connect(m_colorAndWidthWidget, &ToolOptionsPanel::fontSizeDropdownRequested,
            this, &WindowedSubToolbar::fontSizeDropdownRequested);
    connect(m_colorAndWidthWidget, &ToolOptionsPanel::fontFamilyDropdownRequested,
            this, &WindowedSubToolbar::fontFamilyDropdownRequested);

    // Emoji signals
    connect(m_emojiPicker, &EmojiPicker::emojiSelected,
            this, &WindowedSubToolbar::emojiSelected);

    // Auto blur signal
    connect(m_colorAndWidthWidget, &ToolOptionsPanel::autoBlurRequested,
            this, &WindowedSubToolbar::autoBlurRequested);

    // Dropdown state changes - resize window to accommodate dropdown
    connect(m_colorAndWidthWidget, &ToolOptionsPanel::dropdownStateChanged,
            this, &WindowedSubToolbar::onDropdownStateChanged);
}

void WindowedSubToolbar::positionBelow(const QRect &toolbarRect)
{
    QScreen *screen = QGuiApplication::screenAt(toolbarRect.center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }

    QRect screenGeom = screen->geometry();

    // Calculate tooltip space - if AutoBlur section is visible, we reserved space above content
    int tooltipSpace = (m_colorAndWidthWidget && m_colorAndWidthWidget->showAutoBlurSection())
                       ? TOOLTIP_HEIGHT : 0;

    // Position below the toolbar with MARGIN gap, left-aligned with toolbar
    // Offset upward by tooltipSpace so the glass panel (content area) appears at the right position
    int x = toolbarRect.left();
    int y = toolbarRect.bottom() + MARGIN - tooltipSpace;

    // If would go off bottom, position above toolbar
    if (y + height() > screenGeom.bottom() - 10) {
        y = toolbarRect.top() - height() - MARGIN;
    }

    // Keep on screen horizontally
    x = qBound(screenGeom.left() + 10, x, screenGeom.right() - width() - 10);

    move(x, y);
}

void WindowedSubToolbar::showForTool(int buttonId)
{
    using ButtonId = WindowedToolbar::ButtonId;

    // Reset emoji picker state
    m_showingEmojiPicker = false;
    m_emojiPicker->setVisible(false);

    // Handle special cases first
    if (buttonId == ButtonId::ButtonEmoji) {
        m_showingEmojiPicker = true;
        m_emojiPicker->setVisible(true);
        // Apply empty config to hide ToolOptionsPanel sections
        ToolSectionConfig{}.applyTo(m_colorAndWidthWidget);
    }
    else if (buttonId == ButtonId::ButtonEraser) {
        // Eraser has no sub-toolbar (uses dynamic cursor only)
        QWidget::hide();
        return;
    }
    else {
        // Map ButtonId to ToolId and apply data-driven configuration
        ToolId toolId = buttonIdToToolId(buttonId);
        ToolSectionConfig config = ToolSectionConfig::forTool(toolId);

        if (!config.hasAnySectionEnabled()) {
            // Tool has no sub-toolbar sections
            QWidget::hide();
            return;
        }

        config.applyTo(m_colorAndWidthWidget);
    }

    bool showWidget = m_showingEmojiPicker || ToolSectionConfig::forTool(buttonIdToToolId(buttonId)).hasAnySectionEnabled();

    if (showWidget) {
        // Force widgets to recalculate their size
        // Pass dummy anchor rect since we position them manually at (MARGIN, MARGIN)
        // Use above=false so dropdowns expand DOWNWARD (like RegionSelector)
        if (m_showingEmojiPicker) {
            m_emojiPicker->updatePosition(QRect(0, 0, 0, 0), false);
        } else {
            m_colorAndWidthWidget->setVisible(true);  // Enable drawing
            m_colorAndWidthWidget->updatePosition(QRect(0, 0, 0, 0), false, 0);
        }

        updateSize();
        QWidget::show();
        raise();
    }
}

void WindowedSubToolbar::hide()
{
    m_emojiPicker->setVisible(false);
    m_showingEmojiPicker = false;
    QWidget::hide();
}

void WindowedSubToolbar::updateSize()
{
    if (m_showingEmojiPicker) {
        // EmojiPicker has its own padding, no extra margin needed
        QRect emojiRect = m_emojiPicker->boundingRect();
        setFixedSize(emojiRect.width(), emojiRect.height());
    } else {
        // ToolOptionsPanel has its own padding, no extra margin needed (like RegionSelector)
        QRect widgetRect = m_colorAndWidthWidget->boundingRect();
        // Dropdowns expand DOWNWARD, include dropdown height at bottom of window
        int dropdownHeight = m_colorAndWidthWidget->activeDropdownHeight();
        // Reserve space ABOVE content for tooltips (AutoBlurSection)
        int tooltipSpace = m_colorAndWidthWidget->showAutoBlurSection() ? TOOLTIP_HEIGHT : 0;
        // Reserve extra width for tooltip text ("Auto Blur Faces" is ~120px)
        int minWidth = m_colorAndWidthWidget->showAutoBlurSection() ? 130 : 0;
        int finalWidth = qMax(widgetRect.width(), minWidth);
        setFixedSize(finalWidth, widgetRect.height() + dropdownHeight + tooltipSpace);
    }
}

void WindowedSubToolbar::onDropdownStateChanged()
{
    // Dropdowns expand downward - just resize, no position change needed
    updateSize();
    update();
}

void WindowedSubToolbar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    if (m_showingEmojiPicker) {
        // EmojiPicker draws its own glass panel background
        // Translate to compensate for boundingRect position
        QRect emojiRect = m_emojiPicker->boundingRect();
        painter.translate(-emojiRect.x(), -emojiRect.y());
        m_emojiPicker->draw(painter);
    } else {
        // ToolOptionsPanel draws its own glass panel background (like RegionSelector)
        // Reserve space for tooltip ABOVE content
        int tooltipSpace = m_colorAndWidthWidget->showAutoBlurSection() ? TOOLTIP_HEIGHT : 0;
        // Translate to compensate for boundingRect position and add tooltip space
        QRect widgetRect = m_colorAndWidthWidget->boundingRect();
        painter.translate(-widgetRect.x(), -widgetRect.y() + tooltipSpace);
        m_colorAndWidthWidget->draw(painter);
    }
}

void WindowedSubToolbar::mousePressEvent(QMouseEvent *event)
{
    if (m_showingEmojiPicker) {
        // Convert window coords to widget coords (reverse of paint translation)
        QRect emojiRect = m_emojiPicker->boundingRect();
        QPoint localPos = event->pos() + QPoint(emojiRect.x(), emojiRect.y());
        if (m_emojiPicker->handleClick(localPos)) {
            update();
            return;
        }
    } else {
        // Convert window coords to widget coords (reverse of paint translation)
        int tooltipSpace = m_colorAndWidthWidget->showAutoBlurSection() ? TOOLTIP_HEIGHT : 0;
        QRect widgetRect = m_colorAndWidthWidget->boundingRect();
        QPoint localPos = event->pos() + QPoint(widgetRect.x(), widgetRect.y() - tooltipSpace);
        if (m_colorAndWidthWidget->handleMousePress(localPos)) {
            update();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void WindowedSubToolbar::enterEvent(QEnterEvent *event)
{
    // Use ArrowCursor for toolbar hover
    CursorManager::instance().pushCursorForWidget(this, CursorContext::Hover, Qt::ArrowCursor);
    QWidget::enterEvent(event);
}

void WindowedSubToolbar::mouseMoveEvent(QMouseEvent *event)
{
    bool pressed = event->buttons() & Qt::LeftButton;

    // Cursor is already set in enterEvent, no need to push on every move

    if (m_showingEmojiPicker) {
        // Convert window coords to widget coords (reverse of paint translation)
        QRect emojiRect = m_emojiPicker->boundingRect();
        QPoint localPos = event->pos() + QPoint(emojiRect.x(), emojiRect.y());
        if (m_emojiPicker->updateHoveredEmoji(localPos)) {
            update();
        }
    } else {
        // Convert window coords to widget coords (reverse of paint translation)
        int tooltipSpace = m_colorAndWidthWidget->showAutoBlurSection() ? TOOLTIP_HEIGHT : 0;
        QRect widgetRect = m_colorAndWidthWidget->boundingRect();
        QPoint localPos = event->pos() + QPoint(widgetRect.x(), widgetRect.y() - tooltipSpace);
        if (m_colorAndWidthWidget->handleMouseMove(localPos, pressed)) {
            update();
        }
    }
    QWidget::mouseMoveEvent(event);
}

void WindowedSubToolbar::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_showingEmojiPicker) {
        // Convert window coords to widget coords (reverse of paint translation)
        int tooltipSpace = m_colorAndWidthWidget->showAutoBlurSection() ? TOOLTIP_HEIGHT : 0;
        QRect widgetRect = m_colorAndWidthWidget->boundingRect();
        QPoint localPos = event->pos() + QPoint(widgetRect.x(), widgetRect.y() - tooltipSpace);
        if (m_colorAndWidthWidget->handleMouseRelease(localPos)) {
            update();
            return;
        }
    }
    QWidget::mouseReleaseEvent(event);
}

void WindowedSubToolbar::wheelEvent(QWheelEvent *event)
{
    if (!m_showingEmojiPicker) {
        int delta = event->angleDelta().y();
        if (m_colorAndWidthWidget->handleWheel(delta)) {
            update();
            return;
        }
    }
    QWidget::wheelEvent(event);
}

void WindowedSubToolbar::leaveEvent(QEvent *event)
{
    if (m_showingEmojiPicker) {
        m_emojiPicker->updateHoveredEmoji(QPoint(-1, -1));
    } else {
        m_colorAndWidthWidget->updateHovered(QPoint(-1, -1));
    }
    // Pop hover cursor instead of clearing entire stack
    CursorManager::instance().popCursorForWidget(this, CursorContext::Hover);
    emit cursorRestoreRequested();
    update();
    QWidget::leaveEvent(event);
}
