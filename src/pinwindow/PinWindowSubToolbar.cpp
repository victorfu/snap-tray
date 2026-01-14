#include "pinwindow/PinWindowSubToolbar.h"
#include "ColorAndWidthWidget.h"
#include "EmojiPicker.h"
#include "pinwindow/PinWindowToolbar.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QScreen>
#include <QGuiApplication>

// Reserve space above content for tooltips (AutoBlurSection draws tooltip above button)
static constexpr int TOOLTIP_HEIGHT = 28;

PinWindowSubToolbar::PinWindowSubToolbar(QWidget *parent)
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
    // Let ColorAndWidthWidget draw its own glass panel background (like RegionSelector)
    m_colorAndWidthWidget = new ColorAndWidthWidget(this);

    // Create emoji picker
    m_emojiPicker = new EmojiPicker(this);

    setupConnections();

    // Initially hidden
    QWidget::hide();
}

PinWindowSubToolbar::~PinWindowSubToolbar()
{
}

void PinWindowSubToolbar::setupConnections()
{
    // Color/width signals
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::colorSelected,
            this, &PinWindowSubToolbar::colorSelected);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::customColorPickerRequested,
            this, &PinWindowSubToolbar::customColorPickerRequested);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::widthChanged,
            this, &PinWindowSubToolbar::widthChanged);

    // Shape signals
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::shapeTypeChanged,
            this, &PinWindowSubToolbar::shapeTypeChanged);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::shapeFillModeChanged,
            this, &PinWindowSubToolbar::shapeFillModeChanged);

    // Arrow signals
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::arrowStyleChanged,
            this, &PinWindowSubToolbar::arrowStyleChanged);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::lineStyleChanged,
            this, &PinWindowSubToolbar::lineStyleChanged);

    // Step badge signals
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::stepBadgeSizeChanged,
            this, &PinWindowSubToolbar::stepBadgeSizeChanged);

    // Text signals
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::boldToggled,
            this, &PinWindowSubToolbar::boldToggled);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::italicToggled,
            this, &PinWindowSubToolbar::italicToggled);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::underlineToggled,
            this, &PinWindowSubToolbar::underlineToggled);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::fontSizeDropdownRequested,
            this, &PinWindowSubToolbar::fontSizeDropdownRequested);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::fontFamilyDropdownRequested,
            this, &PinWindowSubToolbar::fontFamilyDropdownRequested);

    // Emoji signals
    connect(m_emojiPicker, &EmojiPicker::emojiSelected,
            this, &PinWindowSubToolbar::emojiSelected);

    // Auto blur signal
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::autoBlurRequested,
            this, &PinWindowSubToolbar::autoBlurRequested);

    // Dropdown state changes - resize window to accommodate dropdown
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::dropdownStateChanged,
            this, &PinWindowSubToolbar::onDropdownStateChanged);
}

void PinWindowSubToolbar::positionBelow(const QRect &toolbarRect)
{
    qDebug() << "PinWindowSubToolbar::positionBelow - toolbarRect=" << toolbarRect;

    QScreen *screen = QGuiApplication::screenAt(toolbarRect.center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }

    QRect screenGeom = screen->geometry();
    qDebug() << "PinWindowSubToolbar: screenGeom=" << screenGeom << "mySize=" << size();

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

    qDebug() << "PinWindowSubToolbar: moving to x=" << x << "y=" << y << "tooltipSpace=" << tooltipSpace;
    move(x, y);
}

void PinWindowSubToolbar::showForTool(int toolId)
{
    qDebug() << "PinWindowSubToolbar::showForTool - toolId:" << toolId;

    using ButtonId = PinWindowToolbar::ButtonId;

    // Reset ALL sections first
    m_colorAndWidthWidget->setShowColorSection(false);
    m_colorAndWidthWidget->setShowWidthSection(false);
    m_colorAndWidthWidget->setShowTextSection(false);
    m_colorAndWidthWidget->setShowArrowStyleSection(false);
    m_colorAndWidthWidget->setShowLineStyleSection(false);
    m_colorAndWidthWidget->setShowShapeSection(false);
    m_colorAndWidthWidget->setShowSizeSection(false);
    m_colorAndWidthWidget->setShowMosaicWidthSection(false);
    m_colorAndWidthWidget->setShowAutoBlurSection(false);
    m_showingEmojiPicker = false;
    m_emojiPicker->setVisible(false);

    bool showWidget = false;

    switch (toolId) {
    case ButtonId::ButtonPencil:
        m_colorAndWidthWidget->setShowColorSection(true);
        m_colorAndWidthWidget->setShowWidthSection(true);
        m_colorAndWidthWidget->setShowLineStyleSection(true);
        showWidget = true;
        break;

    case ButtonId::ButtonMarker:
        // Marker only needs color, no width (matches RegionSelector kToolCapabilities)
        m_colorAndWidthWidget->setShowColorSection(true);
        showWidget = true;
        break;

    case ButtonId::ButtonArrow:
        m_colorAndWidthWidget->setShowColorSection(true);
        m_colorAndWidthWidget->setShowWidthSection(true);
        m_colorAndWidthWidget->setShowArrowStyleSection(true);
        m_colorAndWidthWidget->setShowLineStyleSection(true);
        showWidget = true;
        break;

    case ButtonId::ButtonShape:
        m_colorAndWidthWidget->setShowColorSection(true);
        m_colorAndWidthWidget->setShowWidthSection(true);
        m_colorAndWidthWidget->setShowShapeSection(true);
        showWidget = true;
        break;

    case ButtonId::ButtonText:
        m_colorAndWidthWidget->setShowColorSection(true);
        m_colorAndWidthWidget->setShowTextSection(true);
        showWidget = true;
        break;

    case ButtonId::ButtonMosaic:
        // Use regular width section (same as Arrow/Shape), not slider
        m_colorAndWidthWidget->setShowWidthSection(true);
        m_colorAndWidthWidget->setShowAutoBlurSection(true);
        showWidget = true;
        break;

    case ButtonId::ButtonStepBadge:
        m_colorAndWidthWidget->setShowColorSection(true);
        m_colorAndWidthWidget->setShowSizeSection(true);
        showWidget = true;
        break;

    case ButtonId::ButtonEmoji:
        m_showingEmojiPicker = true;
        m_emojiPicker->setVisible(true);
        showWidget = true;
        break;

    case ButtonId::ButtonEraser:
        // Eraser has no sub-toolbar in RegionSelector (uses dynamic cursor only)
        qDebug() << "PinWindowSubToolbar: Eraser has no sub-toolbar, hiding";
        QWidget::hide();
        return;

    default:
        qDebug() << "PinWindowSubToolbar: unknown toolId, hiding";
        QWidget::hide();
        return;
    }

    qDebug() << "PinWindowSubToolbar: showWidget=" << showWidget;

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

        qDebug() << "PinWindowSubToolbar: calling updateSize()";
        updateSize();
        qDebug() << "PinWindowSubToolbar: size after updateSize=" << size();
        QWidget::show();
        raise();
        qDebug() << "PinWindowSubToolbar: shown, isVisible=" << isVisible()
                 << "geometry=" << geometry()
                 << "frameGeometry=" << frameGeometry();
    }
}

void PinWindowSubToolbar::hide()
{
    m_emojiPicker->setVisible(false);
    m_showingEmojiPicker = false;
    QWidget::hide();
}

void PinWindowSubToolbar::updateSize()
{
    qDebug() << "PinWindowSubToolbar::updateSize - showingEmojiPicker=" << m_showingEmojiPicker;
    if (m_showingEmojiPicker) {
        // EmojiPicker has its own padding, no extra margin needed
        QRect emojiRect = m_emojiPicker->boundingRect();
        qDebug() << "PinWindowSubToolbar: emojiRect=" << emojiRect;
        setFixedSize(emojiRect.width(), emojiRect.height());
    } else {
        // ColorAndWidthWidget has its own padding, no extra margin needed (like RegionSelector)
        QRect widgetRect = m_colorAndWidthWidget->boundingRect();
        // Dropdowns expand DOWNWARD, include dropdown height at bottom of window
        int dropdownHeight = m_colorAndWidthWidget->activeDropdownHeight();
        // Reserve space ABOVE content for tooltips (AutoBlurSection)
        int tooltipSpace = m_colorAndWidthWidget->showAutoBlurSection() ? TOOLTIP_HEIGHT : 0;
        // Reserve extra width for tooltip text ("Auto Blur Faces" is ~120px)
        int minWidth = m_colorAndWidthWidget->showAutoBlurSection() ? 130 : 0;
        int finalWidth = qMax(widgetRect.width(), minWidth);
        qDebug() << "PinWindowSubToolbar: colorAndWidthWidget boundingRect=" << widgetRect
                 << "dropdownHeight=" << dropdownHeight
                 << "tooltipSpace=" << tooltipSpace
                 << "finalWidth=" << finalWidth;
        setFixedSize(finalWidth, widgetRect.height() + dropdownHeight + tooltipSpace);
    }
    qDebug() << "PinWindowSubToolbar: final size=" << size();
}

void PinWindowSubToolbar::onDropdownStateChanged()
{
    // Dropdowns expand downward - just resize, no position change needed
    updateSize();
    update();
}

void PinWindowSubToolbar::paintEvent(QPaintEvent *event)
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
        // ColorAndWidthWidget draws its own glass panel background (like RegionSelector)
        // Reserve space for tooltip ABOVE content
        int tooltipSpace = m_colorAndWidthWidget->showAutoBlurSection() ? TOOLTIP_HEIGHT : 0;
        // Translate to compensate for boundingRect position and add tooltip space
        QRect widgetRect = m_colorAndWidthWidget->boundingRect();
        painter.translate(-widgetRect.x(), -widgetRect.y() + tooltipSpace);
        m_colorAndWidthWidget->draw(painter);
    }
}

void PinWindowSubToolbar::mousePressEvent(QMouseEvent *event)
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

void PinWindowSubToolbar::enterEvent(QEnterEvent *event)
{
    // If in annotation mode, use the annotation cursor
    if (m_hasAnnotationCursor) {
        setCursor(m_annotationCursor);
        QWidget::enterEvent(event);
        return;
    }

    // Set cursor immediately when mouse enters (mouseMoveEvent requires movement)
    setCursor(Qt::PointingHandCursor);
    QWidget::enterEvent(event);
}

void PinWindowSubToolbar::mouseMoveEvent(QMouseEvent *event)
{
    bool pressed = event->buttons() & Qt::LeftButton;

    // Don't change cursor when in annotation mode
    if (!m_hasAnnotationCursor) {
        // Keep pointing hand cursor for interactive elements
        setCursor(Qt::PointingHandCursor);
    }

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

void PinWindowSubToolbar::mouseReleaseEvent(QMouseEvent *event)
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

void PinWindowSubToolbar::wheelEvent(QWheelEvent *event)
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

void PinWindowSubToolbar::leaveEvent(QEvent *event)
{
    if (m_showingEmojiPicker) {
        m_emojiPicker->updateHoveredEmoji(QPoint(-1, -1));
    } else {
        m_colorAndWidthWidget->updateHovered(QPoint(-1, -1));
    }
    if (!m_hasAnnotationCursor) {
        setCursor(Qt::ArrowCursor);
    }
    emit cursorRestoreRequested();
    update();
    QWidget::leaveEvent(event);
}

void PinWindowSubToolbar::setAnnotationCursor(const QCursor& cursor)
{
    m_annotationCursor = cursor;
    m_hasAnnotationCursor = true;
    setCursor(cursor);
}

void PinWindowSubToolbar::clearAnnotationCursor()
{
    m_hasAnnotationCursor = false;
    setCursor(Qt::PointingHandCursor);
}
