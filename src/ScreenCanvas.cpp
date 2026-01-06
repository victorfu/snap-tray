#include "ScreenCanvas.h"
#include "annotations/AnnotationLayer.h"
#include "tools/ToolManager.h"
#include "IconRenderer.h"
#include "GlassRenderer.h"
#include "ColorPaletteWidget.h"
#include "ColorPickerDialog.h"
#include "ColorAndWidthWidget.h"
#include "EmojiPicker.h"
#include "LaserPointerRenderer.h"
#include "ClickRippleRenderer.h"
#include "ToolbarWidget.h"
#include "settings/AnnotationSettingsManager.h"
#include "settings/Settings.h"
#include "tools/handlers/MosaicToolHandler.h"
#include "tools/handlers/EmojiStickerToolHandler.h"

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
    {ToolId::Mosaic,       {false, true,  true}},
    {ToolId::StepBadge,    {true,  false, true}},
    {ToolId::Text,         {true,  false, true}},
    {ToolId::LaserPointer, {true,  true,  true}},
};

// Create a rounded square cursor for mosaic tool (matching RegionSelector pattern)
static QCursor createMosaicCursor(int size) {
    // Use same pattern as EraserToolHandler - no devicePixelRatio scaling
    int cursorSize = size + 4;  // Add margin for border
    QPixmap pixmap(cursorSize, cursorSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    int center = cursorSize / 2;
    int halfSize = size / 2;

    // Draw semi-transparent fill
    painter.setBrush(QColor(255, 255, 255, 60));
    painter.setPen(Qt::NoPen);
    QRect innerRect(center - halfSize, center - halfSize, size, size);
    painter.drawRoundedRect(innerRect, 2, 2);

    // Draw light gray/white border for better visibility
    painter.setPen(QPen(QColor(220, 220, 220), 1.5, Qt::SolidLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(innerRect, 2, 2);

    painter.end();

    int hotspot = cursorSize / 2;
    return QCursor(pixmap, hotspot, hotspot);
}

ScreenCanvas::ScreenCanvas(QWidget* parent)
    : QWidget(parent)
    , m_currentScreen(nullptr)
    , m_devicePixelRatio(1.0)
    , m_annotationLayer(nullptr)
    , m_toolManager(nullptr)
    , m_currentToolId(ToolId::Pencil)
    , m_toolbar(nullptr)
    , m_isDraggingToolbar(false)
    , m_showSubToolbar(true)
    , m_colorPalette(nullptr)
    , m_colorPickerDialog(nullptr)
    , m_toolbarStyleConfig(ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle()))
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);

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
    m_stepBadgeSize = annotationSettings.loadStepBadgeSize();
    m_toolManager->setColor(savedColor);
    m_toolManager->setWidth(savedWidth);
    m_toolManager->setArrowStyle(savedArrowStyle);
    m_toolManager->setLineStyle(savedLineStyle);

    // Connect tool manager signals
    connect(m_toolManager, &ToolManager::needsRepaint, this, QOverload<>::of(&QWidget::update));

    // Initialize SVG icons
    initializeIcons();

    // Initialize toolbar
    setupToolbar();

    // Initialize color palette widget
    m_colorPalette = new ColorPaletteWidget(this);
    m_colorPalette->setCurrentColor(savedColor);
    connect(m_colorPalette, &ColorPaletteWidget::colorSelected,
        this, &ScreenCanvas::onColorSelected);
    connect(m_colorPalette, &ColorPaletteWidget::moreColorsRequested,
        this, &ScreenCanvas::onMoreColorsRequested);

    // Initialize unified color and width widget
    m_colorAndWidthWidget = new ColorAndWidthWidget(this);
    m_colorAndWidthWidget->setCurrentColor(savedColor);
    m_colorAndWidthWidget->setCurrentWidth(savedWidth);
    m_colorAndWidthWidget->setWidthRange(1, 20);
    m_colorAndWidthWidget->setArrowStyle(savedArrowStyle);
    m_colorAndWidthWidget->setLineStyle(savedLineStyle);
    m_colorAndWidthWidget->setStepBadgeSize(m_stepBadgeSize);
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
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::shapeTypeChanged,
        this, [this](ShapeType type) {
            m_shapeType = type;
            m_toolManager->setShapeType(static_cast<int>(type));
        });
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::shapeFillModeChanged,
        this, [this](ShapeFillMode mode) {
            m_shapeFillMode = mode;
            m_toolManager->setShapeFillMode(static_cast<int>(mode));
        });
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::mosaicBlurTypeChanged,
        this, [this](MosaicBlurTypeSection::BlurType type) {
            m_mosaicBlurType = type;
            m_toolManager->setMosaicBlurType(static_cast<MosaicStroke::BlurType>(type));
        });
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::stepBadgeSizeChanged,
        this, [this](StepBadgeSize size) {
            m_stepBadgeSize = size;
            int radius = StepBadgeAnnotation::radiusForSize(size);
            m_toolManager->setWidth(radius);
        });

    // Initialize emoji picker
    m_emojiPicker = new EmojiPicker(this);
    connect(m_emojiPicker, &EmojiPicker::emojiSelected,
        this, [this](const QString& emoji) {
            if (auto* handler = dynamic_cast<EmojiStickerToolHandler*>(
                    m_toolManager->handler(ToolId::EmojiSticker))) {
                handler->setCurrentEmoji(emoji);
            }
            update();
        });

    // Initialize inline text editor
    m_textEditor = new InlineTextEditor(this);
    connect(m_textEditor, &InlineTextEditor::editingFinished,
        this, &ScreenCanvas::onTextEditingFinished);
    connect(m_textEditor, &InlineTextEditor::editingCancelled,
        this, [this]() {
            m_textAnnotationEditor->cancelEditing();
        });

    // Initialize text annotation editor component
    m_textAnnotationEditor = new TextAnnotationEditor(this);
    m_textAnnotationEditor->setAnnotationLayer(m_annotationLayer);
    m_textAnnotationEditor->setTextEditor(m_textEditor);
    m_textAnnotationEditor->setColorAndWidthWidget(m_colorAndWidthWidget);
    m_textAnnotationEditor->setParentWidget(this);
    connect(m_textAnnotationEditor, &TextAnnotationEditor::updateRequested,
        this, QOverload<>::of(&QWidget::update));

    // Load text formatting settings from TextAnnotationEditor
    TextFormattingState textFormatting = m_textAnnotationEditor->formatting();
    m_colorAndWidthWidget->setBold(textFormatting.bold);
    m_colorAndWidthWidget->setItalic(textFormatting.italic);
    m_colorAndWidthWidget->setUnderline(textFormatting.underline);
    m_colorAndWidthWidget->setFontSize(textFormatting.fontSize);
    m_colorAndWidthWidget->setFontFamily(textFormatting.fontFamily);

    // Connect text formatting signals
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::boldToggled,
        m_textAnnotationEditor, &TextAnnotationEditor::setBold);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::italicToggled,
        m_textAnnotationEditor, &TextAnnotationEditor::setItalic);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::underlineToggled,
        m_textAnnotationEditor, &TextAnnotationEditor::setUnderline);

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

    // Initialize spotlight effect
    m_spotlightEffect = new SpotlightEffect(this);
    connect(m_spotlightEffect, &SpotlightEffect::needsRepaint,
        this, QOverload<>::of(&QWidget::update));

    // Set initial cursor based on default tool
    setToolCursor();

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
    iconRenderer.loadIcon("mosaic", ":/icons/icons/mosaic.svg");
    iconRenderer.loadIcon("eraser", ":/icons/icons/eraser.svg");
    iconRenderer.loadIcon("step-badge", ":/icons/icons/step-badge.svg");
    iconRenderer.loadIcon("text", ":/icons/icons/text.svg");
    iconRenderer.loadIcon("laser-pointer", ":/icons/icons/laser-pointer.svg");
    iconRenderer.loadIcon("cursor-highlight", ":/icons/icons/cursor-highlight.svg");
    iconRenderer.loadIcon("spotlight", ":/icons/icons/spotlight.svg");
    iconRenderer.loadIcon("undo", ":/icons/icons/undo.svg");
    iconRenderer.loadIcon("redo", ":/icons/icons/redo.svg");
    iconRenderer.loadIcon("cancel", ":/icons/icons/cancel.svg");
    // Shape and arrow style icons for ColorAndWidthWidget sections
    iconRenderer.loadIcon("shape-filled", ":/icons/icons/shape-filled.svg");
    iconRenderer.loadIcon("shape-outline", ":/icons/icons/shape-outline.svg");
    iconRenderer.loadIcon("arrow-none", ":/icons/icons/arrow-none.svg");
    iconRenderer.loadIcon("arrow-end", ":/icons/icons/arrow-end.svg");
    iconRenderer.loadIcon("arrow-end-outline", ":/icons/icons/arrow-end-outline.svg");
    iconRenderer.loadIcon("arrow-end-line", ":/icons/icons/arrow-end-line.svg");
    iconRenderer.loadIcon("arrow-both", ":/icons/icons/arrow-both.svg");
    iconRenderer.loadIcon("arrow-both-outline", ":/icons/icons/arrow-both-outline.svg");
}

void ScreenCanvas::setupToolbar()
{
    m_toolbar = new ToolbarWidget(this);

    // Configure buttons with separators at appropriate positions
    QVector<ToolbarWidget::ButtonConfig> buttons = {
        {static_cast<int>(CanvasButton::Pencil), "pencil", "Pencil"},
        {static_cast<int>(CanvasButton::Marker), "marker", "Marker"},
        {static_cast<int>(CanvasButton::Arrow), "arrow", "Arrow"},
        {static_cast<int>(CanvasButton::Shape), "shape", "Shape"},
        {static_cast<int>(CanvasButton::Mosaic), "mosaic", "Mosaic"},
        {static_cast<int>(CanvasButton::StepBadge), "step-badge", "Step Badge"},
        {static_cast<int>(CanvasButton::EmojiSticker), "emoji", "Emoji Sticker"},
        {static_cast<int>(CanvasButton::Text), "text", "Text"},
        {static_cast<int>(CanvasButton::LaserPointer), "laser-pointer", "Laser Pointer"},
        ToolbarWidget::ButtonConfig(static_cast<int>(CanvasButton::CursorHighlight), "cursor-highlight", "Cursor Highlight (Toggle)").separator(),
        {static_cast<int>(CanvasButton::Spotlight), "spotlight", "Spotlight (Toggle)"},
        ToolbarWidget::ButtonConfig(static_cast<int>(CanvasButton::Undo), "undo", "Undo (Ctrl+Z)").separator(),
        {static_cast<int>(CanvasButton::Redo), "redo", "Redo (Ctrl+Y)"},
        {static_cast<int>(CanvasButton::Clear), "cancel", "Clear All"},
        ToolbarWidget::ButtonConfig(static_cast<int>(CanvasButton::Exit), "cancel", "Exit (Esc)").separator().cancel()
    };
    m_toolbar->setButtons(buttons);

    // Set which buttons can be active (drawing tools and cursor highlight toggle)
    QVector<int> activeButtonIds = {
        static_cast<int>(CanvasButton::Pencil),
        static_cast<int>(CanvasButton::Marker),
        static_cast<int>(CanvasButton::Arrow),
        static_cast<int>(CanvasButton::Shape),
        static_cast<int>(CanvasButton::Mosaic),
        static_cast<int>(CanvasButton::StepBadge),
        static_cast<int>(CanvasButton::EmojiSticker),
        static_cast<int>(CanvasButton::Text),
        static_cast<int>(CanvasButton::LaserPointer),
        static_cast<int>(CanvasButton::CursorHighlight),
        static_cast<int>(CanvasButton::Spotlight)
    };
    m_toolbar->setActiveButtonIds(activeButtonIds);

    m_toolbar->setStyleConfig(m_toolbarStyleConfig);

    // Set custom icon color provider for complex color logic
    m_toolbar->setIconColorProvider([this](int buttonId, bool isActive, bool isHovered) -> QColor {
        Q_UNUSED(isHovered)
        return getButtonIconColor(buttonId);
    });
}

QColor ScreenCanvas::getButtonIconColor(int buttonId) const
{
    CanvasButton button = static_cast<CanvasButton>(buttonId);
    ToolId buttonToolId = canvasButtonToToolId(button);

    bool isButtonActive = (buttonToolId == m_currentToolId) && isDrawingTool(buttonToolId) && m_showSubToolbar;
    bool isToggleActive = ((button == CanvasButton::CursorHighlight) && m_rippleRenderer->isEnabled()) ||
                          ((button == CanvasButton::Spotlight) && m_spotlightEffect->isEnabled());

    if (button == CanvasButton::Exit) {
        return m_toolbarStyleConfig.iconCancelColor;
    }
    if (button == CanvasButton::Clear) {
        return QColor(255, 180, 100);  // Orange for clear
    }
    if (button == CanvasButton::Undo && !m_annotationLayer->canUndo()) {
        return QColor(128, 128, 128);  // Gray for disabled undo
    }
    if (button == CanvasButton::Redo && !m_annotationLayer->canRedo()) {
        return QColor(128, 128, 128);  // Gray for disabled redo
    }
    if (isButtonActive || isToggleActive) {
        return m_toolbarStyleConfig.iconActiveColor;
    }
    return m_toolbarStyleConfig.iconNormalColor;
}

QString ScreenCanvas::getIconKeyForButton(CanvasButton button) const
{
    switch (button) {
    case CanvasButton::Pencil:          return "pencil";
    case CanvasButton::Marker:          return "marker";
    case CanvasButton::Arrow:           return "arrow";
    case CanvasButton::Shape:           return "shape";
    case CanvasButton::Mosaic:          return "mosaic";
    case CanvasButton::StepBadge:       return "step-badge";
    case CanvasButton::EmojiSticker:    return "emoji";
    case CanvasButton::Text:            return "text";
    case CanvasButton::LaserPointer:    return "laser-pointer";
    case CanvasButton::CursorHighlight: return "cursor-highlight";
    case CanvasButton::Spotlight:       return "spotlight";
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
    if (!m_showSubToolbar) return false;
    auto it = kToolCapabilities.find(m_currentToolId);
    return it != kToolCapabilities.end() && it->second.showInPalette;
}

void ScreenCanvas::onColorSelected(const QColor& color)
{
    m_toolManager->setColor(color);
    m_laserRenderer->setColor(color);
    m_colorAndWidthWidget->setCurrentColor(color);
    AnnotationSettingsManager::instance().saveColor(color);
    update();
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
    if (!m_showSubToolbar) return false;
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

    // Update tool manager with background pixmap for mosaic tool
    m_toolManager->setSourcePixmap(&m_backgroundPixmap);
    m_toolManager->setDevicePixelRatio(m_devicePixelRatio);

    qDebug() << "ScreenCanvas: Initialized for screen" << m_currentScreen->name()
        << "logical size:" << m_currentScreen->geometry().size()
        << "pixmap size:" << m_backgroundPixmap.size()
        << "devicePixelRatio:" << m_devicePixelRatio;

    // Lock window size
    setFixedSize(m_currentScreen->geometry().size());

    // Update toolbar position
    updateToolbarPosition();

    // Set initial cursor based on current tool
    setToolCursor();
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

    // Draw spotlight effect
    if (m_spotlightEffect->isEnabled()) {
        m_spotlightEffect->render(painter, rect());
    }

    // Update toolbar position only when not dragging (dragging sets position directly)
    if (!m_isDraggingToolbar) {
        updateToolbarPosition();
    }
    // Update active button based on current tool state
    int activeButtonId = -1;
    if (m_showSubToolbar && isDrawingTool(m_currentToolId)) {
        activeButtonId = static_cast<int>(m_currentToolId);  // ToolId maps to CanvasButton
        // Map ToolId to CanvasButton
        switch (m_currentToolId) {
        case ToolId::Pencil: activeButtonId = static_cast<int>(CanvasButton::Pencil); break;
        case ToolId::Marker: activeButtonId = static_cast<int>(CanvasButton::Marker); break;
        case ToolId::Arrow: activeButtonId = static_cast<int>(CanvasButton::Arrow); break;
        case ToolId::Shape: activeButtonId = static_cast<int>(CanvasButton::Shape); break;
        case ToolId::Mosaic: activeButtonId = static_cast<int>(CanvasButton::Mosaic); break;
        case ToolId::StepBadge: activeButtonId = static_cast<int>(CanvasButton::StepBadge); break;
        case ToolId::Text: activeButtonId = static_cast<int>(CanvasButton::Text); break;
        case ToolId::LaserPointer: activeButtonId = static_cast<int>(CanvasButton::LaserPointer); break;
        default: activeButtonId = -1; break;
        }
    }
    // Handle CursorHighlight toggle separately
    if (m_rippleRenderer->isEnabled()) {
        activeButtonId = static_cast<int>(CanvasButton::CursorHighlight);
    }
    // Handle Spotlight toggle separately
    if (m_spotlightEffect->isEnabled()) {
        activeButtonId = static_cast<int>(CanvasButton::Spotlight);
    }
    m_toolbar->setActiveButton(activeButtonId);
    m_toolbar->draw(painter);

    // Use unified color and width widget
    QRect toolbarRect = m_toolbar->boundingRect();
    if (shouldShowColorAndWidthWidget()) {
        m_colorAndWidthWidget->setVisible(true);
        m_colorAndWidthWidget->setShowWidthSection(shouldShowWidthControl());
        // Show arrow style section only for Arrow tool
        m_colorAndWidthWidget->setShowArrowStyleSection(m_currentToolId == ToolId::Arrow);
        // Show line style section for Pencil and Arrow tools
        bool showLineStyle = (m_currentToolId == ToolId::Pencil ||
                              m_currentToolId == ToolId::Arrow);
        m_colorAndWidthWidget->setShowLineStyleSection(showLineStyle);
        // Show shape section for Shape tool
        m_colorAndWidthWidget->setShowShapeSection(m_currentToolId == ToolId::Shape);
        // Show mosaic blur type section for Mosaic tool (hide color for Mosaic)
        m_colorAndWidthWidget->setShowMosaicBlurTypeSection(m_currentToolId == ToolId::Mosaic);
        m_colorAndWidthWidget->setShowColorSection(m_currentToolId != ToolId::Mosaic);
        // Show size section for StepBadge (replaces width section)
        m_colorAndWidthWidget->setShowSizeSection(m_currentToolId == ToolId::StepBadge);
        // Show text section for Text tool
        m_colorAndWidthWidget->setShowTextSection(m_currentToolId == ToolId::Text);
        m_colorAndWidthWidget->updatePosition(toolbarRect, true, width());
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
            m_colorPalette->updatePosition(toolbarRect, true);
            m_colorPalette->draw(painter);
        }
        else {
            m_colorPalette->setVisible(false);
        }
    }

    // Draw emoji picker when EmojiSticker tool is selected
    if (m_currentToolId == ToolId::EmojiSticker) {
        m_emojiPicker->setVisible(true);
        m_emojiPicker->updatePosition(toolbarRect, true);
        m_emojiPicker->draw(painter);
    }
    else {
        m_emojiPicker->setVisible(false);
    }

    // Draw tooltip
    m_toolbar->drawTooltip(painter);

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

void ScreenCanvas::updateToolbarPosition()
{
    // Center horizontally, 30px from bottom
    int centerX = width() / 2;
    int bottomY = height() - 30;

    m_toolbar->setPosition(centerX, bottomY);
    m_toolbar->setViewportWidth(width());
}

void ScreenCanvas::handleToolbarClick(CanvasButton button)
{
    ToolId toolId = canvasButtonToToolId(button);

    switch (button) {
    case CanvasButton::Pencil:
    case CanvasButton::Marker:
    case CanvasButton::Arrow:
    case CanvasButton::Shape:
    case CanvasButton::Mosaic:
    case CanvasButton::Text:
        if (m_currentToolId == toolId) {
            // Same tool clicked - toggle sub-toolbar visibility
            m_showSubToolbar = !m_showSubToolbar;
        } else {
            // Different tool - select it and show sub-toolbar
            m_currentToolId = toolId;
            m_toolManager->setCurrentTool(toolId);
            m_showSubToolbar = true;
        }
        qDebug() << "ScreenCanvas: Tool selected:" << static_cast<int>(toolId) << "showSubToolbar:" << m_showSubToolbar;
        setToolCursor();
        update();
        break;

    case CanvasButton::StepBadge:
        if (m_currentToolId == toolId) {
            // Same tool clicked - toggle sub-toolbar visibility
            m_showSubToolbar = !m_showSubToolbar;
        } else {
            // Different tool - select it and show sub-toolbar
            m_currentToolId = toolId;
            m_toolManager->setCurrentTool(toolId);
            // Set width to badge radius (StepBadgeToolHandler uses ctx->width for radius)
            m_toolManager->setWidth(StepBadgeAnnotation::radiusForSize(m_stepBadgeSize));
            m_showSubToolbar = true;
        }
        qDebug() << "ScreenCanvas: StepBadge selected, showSubToolbar:" << m_showSubToolbar;
        setToolCursor();
        update();
        break;

    case CanvasButton::EmojiSticker:
        if (m_currentToolId == toolId) {
            m_showSubToolbar = !m_showSubToolbar;
        } else {
            m_currentToolId = toolId;
            m_toolManager->setCurrentTool(toolId);
            m_showSubToolbar = true;
        }
        qDebug() << "ScreenCanvas: EmojiSticker selected, showSubToolbar:" << m_showSubToolbar;
        setToolCursor();
        update();
        break;

    case CanvasButton::LaserPointer:
        if (m_currentToolId == toolId) {
            // Same tool clicked - toggle sub-toolbar visibility
            m_showSubToolbar = !m_showSubToolbar;
        } else {
            // Different tool - select it and show sub-toolbar
            m_currentToolId = toolId;
            m_toolManager->setCurrentTool(toolId);
            // Sync laser pointer with current color and width settings
            m_laserRenderer->setColor(m_toolManager->color());
            m_laserRenderer->setWidth(m_toolManager->width());
            m_showSubToolbar = true;
        }
        qDebug() << "ScreenCanvas: Laser Pointer selected, showSubToolbar:" << m_showSubToolbar;
        setToolCursor();
        update();
        break;

    case CanvasButton::CursorHighlight:
        // Toggle cursor highlight (not a drawing tool)
        m_rippleRenderer->setEnabled(!m_rippleRenderer->isEnabled());
        qDebug() << "ScreenCanvas: Cursor Highlight" << (m_rippleRenderer->isEnabled() ? "enabled" : "disabled");
        update();
        break;

    case CanvasButton::Spotlight:
        // Toggle spotlight effect (not a drawing tool)
        m_spotlightEffect->setEnabled(!m_spotlightEffect->isEnabled());
        qDebug() << "ScreenCanvas: Spotlight" << (m_spotlightEffect->isEnabled() ? "enabled" : "disabled");
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
    case ToolId::StepBadge:
    case ToolId::Text:
        return true;
    default:
        return false;
    }
}

void ScreenCanvas::setToolCursor()
{
    switch (m_currentToolId) {
    case ToolId::Text:
        setCursor(Qt::IBeamCursor);
        break;
    case ToolId::Mosaic: {
        int mosaicWidth = m_toolManager ? m_toolManager->width() : MosaicToolHandler::kDefaultBrushWidth;
        setCursor(createMosaicCursor(mosaicWidth));
        break;
    }
    case ToolId::LaserPointer:
    case ToolId::CursorHighlight:
    case ToolId::Spotlight:
        // Effect tools - use arrow cursor
        setCursor(Qt::ArrowCursor);
        break;
    default:
        if (isDrawingTool(m_currentToolId)) {
            setCursor(Qt::CrossCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }
        break;
    }
}

void ScreenCanvas::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        // Finalize polyline when clicking on UI elements (toolbar, widgets)
        // ArrowToolHandler's onDoubleClick returns early if not in polyline mode
        auto finalizePolylineForUiClick = [&](const QPoint& pos) {
            if (m_currentToolId == ToolId::Arrow && m_toolManager->isDrawing()) {
                m_toolManager->handleDoubleClick(pos);
            }
        };

        // Check if clicked on toolbar FIRST (before widgets that may overlap)
        if (m_toolbar->contains(event->pos())) {
            int buttonIdx = m_toolbar->buttonAtPosition(event->pos());
            if (buttonIdx >= 0) {
                finalizePolylineForUiClick(event->pos());
                int buttonId = m_toolbar->buttonIdAt(buttonIdx);
                handleToolbarClick(static_cast<CanvasButton>(buttonId));
            } else {
                // Start toolbar drag (clicked on toolbar but not on a button)
                m_isDraggingToolbar = true;
                m_toolbarDragOffset = event->pos() - m_toolbar->boundingRect().topLeft();
                setCursor(Qt::ClosedHandCursor);
            }
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

        // Check if clicked on emoji picker
        if (m_emojiPicker->isVisible()) {
            if (m_emojiPicker->handleClick(event->pos())) {
                update();
                return;
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

        // Handle text tool - start text editing
        if (m_currentToolId == ToolId::Text) {
            m_textAnnotationEditor->startEditing(event->pos(), rect(), m_toolManager->color());
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

    // Update spotlight cursor position
    if (m_spotlightEffect->isEnabled()) {
        m_spotlightEffect->updateCursorPosition(event->pos());
    }

    // Handle toolbar drag
    if (m_isDraggingToolbar) {
        QPoint newTopLeft = event->pos() - m_toolbarDragOffset;
        QRect toolbarRect = m_toolbar->boundingRect();
        // Clamp to screen bounds
        newTopLeft.setX(qBound(0, newTopLeft.x(), width() - toolbarRect.width()));
        newTopLeft.setY(qBound(0, newTopLeft.y(), height() - toolbarRect.height()));
        // Update toolbar position using setPosition (centered, so adjust)
        int centerX = newTopLeft.x() + toolbarRect.width() / 2;
        int bottomY = newTopLeft.y() + toolbarRect.height();
        m_toolbar->setPosition(centerX, bottomY);
        update();
        return;
    }

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

        // Update hovered emoji in emoji picker
        if (m_emojiPicker->isVisible()) {
            if (m_emojiPicker->updateHoveredEmoji(event->pos())) {
                needsUpdate = true;
            }
            if (m_emojiPicker->contains(event->pos())) {
                setCursor(Qt::PointingHandCursor);
                widgetHovered = true;
            }
        }

        // Update hovered button via ToolbarWidget
        if (m_toolbar->updateHoveredButton(event->pos())) {
            needsUpdate = true;
        }

        // Always update cursor based on current state
        if (m_toolbar->hoveredButton() >= 0) {
            setCursor(Qt::PointingHandCursor);
        }
        else if (m_toolbar->contains(event->pos())) {
            // Hovering over toolbar but not on a button - show drag cursor
            setCursor(Qt::OpenHandCursor);
        }
        else if (widgetHovered) {
            setCursor(Qt::PointingHandCursor);
        }
        else {
            setToolCursor();
        }

        if (needsUpdate) {
            update();
        }
    }

    // Only update cursor dot region if position changed (performance optimization)
    // Cursor dot is 6px diameter, use 10px margin for safety
    if (m_cursorPos != oldCursorPos) {
        int cursorMargin = 10;
        if (m_currentToolId == ToolId::Mosaic) {
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
        // Handle toolbar drag end
        if (m_isDraggingToolbar) {
            m_isDraggingToolbar = false;
            // Restore cursor based on position
            if (m_toolbar->contains(event->pos())) {
                setCursor(Qt::OpenHandCursor);
            } else {
                setToolCursor();
            }
            return;
        }

        // Handle unified widget release
        if (shouldShowColorAndWidthWidget() && m_colorAndWidthWidget->handleMouseRelease(event->pos())) {
            update();
            return;
        }

        // Handle laser pointer release
        if (m_currentToolId == ToolId::LaserPointer && m_laserRenderer->isDrawing()) {
            m_laserRenderer->stopDrawing();
            update();
            return;
        }

        // Skip annotation handling if releasing on toolbar or widgets
        // This prevents StepBadge from creating badges when clicking on UI elements
        if (m_toolbar->contains(event->pos())) {
            return;
        }
        if (shouldShowColorAndWidthWidget() && m_colorAndWidthWidget->contains(event->pos())) {
            return;
        }

        // Finish drawing annotation
        // StepBadge places on release but isDrawing() returns false, so handle it specially
        if (m_toolManager->isDrawing() || m_currentToolId == ToolId::StepBadge) {
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
    // Mosaic uses system cursor only (like RegionSelector)
    if (m_currentToolId == ToolId::Mosaic) return;

    // Don't show when drawing, on toolbar, or on widgets
    if (m_toolManager->isDrawing()) return;
    if (m_laserRenderer->isDrawing()) return;
    if (m_toolbar->contains(m_cursorPos)) return;
    if (m_toolbar->hoveredButton() >= 0) return;
    if (shouldShowColorAndWidthWidget() && m_colorAndWidthWidget->contains(m_cursorPos)) return;
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

void ScreenCanvas::onTextEditingFinished(const QString& text, const QPoint& position)
{
    m_textAnnotationEditor->finishEditing(text, position, m_toolManager->color());
}
