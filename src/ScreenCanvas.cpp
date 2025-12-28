#include "ScreenCanvas.h"
#include "annotations/AnnotationLayer.h"
#include "tools/ToolManager.h"
#include "IconRenderer.h"
#include "GlassRenderer.h"
#include "ColorPaletteWidget.h"
#include "ColorPickerDialog.h"
#include "LineWidthWidget.h"
#include "ColorAndWidthWidget.h"
#include "LaserPointerRenderer.h"
#include "ClickRippleRenderer.h"
#include "settings/AnnotationSettingsManager.h"
#include "settings/Settings.h"

#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QDebug>
#include <QSettings>
#include <map>

// Tool capability lookup table - replaces multiple switch statements
struct ToolCapabilities {
    bool showInPalette;     // Show in color palette
    bool needsWidth;        // Show width control
    bool needsColorOrWidth; // Show unified color/width widget
};

static const std::map<ToolId, ToolCapabilities> kToolCapabilities = {
    {ToolId::Pencil,       {true,  true,  true}},
    {ToolId::Marker,       {true,  false, true}},
    {ToolId::Arrow,        {true,  true,  true}},
    {ToolId::Shape,        {true,  true,  true}},
    {ToolId::LaserPointer, {true,  true,  true}},
};

ScreenCanvas::ScreenCanvas(QWidget* parent)
    : QWidget(parent)
    , m_currentScreen(nullptr)
    , m_devicePixelRatio(1.0)
    , m_annotationLayer(nullptr)
    , m_toolManager(nullptr)
    , m_currentToolId(ToolId::Pencil)
    , m_hoveredButton(-1)
    , m_colorPalette(nullptr)
    , m_lineWidthWidget(nullptr)
    , m_colorPickerDialog(nullptr)
    , m_toolbarStyleConfig(ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle()))
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);

    // Initialize button rects
    m_buttonRects.resize(static_cast<int>(CanvasButton::Count));

    // Initialize annotation layer
    m_annotationLayer = new AnnotationLayer(this);

    // Initialize tool manager with saved settings
    m_toolManager = new ToolManager(this);
    m_toolManager->registerDefaultHandlers();
    m_toolManager->setAnnotationLayer(m_annotationLayer);
    m_toolManager->setCurrentTool(m_currentToolId);

    // Load saved annotation settings (or defaults)
    auto& annotationSettings = AnnotationSettingsManager::instance();
    QColor savedColor = annotationSettings.loadColor();
    int savedWidth = annotationSettings.loadWidth();
    LineEndStyle savedArrowStyle = loadArrowStyle();
    LineStyle savedLineStyle = loadLineStyle();
    bool savedPolylineMode = annotationSettings.loadPolylineMode();
    m_toolManager->setColor(savedColor);
    m_toolManager->setWidth(savedWidth);
    m_toolManager->setArrowStyle(savedArrowStyle);
    m_toolManager->setLineStyle(savedLineStyle);
    m_toolManager->setPolylineMode(savedPolylineMode);

    // Connect tool manager signals
    connect(m_toolManager, &ToolManager::needsRepaint, this, QOverload<>::of(&QWidget::update));

    // Initialize SVG icons
    initializeIcons();

    // Initialize color palette widget
    m_colorPalette = new ColorPaletteWidget(this);
    m_colorPalette->setCurrentColor(savedColor);
    connect(m_colorPalette, &ColorPaletteWidget::colorSelected,
        this, &ScreenCanvas::onColorSelected);
    connect(m_colorPalette, &ColorPaletteWidget::moreColorsRequested,
        this, &ScreenCanvas::onMoreColorsRequested);

    // Initialize line width widget
    m_lineWidthWidget = new LineWidthWidget(this);
    m_lineWidthWidget->setWidthRange(1, 20);
    m_lineWidthWidget->setCurrentWidth(savedWidth);
    m_lineWidthWidget->setPreviewColor(savedColor);
    connect(m_lineWidthWidget, &LineWidthWidget::widthChanged,
        this, &ScreenCanvas::onLineWidthChanged);

    // Initialize unified color and width widget
    m_colorAndWidthWidget = new ColorAndWidthWidget(this);
    m_colorAndWidthWidget->setCurrentColor(savedColor);
    m_colorAndWidthWidget->setCurrentWidth(savedWidth);
    m_colorAndWidthWidget->setWidthRange(1, 20);
    m_colorAndWidthWidget->setArrowStyle(savedArrowStyle);
    m_colorAndWidthWidget->setLineStyle(savedLineStyle);
    m_colorAndWidthWidget->setPolylineMode(savedPolylineMode);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::colorSelected,
        this, &ScreenCanvas::onColorSelected);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::moreColorsRequested,
        this, &ScreenCanvas::onMoreColorsRequested);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::widthChanged,
        this, &ScreenCanvas::onLineWidthChanged);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::arrowStyleChanged,
        this, &ScreenCanvas::onArrowStyleChanged);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::lineStyleChanged,
        this, &ScreenCanvas::onLineStyleChanged);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::polylineModeChanged,
        this, [this](bool enabled) {
            m_toolManager->setPolylineMode(enabled);
            AnnotationSettingsManager::instance().savePolylineMode(enabled);
        });

    // Initialize laser pointer renderer
    m_laserRenderer = new LaserPointerRenderer(this);
    m_laserRenderer->setColor(savedColor);
    m_laserRenderer->setWidth(savedWidth);
    connect(m_laserRenderer, &LaserPointerRenderer::needsRepaint,
        this, QOverload<>::of(&QWidget::update));

    // Initialize click ripple renderer
    m_rippleRenderer = new ClickRippleRenderer(this);
    connect(m_rippleRenderer, &ClickRippleRenderer::needsRepaint,
        this, QOverload<>::of(&QWidget::update));

    qDebug() << "ScreenCanvas: Created";
}

ScreenCanvas::~ScreenCanvas()
{
    delete m_colorPickerDialog;
    qDebug() << "ScreenCanvas: Destroyed";
}

void ScreenCanvas::initializeIcons()
{
    // Use shared IconRenderer for all icons
    IconRenderer& iconRenderer = IconRenderer::instance();

    iconRenderer.loadIcon("pencil", ":/icons/icons/pencil.svg");
    iconRenderer.loadIcon("marker", ":/icons/icons/marker.svg");
    iconRenderer.loadIcon("arrow", ":/icons/icons/arrow.svg");
    iconRenderer.loadIcon("polyline", ":/icons/icons/polyline.svg");
    iconRenderer.loadIcon("rectangle", ":/icons/icons/rectangle.svg");
    iconRenderer.loadIcon("ellipse", ":/icons/icons/ellipse.svg");
    iconRenderer.loadIcon("shape", ":/icons/icons/shape.svg");
    iconRenderer.loadIcon("laser-pointer", ":/icons/icons/laser-pointer.svg");
    iconRenderer.loadIcon("cursor-highlight", ":/icons/icons/cursor-highlight.svg");
    iconRenderer.loadIcon("undo", ":/icons/icons/undo.svg");
    iconRenderer.loadIcon("redo", ":/icons/icons/redo.svg");
    iconRenderer.loadIcon("cancel", ":/icons/icons/cancel.svg");
}

QString ScreenCanvas::getIconKeyForButton(CanvasButton button) const
{
    switch (button) {
    case CanvasButton::Pencil:          return "pencil";
    case CanvasButton::Marker:          return "marker";
    case CanvasButton::Arrow:           return "arrow";
    case CanvasButton::Shape:           return "shape";
    case CanvasButton::LaserPointer:    return "laser-pointer";
    case CanvasButton::CursorHighlight: return "cursor-highlight";
    case CanvasButton::Undo:            return "undo";
    case CanvasButton::Redo:            return "redo";
    case CanvasButton::Clear:           return "cancel";
    case CanvasButton::Exit:            return "cancel";
    default:                            return QString();
    }
}

void ScreenCanvas::renderIcon(QPainter& painter, const QRect& rect, CanvasButton button, const QColor& color)
{
    QString key = getIconKeyForButton(button);
    IconRenderer::instance().renderIcon(painter, rect, key, color);
}

bool ScreenCanvas::shouldShowColorPalette() const
{
    auto it = kToolCapabilities.find(m_currentToolId);
    return it != kToolCapabilities.end() && it->second.showInPalette;
}

void ScreenCanvas::onColorSelected(const QColor& color)
{
    m_toolManager->setColor(color);
    m_laserRenderer->setColor(color);
    m_lineWidthWidget->setPreviewColor(color);
    m_colorAndWidthWidget->setCurrentColor(color);
    AnnotationSettingsManager::instance().saveColor(color);
    update();
}

bool ScreenCanvas::shouldShowLineWidthWidget() const
{
    auto it = kToolCapabilities.find(m_currentToolId);
    return it != kToolCapabilities.end() && it->second.needsWidth;
}

void ScreenCanvas::onLineWidthChanged(int width)
{
    m_toolManager->setWidth(width);
    m_laserRenderer->setWidth(width);
    AnnotationSettingsManager::instance().saveWidth(width);
    update();
}

bool ScreenCanvas::shouldShowColorAndWidthWidget() const
{
    auto it = kToolCapabilities.find(m_currentToolId);
    return it != kToolCapabilities.end() && it->second.needsColorOrWidth;
}

bool ScreenCanvas::shouldShowWidthControl() const
{
    auto it = kToolCapabilities.find(m_currentToolId);
    return it != kToolCapabilities.end() && it->second.needsWidth;
}

void ScreenCanvas::onMoreColorsRequested()
{
    if (!m_colorPickerDialog) {
        m_colorPickerDialog = new ColorPickerDialog();
        connect(m_colorPickerDialog, &ColorPickerDialog::colorSelected,
            this, [this](const QColor& color) {
                m_toolManager->setColor(color);
                m_colorPalette->setCurrentColor(color);
                m_lineWidthWidget->setPreviewColor(color);
                m_colorAndWidthWidget->setCurrentColor(color);
                qDebug() << "ScreenCanvas: Custom color selected:" << color.name();
                update();
            });
    }

    m_colorPickerDialog->setCurrentColor(m_toolManager->color());

    // Ensure unified color/width widget is in sync with the tool manager color before showing the dialog
    m_colorAndWidthWidget->setCurrentColor(m_toolManager->color());

    // Position at center of screen
    QPoint center = geometry().center();
    m_colorPickerDialog->move(center.x() - 170, center.y() - 210);
    m_colorPickerDialog->show();
    m_colorPickerDialog->raise();
    m_colorPickerDialog->activateWindow();
}

void ScreenCanvas::initializeForScreen(QScreen* screen)
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

void ScreenCanvas::paintEvent(QPaintEvent*)
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

    // Draw laser pointer trail
    m_laserRenderer->draw(painter);

    // Draw click ripple effects
    m_rippleRenderer->draw(painter);

    // Draw toolbar
    drawToolbar(painter);

    // Use unified color and width widget
    if (shouldShowColorAndWidthWidget()) {
        m_colorAndWidthWidget->setVisible(true);
        m_colorAndWidthWidget->setShowWidthSection(shouldShowWidthControl());
        // Show arrow style section only for Arrow tool
        m_colorAndWidthWidget->setShowArrowStyleSection(m_currentToolId == ToolId::Arrow);
        // Show line style section for Pencil and Arrow tools
        bool showLineStyle = (m_currentToolId == ToolId::Pencil ||
                              m_currentToolId == ToolId::Arrow);
        m_colorAndWidthWidget->setShowLineStyleSection(showLineStyle);
        m_colorAndWidthWidget->updatePosition(m_toolbarRect, true, width());
        m_colorAndWidthWidget->draw(painter);
    }
    else {
        m_colorAndWidthWidget->setVisible(false);
    }

    // Legacy widgets (keep for compatibility, but hidden when unified widget is shown)
    if (!shouldShowColorAndWidthWidget()) {
        // Draw color palette
        if (shouldShowColorPalette()) {
            m_colorPalette->setVisible(true);
            m_colorPalette->updatePosition(m_toolbarRect, true);
            m_colorPalette->draw(painter);
        }
        else {
            m_colorPalette->setVisible(false);
        }

        // Draw line width widget (above color palette, aligned to toolbar left edge)
        if (shouldShowLineWidthWidget()) {
            m_lineWidthWidget->setVisible(true);
            QRect anchorRect = m_colorPalette->boundingRect();
            anchorRect.moveLeft(m_toolbarRect.left());  // Align with toolbar left edge
            m_lineWidthWidget->updatePosition(anchorRect, true, width());
            m_lineWidthWidget->draw(painter);
        }
        else {
            m_lineWidthWidget->setVisible(false);
        }
    }

    // Draw tooltip
    drawTooltip(painter);

    // Draw cursor dot
    drawCursorDot(painter);
}

void ScreenCanvas::drawAnnotations(QPainter& painter)
{
    m_annotationLayer->draw(painter);
}

void ScreenCanvas::drawCurrentAnnotation(QPainter& painter)
{
    m_toolManager->drawCurrentPreview(painter);
}

void ScreenCanvas::drawToolbar(QPainter& painter)
{
    // Draw glass panel (shadow, background, border, highlight)
    GlassRenderer::drawGlassPanel(painter, m_toolbarRect, m_toolbarStyleConfig);

    // Render icons
    for (int i = 0; i < static_cast<int>(CanvasButton::Count); ++i) {
        QRect btnRect = m_buttonRects[i];
        CanvasButton button = static_cast<CanvasButton>(i);
        ToolId buttonToolId = canvasButtonToToolId(button);

        // Highlight active tool (drawing tools only) or toggle state for CursorHighlight
        bool isActive = (buttonToolId == m_currentToolId) && isDrawingTool(buttonToolId);
        bool isToggleActive = (button == CanvasButton::CursorHighlight) && m_rippleRenderer->isEnabled();

        // Draw separator before certain buttons
        if (i == static_cast<int>(CanvasButton::CursorHighlight) ||
            i == static_cast<int>(CanvasButton::Undo) ||
            i == static_cast<int>(CanvasButton::Exit)) {
            painter.setPen(m_toolbarStyleConfig.separatorColor);
            painter.drawLine(btnRect.left() - 4, btnRect.top() + 6,
                btnRect.left() - 4, btnRect.bottom() - 6);
        }

        if (isActive || isToggleActive) {
            if (m_toolbarStyleConfig.useRedDotIndicator) {
                // Light style: draw red dot at top-right corner
                int dotRadius = m_toolbarStyleConfig.redDotSize / 2;
                int dotX = btnRect.right() - dotRadius - 2;
                int dotY = btnRect.top() + dotRadius + 2;
                painter.setPen(Qt::NoPen);
                painter.setBrush(m_toolbarStyleConfig.redDotColor);
                painter.drawEllipse(QPoint(dotX, dotY), dotRadius, dotRadius);
            } else {
                // Dark style: background highlight
                painter.setPen(Qt::NoPen);
                painter.setBrush(m_toolbarStyleConfig.activeBackgroundColor);
                painter.drawRoundedRect(btnRect.adjusted(2, 2, -2, -2), 6, 6);
            }
        }
        else if (i == m_hoveredButton) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_toolbarStyleConfig.hoverBackgroundColor);
            painter.drawRoundedRect(btnRect.adjusted(2, 2, -2, -2), 6, 6);
        }

        // Determine icon color
        QColor iconColor;
        if (button == CanvasButton::Exit) {
            iconColor = m_toolbarStyleConfig.iconCancelColor;
        }
        else if (button == CanvasButton::Clear) {
            iconColor = QColor(255, 180, 100);  // Orange for clear (keep as special color)
        }
        else if (isActive || isToggleActive) {
            iconColor = m_toolbarStyleConfig.iconActiveColor;
        }
        else {
            iconColor = m_toolbarStyleConfig.iconNormalColor;
        }

        renderIcon(painter, btnRect, button, iconColor);
    }
}

void ScreenCanvas::updateToolbarPosition()
{
    int separatorCount = 3;  // CursorHighlight, Undo, Exit 前各有分隔線
    int toolbarWidth = static_cast<int>(CanvasButton::Count) * (BUTTON_WIDTH + BUTTON_SPACING) + 20 + separatorCount * 6;

    // Center horizontally, 30px from bottom
    int toolbarX = (width() - toolbarWidth) / 2;
    int toolbarY = height() - TOOLBAR_HEIGHT - 30;

    m_toolbarRect = QRect(toolbarX, toolbarY, toolbarWidth, TOOLBAR_HEIGHT);

    // Update button rects
    int x = toolbarX + 10;
    int y = toolbarY + (TOOLBAR_HEIGHT - BUTTON_WIDTH + 4) / 2;

    for (int i = 0; i < static_cast<int>(CanvasButton::Count); ++i) {
        m_buttonRects[i] = QRect(x, y, BUTTON_WIDTH, BUTTON_WIDTH - 4);
        x += BUTTON_WIDTH + BUTTON_SPACING;

        // Add extra spacing for separators
        if (i == static_cast<int>(CanvasButton::LaserPointer) ||
            i == static_cast<int>(CanvasButton::CursorHighlight) ||
            i == static_cast<int>(CanvasButton::Clear)) {
            x += 6;
        }
    }
}

int ScreenCanvas::getButtonAtPosition(const QPoint& pos)
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
        "Shape",  // Unified Rectangle/Ellipse
        "Laser Pointer",
        "Cursor Highlight (Toggle)",
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

void ScreenCanvas::drawTooltip(QPainter& painter)
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

void ScreenCanvas::handleToolbarClick(CanvasButton button)
{
    ToolId toolId = canvasButtonToToolId(button);

    switch (button) {
    case CanvasButton::Pencil:
    case CanvasButton::Marker:
    case CanvasButton::Arrow:
    case CanvasButton::Shape:
        m_currentToolId = toolId;
        m_toolManager->setCurrentTool(toolId);
        qDebug() << "ScreenCanvas: Tool selected:" << static_cast<int>(toolId);
        update();
        break;

    case CanvasButton::LaserPointer:
        m_currentToolId = toolId;
        m_toolManager->setCurrentTool(toolId);
        // Sync laser pointer with current color and width settings
        m_laserRenderer->setColor(m_toolManager->color());
        m_laserRenderer->setWidth(m_toolManager->width());
        qDebug() << "ScreenCanvas: Laser Pointer selected";
        update();
        break;

    case CanvasButton::CursorHighlight:
        // Toggle cursor highlight (not a drawing tool)
        m_rippleRenderer->setEnabled(!m_rippleRenderer->isEnabled());
        qDebug() << "ScreenCanvas: Cursor Highlight" << (m_rippleRenderer->isEnabled() ? "enabled" : "disabled");
        update();
        break;

    case CanvasButton::Undo:
        if (m_annotationLayer->canUndo()) {
            m_annotationLayer->undo();
            qDebug() << "ScreenCanvas: Undo";
            update();
        }
        break;

    case CanvasButton::Redo:
        if (m_annotationLayer->canRedo()) {
            m_annotationLayer->redo();
            qDebug() << "ScreenCanvas: Redo";
            update();
        }
        break;

    case CanvasButton::Clear:
        m_annotationLayer->clear();
        qDebug() << "ScreenCanvas: Clear all annotations";
        update();
        break;

    case CanvasButton::Exit:
        close();
        break;

    default:
        break;
    }
}

bool ScreenCanvas::isDrawingTool(ToolId toolId) const
{
    switch (toolId) {
    case ToolId::Pencil:
    case ToolId::Marker:
    case ToolId::Arrow:
    case ToolId::Shape:
    case ToolId::LaserPointer:
    case ToolId::Mosaic:
    case ToolId::Eraser:
        return true;
    default:
        return false;
    }
}

void ScreenCanvas::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        auto finalizePolylineForUiClick = [&](const QPoint& pos) {
            if (m_currentToolId == ToolId::Arrow &&
                m_toolManager->polylineMode() &&
                m_toolManager->isDrawing()) {
                m_toolManager->handleDoubleClick(pos);
            }
        };

        // Check if clicked on toolbar FIRST (before widgets that may overlap)
        int buttonIdx = getButtonAtPosition(event->pos());
        if (buttonIdx >= 0) {
            finalizePolylineForUiClick(event->pos());
            handleToolbarClick(static_cast<CanvasButton>(buttonIdx));
            return;
        }

        // Check if clicked on unified color and width widget
        if (shouldShowColorAndWidthWidget()) {
            if (m_colorAndWidthWidget->contains(event->pos())) {
                finalizePolylineForUiClick(event->pos());
            }
            if (m_colorAndWidthWidget->handleClick(event->pos())) {
                update();
                return;
            }
        }

        // Legacy widgets (only handle if unified widget not shown)
        if (!shouldShowColorAndWidthWidget()) {
            // Check if clicked on line width widget
            if (shouldShowLineWidthWidget()) {
                if (m_lineWidthWidget->contains(event->pos())) {
                    finalizePolylineForUiClick(event->pos());
                }
                if (m_lineWidthWidget->handleMousePress(event->pos())) {
                    update();
                    return;
                }
            }

            // Check if clicked on color palette
            if (shouldShowColorPalette()) {
                if (m_colorPalette->contains(event->pos())) {
                    finalizePolylineForUiClick(event->pos());
                }
                if (m_colorPalette->handleClick(event->pos())) {
                    update();
                    return;
                }
            }
        }

        // Trigger click ripple if enabled
        if (m_rippleRenderer->isEnabled()) {
            m_rippleRenderer->triggerRipple(event->pos());
        }

        // Handle laser pointer drawing
        if (m_currentToolId == ToolId::LaserPointer) {
            m_laserRenderer->startDrawing(event->pos());
            update();
            return;
        }

        // Start annotation drawing
        if (isDrawingTool(m_currentToolId)) {
            m_toolManager->handleMousePress(event->pos());
            update();
        }
    }
    else if (event->button() == Qt::RightButton) {
        // Cancel current annotation
        if (m_toolManager->isDrawing()) {
            m_toolManager->cancelDrawing();
            update();
        }
    }
}

void ScreenCanvas::mouseMoveEvent(QMouseEvent* event)
{
    // Track cursor position for cursor dot
    QPoint oldCursorPos = m_cursorPos;
    m_cursorPos = event->pos();

    // Handle laser pointer drawing
    if (m_currentToolId == ToolId::LaserPointer && m_laserRenderer->isDrawing()) {
        m_laserRenderer->updateDrawing(event->pos());
        update();
        return;
    }

    if (m_toolManager->isDrawing()) {
        m_toolManager->handleMouseMove(event->pos());
        update();
    }
    else {
        bool needsUpdate = false;
        bool widgetHovered = false;

        // Handle unified color and width widget
        if (shouldShowColorAndWidthWidget()) {
            if (m_colorAndWidthWidget->handleMouseMove(event->pos(), event->buttons() & Qt::LeftButton)) {
                update();
                return;
            }
            if (m_colorAndWidthWidget->updateHovered(event->pos())) {
                needsUpdate = true;
            }
            if (m_colorAndWidthWidget->contains(event->pos())) {
                setCursor(Qt::PointingHandCursor);
                widgetHovered = true;
            }
        }

        // Legacy widgets (only handle if unified widget not shown)
        if (!shouldShowColorAndWidthWidget()) {
            // Handle line width widget drag
            if (shouldShowLineWidthWidget()) {
                if (m_lineWidthWidget->handleMouseMove(event->pos(), event->buttons() & Qt::LeftButton)) {
                    update();
                    return;
                }
                if (m_lineWidthWidget->contains(event->pos())) {
                    setCursor(Qt::PointingHandCursor);
                    update();
                    return;
                }
            }

            // Update hovered color swatch
            if (shouldShowColorPalette()) {
                if (m_colorPalette->updateHoveredSwatch(event->pos())) {
                    needsUpdate = true;
                    if (m_colorPalette->contains(event->pos())) {
                        setCursor(Qt::PointingHandCursor);
                        widgetHovered = true;
                    }
                }
            }
        }

        // Update hovered button
        int newHovered = getButtonAtPosition(event->pos());
        if (newHovered != m_hoveredButton) {
            m_hoveredButton = newHovered;
            needsUpdate = true;
        }

        // Always update cursor based on current state
        if (m_hoveredButton >= 0) {
            setCursor(Qt::PointingHandCursor);
        }
        else if (widgetHovered) {
            setCursor(Qt::PointingHandCursor);
        }
        else if (m_currentToolId == ToolId::Mosaic || m_currentToolId == ToolId::Eraser) {
            setCursor(Qt::BlankCursor);
        }
        else {
            setCursor(Qt::ArrowCursor);
        }

        if (needsUpdate) {
            update();
        }
    }

    // Only update cursor dot region if position changed (performance optimization)
    // Cursor dot is 6px diameter, use 10px margin for safety
    if (m_cursorPos != oldCursorPos) {
        int cursorMargin = 10;
        if (m_currentToolId == ToolId::Mosaic || m_currentToolId == ToolId::Eraser) {
            cursorMargin = qMax(10, m_toolManager->width() / 2 + 5);
        }

        update(QRect(oldCursorPos.x() - cursorMargin, oldCursorPos.y() - cursorMargin,
            cursorMargin * 2, cursorMargin * 2));
        update(QRect(m_cursorPos.x() - cursorMargin, m_cursorPos.y() - cursorMargin,
            cursorMargin * 2, cursorMargin * 2));
    }
}

void ScreenCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        // Handle unified widget release
        if (shouldShowColorAndWidthWidget() && m_colorAndWidthWidget->handleMouseRelease(event->pos())) {
            update();
            return;
        }

        // Handle line width widget release (legacy)
        if (!shouldShowColorAndWidthWidget() && shouldShowLineWidthWidget() && m_lineWidthWidget->handleMouseRelease(event->pos())) {
            update();
            return;
        }

        // Handle laser pointer release
        if (m_currentToolId == ToolId::LaserPointer && m_laserRenderer->isDrawing()) {
            m_laserRenderer->stopDrawing();
            update();
            return;
        }

        // Finish drawing annotation
        if (m_toolManager->isDrawing()) {
            m_toolManager->handleMouseRelease(event->pos());
            update();
        }
    }
}

void ScreenCanvas::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        return;
    }

    // Forward double-click to tool manager
    m_toolManager->handleDoubleClick(event->pos());
    update();
}

void ScreenCanvas::wheelEvent(QWheelEvent* event)
{
    // Forward wheel events when tools that support width adjustment are active
    if (shouldShowColorAndWidthWidget()) {
        if (m_colorAndWidthWidget->handleWheel(event->angleDelta().y())) {
            update();
            event->accept();
            return;
        }
    }

    event->ignore();
}

void ScreenCanvas::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        qDebug() << "ScreenCanvas: Closed via Escape";
        close();
    }
    else if (event->matches(QKeySequence::Undo)) {
        if (m_annotationLayer->canUndo()) {
            m_annotationLayer->undo();
            update();
        }
    }
    else if (event->matches(QKeySequence::Redo)) {
        if (m_annotationLayer->canRedo()) {
            m_annotationLayer->redo();
            update();
        }
    }
}

void ScreenCanvas::drawCursorDot(QPainter& painter)
{
    bool isMosaicOrEraser = (m_currentToolId == ToolId::Mosaic || m_currentToolId == ToolId::Eraser);

    // Don't show when drawing (unless it's Mosaic/Eraser), on toolbar, or on widgets
    if (m_toolManager->isDrawing() && !isMosaicOrEraser) return;
    if (m_laserRenderer->isDrawing()) return;
    if (m_toolbarRect.contains(m_cursorPos)) return;
    if (m_hoveredButton >= 0) return;
    if (shouldShowColorAndWidthWidget() && m_colorAndWidthWidget->contains(m_cursorPos)) return;

    if (isMosaicOrEraser) {
        // Draw rounded square for Mosaic/Eraser
        int size = m_toolManager->width();
        QRect rect(0, 0, size, size);
        rect.moveCenter(m_cursorPos);

        painter.setPen(QPen(Qt::black, 1)); // Black outline
        painter.setBrush(Qt::NoBrush);      // Transparent fill
        painter.drawRoundedRect(rect, 2, 2); // Slight rounding
    }
    else {
        // Draw a dot following the current tool color
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_toolManager->color());
        painter.drawEllipse(m_cursorPos, 3, 3);  // 6px diameter
    }
}

void ScreenCanvas::closeEvent(QCloseEvent* event)
{
    emit closed();
    QWidget::closeEvent(event);
}

LineEndStyle ScreenCanvas::loadArrowStyle() const
{
    auto settings = SnapTray::getSettings();
    int style = settings.value("annotation/arrowStyle", static_cast<int>(LineEndStyle::EndArrow)).toInt();
    return static_cast<LineEndStyle>(style);
}

void ScreenCanvas::saveArrowStyle(LineEndStyle style)
{
    auto settings = SnapTray::getSettings();
    settings.setValue("annotation/arrowStyle", static_cast<int>(style));
}

void ScreenCanvas::onArrowStyleChanged(LineEndStyle style)
{
    m_toolManager->setArrowStyle(style);
    saveArrowStyle(style);
    update();
}

LineStyle ScreenCanvas::loadLineStyle() const
{
    auto settings = SnapTray::getSettings();
    int style = settings.value("annotation/lineStyle", static_cast<int>(LineStyle::Solid)).toInt();
    return static_cast<LineStyle>(style);
}

void ScreenCanvas::saveLineStyle(LineStyle style)
{
    auto settings = SnapTray::getSettings();
    settings.setValue("annotation/lineStyle", static_cast<int>(style));
}

void ScreenCanvas::onLineStyleChanged(LineStyle style)
{
    m_toolManager->setLineStyle(style);
    saveLineStyle(style);
    update();
}
