#include "ScreenCanvas.h"
#include "AnnotationLayer.h"
#include "AnnotationController.h"
#include "IconRenderer.h"
#include "ColorPaletteWidget.h"
#include "ColorPickerDialog.h"

#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QDebug>

// Helper function to map CanvasTool to AnnotationController::Tool
static AnnotationController::Tool mapToControllerTool(CanvasTool tool)
{
    switch (tool) {
    case CanvasTool::Pencil:    return AnnotationController::Tool::Pencil;
    case CanvasTool::Marker:    return AnnotationController::Tool::Marker;
    case CanvasTool::Arrow:     return AnnotationController::Tool::Arrow;
    case CanvasTool::Rectangle: return AnnotationController::Tool::Rectangle;
    default:                    return AnnotationController::Tool::None;
    }
}

ScreenCanvas::ScreenCanvas(QWidget *parent)
    : QWidget(parent)
    , m_currentScreen(nullptr)
    , m_devicePixelRatio(1.0)
    , m_annotationLayer(nullptr)
    , m_controller(nullptr)
    , m_currentTool(CanvasTool::Pencil)
    , m_hoveredButton(-1)
    , m_colorPalette(nullptr)
    , m_colorPickerDialog(nullptr)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);

    // Initialize button rects
    m_buttonRects.resize(static_cast<int>(CanvasTool::Count));

    // Initialize annotation layer
    m_annotationLayer = new AnnotationLayer(this);

    // Initialize annotation controller
    m_controller = new AnnotationController(this);
    m_controller->setAnnotationLayer(m_annotationLayer);
    m_controller->setCurrentTool(mapToControllerTool(m_currentTool));
    m_controller->setColor(Qt::red);
    m_controller->setWidth(3);

    // Initialize SVG icons
    initializeIcons();

    // Initialize color palette widget
    m_colorPalette = new ColorPaletteWidget(this);
    m_colorPalette->setCurrentColor(m_controller->color());
    connect(m_colorPalette, &ColorPaletteWidget::colorSelected,
            this, &ScreenCanvas::onColorSelected);
    connect(m_colorPalette, &ColorPaletteWidget::moreColorsRequested,
            this, &ScreenCanvas::onMoreColorsRequested);

    qDebug() << "ScreenCanvas: Created";
}

ScreenCanvas::~ScreenCanvas()
{
    qDebug() << "ScreenCanvas: Destroyed";
}

void ScreenCanvas::initializeIcons()
{
    // Use shared IconRenderer for all icons
    IconRenderer& iconRenderer = IconRenderer::instance();

    iconRenderer.loadIcon("pencil", ":/icons/icons/pencil.svg");
    iconRenderer.loadIcon("marker", ":/icons/icons/marker.svg");
    iconRenderer.loadIcon("arrow", ":/icons/icons/arrow.svg");
    iconRenderer.loadIcon("rectangle", ":/icons/icons/rectangle.svg");
    iconRenderer.loadIcon("undo", ":/icons/icons/undo.svg");
    iconRenderer.loadIcon("redo", ":/icons/icons/redo.svg");
    iconRenderer.loadIcon("cancel", ":/icons/icons/cancel.svg");
}

QString ScreenCanvas::getIconKeyForTool(CanvasTool tool) const
{
    switch (tool) {
    case CanvasTool::Pencil:    return "pencil";
    case CanvasTool::Marker:    return "marker";
    case CanvasTool::Arrow:     return "arrow";
    case CanvasTool::Rectangle: return "rectangle";
    case CanvasTool::Undo:      return "undo";
    case CanvasTool::Redo:      return "redo";
    case CanvasTool::Clear:     return "cancel";
    case CanvasTool::Exit:      return "cancel";
    default:                    return QString();
    }
}

void ScreenCanvas::renderIcon(QPainter &painter, const QRect &rect, CanvasTool tool, const QColor &color)
{
    QString key = getIconKeyForTool(tool);
    IconRenderer::instance().renderIcon(painter, rect, key, color);
}

bool ScreenCanvas::shouldShowColorPalette() const
{
    // Show palette for color-enabled tools
    switch (m_currentTool) {
    case CanvasTool::Pencil:
    case CanvasTool::Marker:
    case CanvasTool::Arrow:
    case CanvasTool::Rectangle:
        return true;
    default:
        return false;
    }
}

void ScreenCanvas::onColorSelected(const QColor &color)
{
    m_controller->setColor(color);
    update();
}

void ScreenCanvas::onMoreColorsRequested()
{
    if (!m_colorPickerDialog) {
        m_colorPickerDialog = new ColorPickerDialog();
        connect(m_colorPickerDialog, &ColorPickerDialog::colorSelected,
                this, [this](const QColor &color) {
            m_controller->setColor(color);
            m_colorPalette->setCurrentColor(color);
            qDebug() << "ScreenCanvas: Custom color selected:" << color.name();
            update();
        });
    }

    m_colorPickerDialog->setCurrentColor(m_controller->color());

    // Position at center of screen
    QPoint center = geometry().center();
    m_colorPickerDialog->move(center.x() - 170, center.y() - 210);
    m_colorPickerDialog->show();
    m_colorPickerDialog->raise();
    m_colorPickerDialog->activateWindow();
}

void ScreenCanvas::initializeForScreen(QScreen *screen)
{
    m_currentScreen = screen;
    if (!m_currentScreen) {
        m_currentScreen = QGuiApplication::primaryScreen();
    }

    m_devicePixelRatio = m_currentScreen->devicePixelRatio();

    // Capture the screen
    m_backgroundPixmap = m_currentScreen->grabWindow(0);

    qDebug() << "ScreenCanvas: Initialized for screen" << m_currentScreen->name()
             << "logical size:" << m_currentScreen->geometry().size()
             << "pixmap size:" << m_backgroundPixmap.size()
             << "devicePixelRatio:" << m_devicePixelRatio;

    // Lock window size
    setFixedSize(m_currentScreen->geometry().size());

    // Update toolbar position
    updateToolbarPosition();
}

void ScreenCanvas::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::Antialiasing);

    // Draw background screenshot
    painter.drawPixmap(rect(), m_backgroundPixmap);

    // Draw completed annotations
    drawAnnotations(painter);

    // Draw annotation in progress (preview)
    drawCurrentAnnotation(painter);

    // Draw toolbar
    drawToolbar(painter);

    // Draw color palette
    if (shouldShowColorPalette()) {
        m_colorPalette->setVisible(true);
        m_colorPalette->updatePosition(m_toolbarRect, true);
        m_colorPalette->draw(painter);
    } else {
        m_colorPalette->setVisible(false);
    }

    // Draw tooltip
    drawTooltip(painter);
}

void ScreenCanvas::drawAnnotations(QPainter &painter)
{
    m_annotationLayer->draw(painter);
}

void ScreenCanvas::drawCurrentAnnotation(QPainter &painter)
{
    m_controller->drawCurrentAnnotation(painter);
}

void ScreenCanvas::drawToolbar(QPainter &painter)
{
    // Draw shadow
    QRect shadowRect = m_toolbarRect.adjusted(2, 2, 2, 2);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 50));
    painter.drawRoundedRect(shadowRect, 8, 8);

    // Draw toolbar background with gradient
    QLinearGradient gradient(m_toolbarRect.topLeft(), m_toolbarRect.bottomLeft());
    gradient.setColorAt(0, QColor(55, 55, 55, 245));
    gradient.setColorAt(1, QColor(40, 40, 40, 245));

    painter.setBrush(gradient);
    painter.setPen(QPen(QColor(70, 70, 70), 1));
    painter.drawRoundedRect(m_toolbarRect, 8, 8);

    // Render icons
    for (int i = 0; i < static_cast<int>(CanvasTool::Count); ++i) {
        QRect btnRect = m_buttonRects[i];
        CanvasTool tool = static_cast<CanvasTool>(i);

        // Highlight active tool (annotation tools only)
        bool isActive = (tool == m_currentTool) && isAnnotationTool(tool);
        if (isActive) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 120, 200));
            painter.drawRoundedRect(btnRect.adjusted(2, 2, -2, -2), 4, 4);
        } else if (i == m_hoveredButton) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(80, 80, 80));
            painter.drawRoundedRect(btnRect.adjusted(2, 2, -2, -2), 4, 4);
        }

        // Draw separator before Undo button
        if (i == static_cast<int>(CanvasTool::Undo)) {
            painter.setPen(QColor(80, 80, 80));
            painter.drawLine(btnRect.left() - 4, btnRect.top() + 6,
                             btnRect.left() - 4, btnRect.bottom() - 6);
        }

        // Draw separator before Exit button
        if (i == static_cast<int>(CanvasTool::Exit)) {
            painter.setPen(QColor(80, 80, 80));
            painter.drawLine(btnRect.left() - 4, btnRect.top() + 6,
                             btnRect.left() - 4, btnRect.bottom() - 6);
        }

        // Determine icon color
        QColor iconColor;
        if (tool == CanvasTool::Exit) {
            iconColor = QColor(255, 100, 100);  // Red for exit
        } else if (tool == CanvasTool::Clear) {
            iconColor = QColor(255, 180, 100);  // Orange for clear
        } else if (isActive) {
            iconColor = Qt::white;
        } else {
            iconColor = QColor(220, 220, 220);
        }

        renderIcon(painter, btnRect, tool, iconColor);
    }
}

void ScreenCanvas::updateToolbarPosition()
{
    int separatorCount = 2;  // Undo, Exit 前各有分隔線
    int toolbarWidth = static_cast<int>(CanvasTool::Count) * (BUTTON_WIDTH + BUTTON_SPACING) + 20 + separatorCount * 6;

    // Center horizontally, 30px from bottom
    int toolbarX = (width() - toolbarWidth) / 2;
    int toolbarY = height() - TOOLBAR_HEIGHT - 30;

    m_toolbarRect = QRect(toolbarX, toolbarY, toolbarWidth, TOOLBAR_HEIGHT);

    // Update button rects
    int x = toolbarX + 10;
    int y = toolbarY + (TOOLBAR_HEIGHT - BUTTON_WIDTH + 4) / 2;

    for (int i = 0; i < static_cast<int>(CanvasTool::Count); ++i) {
        m_buttonRects[i] = QRect(x, y, BUTTON_WIDTH, BUTTON_WIDTH - 4);
        x += BUTTON_WIDTH + BUTTON_SPACING;

        // Add extra spacing for separators
        if (i == static_cast<int>(CanvasTool::Rectangle) ||
            i == static_cast<int>(CanvasTool::Clear)) {
            x += 6;
        }
    }
}

int ScreenCanvas::getButtonAtPosition(const QPoint &pos)
{
    for (int i = 0; i < m_buttonRects.size(); ++i) {
        if (m_buttonRects[i].contains(pos)) {
            return i;
        }
    }
    return -1;
}

QString ScreenCanvas::getButtonTooltip(int buttonIndex)
{
    static const QStringList tooltips = {
        "Pencil",
        "Marker",
        "Arrow",
        "Rectangle",
        "Undo (Ctrl+Z)",
        "Redo (Ctrl+Y)",
        "Clear All",
        "Exit (Esc)"
    };

    if (buttonIndex >= 0 && buttonIndex < tooltips.size()) {
        return tooltips[buttonIndex];
    }
    return QString();
}

void ScreenCanvas::drawTooltip(QPainter &painter)
{
    if (m_hoveredButton < 0) return;

    QString tooltip = getButtonTooltip(m_hoveredButton);
    if (tooltip.isEmpty()) return;

    QFont font = painter.font();
    font.setPointSize(11);
    painter.setFont(font);

    QFontMetrics fm(font);
    QRect textRect = fm.boundingRect(tooltip);
    textRect.adjust(-8, -4, 8, 4);

    // Position tooltip above the toolbar
    QRect btnRect = m_buttonRects[m_hoveredButton];
    int tooltipX = btnRect.center().x() - textRect.width() / 2;
    int tooltipY = m_toolbarRect.top() - textRect.height() - 6;

    // Keep on screen
    if (tooltipX < 5) tooltipX = 5;
    if (tooltipX + textRect.width() > width() - 5) {
        tooltipX = width() - textRect.width() - 5;
    }

    textRect.moveTo(tooltipX, tooltipY);

    // Draw tooltip background
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(30, 30, 30, 230));
    painter.drawRoundedRect(textRect, 4, 4);

    // Draw tooltip border
    painter.setPen(QColor(80, 80, 80));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(textRect, 4, 4);

    // Draw tooltip text
    painter.setPen(Qt::white);
    painter.drawText(textRect, Qt::AlignCenter, tooltip);
}

void ScreenCanvas::handleToolbarClick(CanvasTool button)
{
    switch (button) {
    case CanvasTool::Pencil:
    case CanvasTool::Marker:
    case CanvasTool::Arrow:
    case CanvasTool::Rectangle:
        m_currentTool = button;
        m_controller->setCurrentTool(mapToControllerTool(button));
        qDebug() << "ScreenCanvas: Tool selected:" << static_cast<int>(button);
        update();
        break;

    case CanvasTool::Undo:
        if (m_annotationLayer->canUndo()) {
            m_annotationLayer->undo();
            qDebug() << "ScreenCanvas: Undo";
            update();
        }
        break;

    case CanvasTool::Redo:
        if (m_annotationLayer->canRedo()) {
            m_annotationLayer->redo();
            qDebug() << "ScreenCanvas: Redo";
            update();
        }
        break;

    case CanvasTool::Clear:
        m_annotationLayer->clear();
        qDebug() << "ScreenCanvas: Clear all annotations";
        update();
        break;

    case CanvasTool::Exit:
        close();
        break;

    default:
        break;
    }
}

bool ScreenCanvas::isAnnotationTool(CanvasTool tool) const
{
    switch (tool) {
    case CanvasTool::Pencil:
    case CanvasTool::Marker:
    case CanvasTool::Arrow:
    case CanvasTool::Rectangle:
        return true;
    default:
        return false;
    }
}

void ScreenCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // Check if clicked on color palette
        if (shouldShowColorPalette() && m_colorPalette->handleClick(event->pos())) {
            update();
            return;
        }

        // Check if clicked on toolbar
        int buttonIdx = getButtonAtPosition(event->pos());
        if (buttonIdx >= 0) {
            handleToolbarClick(static_cast<CanvasTool>(buttonIdx));
            return;
        }

        // Start annotation drawing
        if (isAnnotationTool(m_currentTool)) {
            m_controller->startDrawing(event->pos());
            update();
        }
    } else if (event->button() == Qt::RightButton) {
        // Cancel current annotation
        if (m_controller->isDrawing()) {
            m_controller->cancelDrawing();
            update();
        }
    }
}

void ScreenCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (m_controller->isDrawing()) {
        m_controller->updateDrawing(event->pos());
        update();
    } else {
        bool needsUpdate = false;

        // Update hovered color swatch
        if (shouldShowColorPalette()) {
            if (m_colorPalette->updateHoveredSwatch(event->pos())) {
                needsUpdate = true;
                if (m_colorPalette->contains(event->pos())) {
                    setCursor(Qt::PointingHandCursor);
                }
            }
        }

        // Update hovered button
        int newHovered = getButtonAtPosition(event->pos());
        if (newHovered != m_hoveredButton) {
            m_hoveredButton = newHovered;
            needsUpdate = true;
            if (m_hoveredButton >= 0) {
                setCursor(Qt::PointingHandCursor);
            } else if (shouldShowColorPalette() && m_colorPalette->contains(event->pos())) {
                // Already set cursor for color swatch
            } else {
                setCursor(Qt::CrossCursor);
            }
        }

        if (needsUpdate) {
            update();
        }
    }
}

void ScreenCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_controller->isDrawing()) {
        m_controller->finishDrawing();
        update();
    }
}

void ScreenCanvas::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        qDebug() << "ScreenCanvas: Closed via Escape";
        close();
    } else if (event->matches(QKeySequence::Undo)) {
        if (m_annotationLayer->canUndo()) {
            m_annotationLayer->undo();
            update();
        }
    } else if (event->matches(QKeySequence::Redo)) {
        if (m_annotationLayer->canRedo()) {
            m_annotationLayer->redo();
            update();
        }
    }
}

void ScreenCanvas::closeEvent(QCloseEvent *event)
{
    emit closed();
    QWidget::closeEvent(event);
}
