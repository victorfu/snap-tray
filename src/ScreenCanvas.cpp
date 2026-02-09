#include "ScreenCanvas.h"
#include "annotation/AnnotationContext.h"
#include "annotations/AnnotationLayer.h"
#include "tools/ToolManager.h"
#include "cursor/CursorManager.h"
#include "IconRenderer.h"
#include "GlassRenderer.h"
#include "colorwidgets/ColorPickerDialogCompat.h"

using snaptray::colorwidgets::ColorPickerDialogCompat;
#include "toolbar/ToolOptionsPanel.h"
#include "EmojiPicker.h"
#include "LaserPointerRenderer.h"
#include "toolbar/ToolbarCore.h"
#include "settings/AnnotationSettingsManager.h"
#include "settings/Settings.h"
#include "tools/handlers/EmojiStickerToolHandler.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/PolylineAnnotation.h"
#include "TransformationGizmo.h"

#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTextEdit>
#include <QWheelEvent>
#include <QCloseEvent>
#include <QShowEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QDebug>
#include <QSettings>
#include <QTimer>
#include "tools/ToolRegistry.h"
#include "tools/ToolSectionConfig.h"
#include "tools/ToolTraits.h"
#include "platform/WindowLevel.h"

ScreenCanvas::ScreenCanvas(QWidget* parent)
    : QWidget(parent)
    , m_currentScreen(nullptr)
    , m_devicePixelRatio(1.0)
    , m_annotationLayer(nullptr)
    , m_toolManager(nullptr)
    , m_currentToolId(ToolId::Pencil)
    , m_toolbar(nullptr)
    , m_isDraggingToolbar(false)
    , m_toolbarManuallyPositioned(false)
    , m_showSubToolbar(true)
    , m_colorPickerDialog(nullptr)
    , m_toolbarStyleConfig(ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle()))
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

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

    // Initialize cursor manager
    auto& cursorManager = CursorManager::instance();
    cursorManager.registerWidget(this, m_toolManager);

    // Initialize SVG icons
    initializeIcons();

    // Initialize toolbar
    setupToolbar();

    // Initialize unified color and width widget
    m_colorAndWidthWidget = new ToolOptionsPanel(this);
    m_colorAndWidthWidget->setCurrentColor(savedColor);
    m_colorAndWidthWidget->setCurrentWidth(savedWidth);
    m_colorAndWidthWidget->setWidthRange(1, 20);
    m_colorAndWidthWidget->setArrowStyle(savedArrowStyle);
    m_colorAndWidthWidget->setLineStyle(savedLineStyle);
    m_colorAndWidthWidget->setStepBadgeSize(m_stepBadgeSize);
    connect(m_colorAndWidthWidget, &ToolOptionsPanel::shapeTypeChanged,
        this, [this](ShapeType type) {
            m_shapeType = type;
            m_toolManager->setShapeType(static_cast<int>(type));
        });
    connect(m_colorAndWidthWidget, &ToolOptionsPanel::shapeFillModeChanged,
        this, [this](ShapeFillMode mode) {
            m_shapeFillMode = mode;
            m_toolManager->setShapeFillMode(static_cast<int>(mode));
        });
    connect(m_colorAndWidthWidget, &ToolOptionsPanel::stepBadgeSizeChanged,
        this, [this](StepBadgeSize size) {
            m_stepBadgeSize = size;
            AnnotationSettingsManager::instance().saveStepBadgeSize(size);
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

    // Initialize text annotation editor component
    m_textAnnotationEditor = new TextAnnotationEditor(this);

    // Initialize settings helper for font dropdowns
    m_settingsHelper = new RegionSettingsHelper(this);
    m_settingsHelper->setParentWidget(this);

    // Connect font selection signals
    connect(m_settingsHelper, &RegionSettingsHelper::fontSizeSelected,
        this, &ScreenCanvas::onFontSizeSelected);
    connect(m_settingsHelper, &RegionSettingsHelper::fontFamilySelected,
        this, &ScreenCanvas::onFontFamilySelected);

    // Centralized annotation/text setup and common signal wiring
    m_annotationContext = std::make_unique<AnnotationContext>(*this);
    m_annotationContext->setupTextAnnotationEditor(true, true);
    m_annotationContext->connectTextEditorSignals();
    m_annotationContext->connectToolOptionsSignals();
    m_annotationContext->connectTextFormattingSignals();
    m_annotationContext->syncTextFormattingControls();

    // Initialize laser pointer renderer
    m_laserRenderer = new LaserPointerRenderer(this);
    m_laserRenderer->setColor(savedColor);
    m_laserRenderer->setWidth(savedWidth);
    connect(m_laserRenderer, &LaserPointerRenderer::needsRepaint,
        this, QOverload<>::of(&QWidget::update));

    // Set initial cursor based on default tool
    setToolCursor();

    // Connect to screen removal signal for graceful handling
    connect(qApp, &QGuiApplication::screenRemoved,
            this, [this](QScreen *screen) {
                if (m_currentScreen == screen || m_currentScreen.isNull()) {
                    qWarning() << "ScreenCanvas: Screen disconnected, closing";
                    emit closed();
                    close();
                }
            });
}

ScreenCanvas::~ScreenCanvas()
{
    // Clean up cursor state before destruction
    CursorManager::instance().clearAllForWidget(this);

    delete m_colorPickerDialog;
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
    iconRenderer.loadIcon("emoji", ":/icons/icons/emoji.svg");
    iconRenderer.loadIcon("text", ":/icons/icons/text.svg");
    iconRenderer.loadIcon("laser-pointer", ":/icons/icons/laser-pointer.svg");
    iconRenderer.loadIcon("undo", ":/icons/icons/undo.svg");
    iconRenderer.loadIcon("redo", ":/icons/icons/redo.svg");
    iconRenderer.loadIcon("cancel", ":/icons/icons/cancel.svg");
    // Shape and arrow style icons for ToolOptionsPanel sections
    iconRenderer.loadIcon("shape-filled", ":/icons/icons/shape-filled.svg");
    iconRenderer.loadIcon("shape-outline", ":/icons/icons/shape-outline.svg");
    iconRenderer.loadIcon("arrow-none", ":/icons/icons/arrow-none.svg");
    iconRenderer.loadIcon("arrow-end", ":/icons/icons/arrow-end.svg");
    iconRenderer.loadIcon("arrow-end-outline", ":/icons/icons/arrow-end-outline.svg");
    iconRenderer.loadIcon("arrow-end-line", ":/icons/icons/arrow-end-line.svg");
    iconRenderer.loadIcon("arrow-both", ":/icons/icons/arrow-both.svg");
    iconRenderer.loadIcon("arrow-both-outline", ":/icons/icons/arrow-both-outline.svg");
    // Background mode icons
    iconRenderer.loadIcon("whiteboard", ":/icons/icons/whiteboard.svg");
    iconRenderer.loadIcon("blackboard", ":/icons/icons/blackboard.svg");
}

void ScreenCanvas::setupToolbar()
{
    m_toolbar = new ToolbarCore(this);

    // Configure buttons with separators at appropriate positions
    QVector<ToolbarCore::ButtonConfig> buttons = {
        {static_cast<int>(CanvasButton::Shape), "shape", "Shape"},
        {static_cast<int>(CanvasButton::Arrow), "arrow", "Arrow"},
        {static_cast<int>(CanvasButton::Pencil), "pencil", "Pencil"},
        {static_cast<int>(CanvasButton::Marker), "marker", "Marker"},
        {static_cast<int>(CanvasButton::Text), "text", "Text"},
        {static_cast<int>(CanvasButton::StepBadge), "step-badge", "Step Badge"},
        {static_cast<int>(CanvasButton::EmojiSticker), "emoji", "Emoji Sticker"},
        {static_cast<int>(CanvasButton::LaserPointer), "laser-pointer", "Laser Pointer"},
        ToolbarCore::ButtonConfig(static_cast<int>(CanvasButton::Whiteboard), "whiteboard", "Whiteboard (W)").separator(),
        {static_cast<int>(CanvasButton::Blackboard), "blackboard", "Blackboard (B)"},
        ToolbarCore::ButtonConfig(static_cast<int>(CanvasButton::Undo), "undo", "Undo (Ctrl+Z)").separator(),
        {static_cast<int>(CanvasButton::Redo), "redo", "Redo (Ctrl+Y)"},
        {static_cast<int>(CanvasButton::Clear), "cancel", "Clear All"},
        ToolbarCore::ButtonConfig(static_cast<int>(CanvasButton::Exit), "cancel", "Exit (Esc)").separator().cancel()
    };
    m_toolbar->setButtons(buttons);

    // Set which buttons can be active (drawing tools and cursor highlight toggle)
    QVector<int> activeButtonIds = {
        static_cast<int>(CanvasButton::Shape),
        static_cast<int>(CanvasButton::Arrow),
        static_cast<int>(CanvasButton::Pencil),
        static_cast<int>(CanvasButton::Marker),
        static_cast<int>(CanvasButton::Text),
        static_cast<int>(CanvasButton::StepBadge),
        static_cast<int>(CanvasButton::EmojiSticker),
        static_cast<int>(CanvasButton::LaserPointer),
        static_cast<int>(CanvasButton::Whiteboard),
        static_cast<int>(CanvasButton::Blackboard)
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
    bool isBgModeActive = ((button == CanvasButton::Whiteboard) && m_bgMode == CanvasBackgroundMode::Whiteboard) ||
                          ((button == CanvasButton::Blackboard) && m_bgMode == CanvasBackgroundMode::Blackboard);

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
    if (isButtonActive || isBgModeActive) {
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
    case CanvasButton::StepBadge:       return "step-badge";
    case CanvasButton::EmojiSticker:    return "emoji";
    case CanvasButton::Text:            return "text";
    case CanvasButton::LaserPointer:    return "laser-pointer";
    case CanvasButton::Whiteboard:      return "whiteboard";
    case CanvasButton::Blackboard:      return "blackboard";
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
    return ToolRegistry::instance().showColorPalette(m_currentToolId);
}

QWidget* ScreenCanvas::annotationHostWidget() const
{
    return const_cast<ScreenCanvas*>(this);
}

AnnotationLayer* ScreenCanvas::annotationLayerForContext() const
{
    return m_annotationLayer;
}

ToolOptionsPanel* ScreenCanvas::toolOptionsPanelForContext() const
{
    return m_colorAndWidthWidget;
}

InlineTextEditor* ScreenCanvas::inlineTextEditorForContext() const
{
    return m_textEditor;
}

TextAnnotationEditor* ScreenCanvas::textAnnotationEditorForContext() const
{
    return m_textAnnotationEditor;
}

void ScreenCanvas::onContextColorSelected(const QColor& color)
{
    onColorSelected(color);
}

void ScreenCanvas::onContextMoreColorsRequested()
{
    onMoreColorsRequested();
}

void ScreenCanvas::onContextLineWidthChanged(int width)
{
    onLineWidthChanged(width);
}

void ScreenCanvas::onContextArrowStyleChanged(LineEndStyle style)
{
    onArrowStyleChanged(style);
}

void ScreenCanvas::onContextLineStyleChanged(LineStyle style)
{
    onLineStyleChanged(style);
}

void ScreenCanvas::onContextFontSizeDropdownRequested(const QPoint& pos)
{
    onFontSizeDropdownRequested(pos);
}

void ScreenCanvas::onContextFontFamilyDropdownRequested(const QPoint& pos)
{
    onFontFamilyDropdownRequested(pos);
}

void ScreenCanvas::onContextTextEditingFinished(const QString& text, const QPoint& position)
{
    onTextEditingFinished(text, position);
}

void ScreenCanvas::onContextTextEditingCancelled()
{
    if (m_textAnnotationEditor) {
        m_textAnnotationEditor->cancelEditing();
    }
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
    return ToolRegistry::instance().showColorWidthWidget(m_currentToolId);
}

bool ScreenCanvas::shouldShowWidthControl() const
{
    return ToolRegistry::instance().showWidthControl(m_currentToolId);
}

void ScreenCanvas::onMoreColorsRequested()
{
    // Ensure unified color/width widget is in sync with the tool manager color before showing the dialog
    m_colorAndWidthWidget->setCurrentColor(m_toolManager->color());

    AnnotationContext::showColorPickerDialog(
        this,
        m_colorPickerDialog,
        m_toolManager->color(),
        geometry().center(),
        [this](const QColor& color) {
            // Preserve existing behavior: custom-color selection does not persist settings.
            m_toolManager->setColor(color);
            m_colorAndWidthWidget->setCurrentColor(color);
            update();
        });
}

void ScreenCanvas::initializeForScreen(QScreen* screen)
{
    m_currentScreen = screen;
    if (m_currentScreen.isNull()) {
        m_currentScreen = QGuiApplication::primaryScreen();
    }
    if (m_currentScreen.isNull()) {
        qWarning() << "ScreenCanvas: No valid screen available";
        emit closed();
        QTimer::singleShot(0, this, &QWidget::close);
        return;
    }

    m_devicePixelRatio = m_currentScreen->devicePixelRatio();
    m_toolManager->setDevicePixelRatio(m_devicePixelRatio);

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

    // Draw background only in non-transparent modes (Whiteboard/Blackboard)
    if (m_bgMode != CanvasBackgroundMode::Screen) {
        painter.drawPixmap(rect(), m_backgroundPixmap);
    }
#ifdef Q_OS_WIN
    else {
        // Windows workaround: fully transparent windows are click-through.
        // Draw a nearly transparent background to capture mouse events.
        painter.fillRect(rect(), QColor(255, 255, 255, 1));
    }
#endif

    // Draw completed annotations
    drawAnnotations(painter);

    // Draw annotation in progress (preview)
    drawCurrentAnnotation(painter);

    // Draw laser pointer trail
    m_laserRenderer->draw(painter);

    // Update toolbar position only when not dragging and not manually positioned
    if (!m_isDraggingToolbar && !m_toolbarManuallyPositioned) {
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
        case ToolId::StepBadge: activeButtonId = static_cast<int>(CanvasButton::StepBadge); break;
        case ToolId::Text: activeButtonId = static_cast<int>(CanvasButton::Text); break;
        case ToolId::EmojiSticker: activeButtonId = static_cast<int>(CanvasButton::EmojiSticker); break;
        case ToolId::LaserPointer: activeButtonId = static_cast<int>(CanvasButton::LaserPointer); break;
        default: activeButtonId = -1; break;
        }
    }
    // Handle Whiteboard/Blackboard background mode
    if (m_bgMode == CanvasBackgroundMode::Whiteboard) {
        activeButtonId = static_cast<int>(CanvasButton::Whiteboard);
    }
    if (m_bgMode == CanvasBackgroundMode::Blackboard) {
        activeButtonId = static_cast<int>(CanvasButton::Blackboard);
    }
    m_toolbar->setActiveButton(activeButtonId);
    m_toolbar->draw(painter);

    // Use unified color and width widget with data-driven configuration
    QRect toolbarRect = m_toolbar->boundingRect();
    if (shouldShowColorAndWidthWidget()) {
        m_colorAndWidthWidget->setVisible(true);
        // Apply tool-specific section configuration
        ToolSectionConfig::forTool(m_currentToolId).applyTo(m_colorAndWidthWidget);
        m_colorAndWidthWidget->updatePosition(toolbarRect, true, width());
        m_colorAndWidthWidget->draw(painter);
    }
    else {
        m_colorAndWidthWidget->setVisible(false);
    }

    // Draw emoji picker when EmojiSticker tool is selected
    if (m_currentToolId == ToolId::EmojiSticker && m_showSubToolbar) {
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
    // Use dirty region optimization during drag operations
    if (m_isArrowDragging || m_isPolylineDragging) {
        // Draw cached content for non-dragged items, then draw dragged item on top
        m_annotationLayer->drawWithDirtyRegion(painter, size(), devicePixelRatioF(),
                                                m_annotationLayer->selectedIndex());
    } else {
        m_annotationLayer->drawCached(painter, size(), devicePixelRatioF());
    }

    // Draw transformation gizmo for selected Arrow
    if (auto* arrowItem = getSelectedArrowAnnotation()) {
        TransformationGizmo::draw(painter, arrowItem);
    }

    // Draw transformation gizmo for selected Polyline
    if (auto* polylineItem = getSelectedPolylineAnnotation()) {
        TransformationGizmo::draw(painter, polylineItem);
    }
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
            // StepBadgeToolHandler reads size from AnnotationSettingsManager, no setWidth needed
            m_showSubToolbar = true;
        }
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
        setToolCursor();
        update();
        break;

    case CanvasButton::Whiteboard:
        // Toggle whiteboard mode
        if (m_bgMode == CanvasBackgroundMode::Whiteboard) {
            setBackgroundMode(CanvasBackgroundMode::Screen);
        } else {
            setBackgroundMode(CanvasBackgroundMode::Whiteboard);
        }
        break;

    case CanvasButton::Blackboard:
        // Toggle blackboard mode
        if (m_bgMode == CanvasBackgroundMode::Blackboard) {
            setBackgroundMode(CanvasBackgroundMode::Screen);
        } else {
            setBackgroundMode(CanvasBackgroundMode::Blackboard);
        }
        break;

    case CanvasButton::Undo:
        if (m_annotationLayer->canUndo()) {
            m_annotationLayer->undo();
            update();
        }
        break;

    case CanvasButton::Redo:
        if (m_annotationLayer->canRedo()) {
            m_annotationLayer->redo();
            update();
        }
        break;

    case CanvasButton::Clear:
        m_annotationLayer->clear();
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
    // Canvas treats laser pointer as a drawing-mode tool for toolbar/sub-toolbar UX.
    return ToolTraits::isDrawingTool(toolId) || toolId == ToolId::LaserPointer;
}

void ScreenCanvas::setToolCursor()
{
    CursorManager::instance().updateToolCursorForWidget(this);
}

void ScreenCanvas::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        // Handle active text editor first - click outside to finish
        if (m_textEditor && m_textEditor->isEditing()) {
            if (!m_textEditor->contains(event->pos())) {
                // Clicking outside the text editor
                if (!m_textEditor->textEdit()->toPlainText().trimmed().isEmpty()) {
                    m_textEditor->finishEditing();  // Save non-empty text
                } else {
                    m_textEditor->cancelEditing();  // Discard empty text
                }
                // Don't return - allow the click to be processed (e.g., start new text)
            } else {
                // Clicking inside - let the editor handle it
                return;
            }
        }

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
                CursorManager::instance().setDragStateForWidget(this, DragState::ToolbarDrag);
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

        // Check if clicked on emoji picker
        if (m_emojiPicker->isVisible()) {
            if (m_emojiPicker->handleClick(event->pos())) {
                update();
                return;
            }
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

        // Check for Arrow annotation interaction
        if (handleArrowAnnotationPress(event->pos())) {
            return;
        }

        // Check for Polyline annotation interaction
        if (handlePolylineAnnotationPress(event->pos())) {
            return;
        }

        // Start annotation drawing
        if (isDrawingTool(m_currentToolId)) {
            m_toolManager->handleMousePress(event->pos(), event->modifiers());
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
    
    // Handle Arrow dragging
    if (m_isArrowDragging) {
        handleArrowAnnotationMove(event->pos());
        return;
    }

    // Handle Polyline dragging
    if (m_isPolylineDragging) {
        handlePolylineAnnotationMove(event->pos());
        return;
    }

    if (m_toolManager->isDrawing()) {
        m_toolManager->handleMouseMove(event->pos(), event->modifiers());
        update();
    }
    else {
        bool needsUpdate = false;
        bool widgetHovered = false;
        auto& cursorManager = CursorManager::instance();

        // Handle unified color and width widget
        if (shouldShowColorAndWidthWidget()) {
            if (m_colorAndWidthWidget->handleMouseMove(event->pos(), event->buttons() & Qt::LeftButton)) {
                if (m_colorAndWidthWidget->contains(event->pos())) {
                    cursorManager.setHoverTargetForWidget(this, HoverTarget::Widget);
                }
                update();
                return;
            }
            if (m_colorAndWidthWidget->updateHovered(event->pos())) {
                needsUpdate = true;
            }
            if (m_colorAndWidthWidget->contains(event->pos())) {
                widgetHovered = true;
            }
        }

        // Update hovered emoji in emoji picker
        if (m_emojiPicker->isVisible()) {
            if (m_emojiPicker->updateHoveredEmoji(event->pos())) {
                needsUpdate = true;
            }
            if (m_emojiPicker->contains(event->pos())) {
                widgetHovered = true;
            }
        }

        // Update hovered button via ToolbarCore
        if (m_toolbar->updateHoveredButton(event->pos())) {
            needsUpdate = true;
        }

        // Update cursor based on current hover state using state-driven API
        if (m_toolbar->hoveredButton() >= 0 || widgetHovered) {
            cursorManager.setHoverTargetForWidget(this, HoverTarget::ToolbarButton);
        }
        else if (m_toolbar->contains(event->pos())) {
            // Hovering over toolbar but not on a button - show drag cursor
            cursorManager.setHoverTargetForWidget(this, HoverTarget::Toolbar);
        }
        else {
            // Check annotation cursors - updateAnnotationCursor uses state-driven API
            updateAnnotationCursor(event->pos());
        }

        if (needsUpdate) {
            update();
        }
    }

    // Only update cursor dot region if position changed (performance optimization)
    // Cursor dot is 6px diameter, use 10px margin for safety
    if (m_cursorPos != oldCursorPos) {
        int cursorMargin = 10;
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
            m_toolbarManuallyPositioned = true;
            // Clear drag state - cursor will revert based on hover target
            auto& cursorManager = CursorManager::instance();
            cursorManager.setDragStateForWidget(this, DragState::None);
            if (m_toolbar->contains(event->pos())) {
                cursorManager.setHoverTargetForWidget(this, HoverTarget::Toolbar);
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
        
        // Handle Arrow release
        if (handleArrowAnnotationRelease(event->pos())) {
            return;
        }

        // Handle Polyline release
        if (handlePolylineAnnotationRelease(event->pos())) {
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
        // StepBadge/EmojiSticker place on release but isDrawing() returns false, so handle them specially
        if (m_toolManager->isDrawing() || m_currentToolId == ToolId::StepBadge ||
            m_currentToolId == ToolId::EmojiSticker) {
            m_toolManager->handleMouseRelease(event->pos(), event->modifiers());
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
    // Handle inline text editing keys first
    if (m_textEditor && m_textEditor->isEditing()) {
        if (event->key() == Qt::Key_Escape) {
            m_textEditor->cancelEditing();
            return;
        }
        // Let QTextEdit handle other keys (Enter for newlines, etc.)
        return;
    }

    if (event->key() == Qt::Key_Escape) {
        if (m_toolManager->handleEscape()) {
            return;
        }
        close();
    }
    else if (event->key() == Qt::Key_W) {
        // Toggle Whiteboard <-> Screen
        if (m_bgMode == CanvasBackgroundMode::Whiteboard) {
            setBackgroundMode(CanvasBackgroundMode::Screen);
        } else {
            setBackgroundMode(CanvasBackgroundMode::Whiteboard);
        }
    }
    else if (event->key() == Qt::Key_B) {
        // Toggle Blackboard <-> Screen
        if (m_bgMode == CanvasBackgroundMode::Blackboard) {
            setBackgroundMode(CanvasBackgroundMode::Screen);
        } else {
            setBackgroundMode(CanvasBackgroundMode::Blackboard);
        }
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

void ScreenCanvas::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    // Delay cursor setting to ensure macOS has finished window activation.
    QTimer::singleShot(100, this, [this]() {
        forceNativeCrosshairCursor(this);
    });
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

// Font dropdown handlers

void ScreenCanvas::onFontSizeDropdownRequested(const QPoint& pos)
{
    TextFormattingState formatting = m_textAnnotationEditor->formatting();
    m_settingsHelper->showFontSizeDropdown(pos, formatting.fontSize);
}

void ScreenCanvas::onFontFamilyDropdownRequested(const QPoint& pos)
{
    TextFormattingState formatting = m_textAnnotationEditor->formatting();
    m_settingsHelper->showFontFamilyDropdown(pos, formatting.fontFamily);
}

void ScreenCanvas::onFontSizeSelected(int size)
{
    m_textAnnotationEditor->setFontSize(size);
    m_colorAndWidthWidget->setFontSize(size);
}

void ScreenCanvas::onFontFamilySelected(const QString& family)
{
    m_textAnnotationEditor->setFontFamily(family);
    m_colorAndWidthWidget->setFontFamily(family);
}

// Background mode helper methods

void ScreenCanvas::setBackgroundMode(CanvasBackgroundMode mode)
{
    // Cancel any in-progress drawing to avoid stuck preview state
    if (m_toolManager->isDrawing()) {
        m_toolManager->cancelDrawing();
    }
    if (m_laserRenderer->isDrawing()) {
        m_laserRenderer->stopDrawing();
    }

    m_bgMode = mode;

    switch (mode) {
    case CanvasBackgroundMode::Screen:
        // Transparent background - clear pixmap
        m_backgroundPixmap = QPixmap();
        break;
    case CanvasBackgroundMode::Whiteboard:
        m_backgroundPixmap = createSolidBackgroundPixmap(Qt::white);
        break;
    case CanvasBackgroundMode::Blackboard:
        m_backgroundPixmap = createSolidBackgroundPixmap(Qt::black);
        break;
    }

    update();
}


QPixmap ScreenCanvas::createSolidBackgroundPixmap(const QColor& color) const
{
    // Create pixmap at physical pixel size for HiDPI support
    QSize physicalSize = size() * m_devicePixelRatio;
    QPixmap pixmap(physicalSize);
    pixmap.setDevicePixelRatio(m_devicePixelRatio);
    pixmap.fill(color);
    return pixmap;
}

// ============================================================================
// Arrow and Polyline Handling
// ============================================================================

ArrowAnnotation* ScreenCanvas::getSelectedArrowAnnotation()
{
    if (!m_annotationLayer) return nullptr;
    int index = m_annotationLayer->selectedIndex();
    if (index >= 0) {
        return dynamic_cast<ArrowAnnotation*>(m_annotationLayer->itemAt(index));
    }
    return nullptr;
}

bool ScreenCanvas::handleArrowAnnotationPress(const QPoint& pos)
{
    if (auto* arrowItem = getSelectedArrowAnnotation()) {
        GizmoHandle handle = TransformationGizmo::hitTest(arrowItem, pos);
        if (handle != GizmoHandle::None) {
             m_isArrowDragging = true;
             m_arrowDragHandle = handle;
             m_dragStartPos = pos;
             // Set appropriate cursor based on handle
             update(); 
             return true;
        }
    }

    // Check hit test for unselected items
    int hitIndex = m_annotationLayer->hitTestArrow(pos);
    if (hitIndex >= 0) {
        m_annotationLayer->setSelectedIndex(hitIndex);
        m_isArrowDragging = true;
        m_arrowDragHandle = GizmoHandle::Body; // Default to body drag on selection
        m_dragStartPos = pos;
        
        // Refine potential handle hit
        if (auto* arrowItem = getSelectedArrowAnnotation()) {
             GizmoHandle handle = TransformationGizmo::hitTest(arrowItem, pos);
             if (handle != GizmoHandle::None) {
                 m_arrowDragHandle = handle;
             }
        }
        
        update();
        return true;
    }
    return false;
}

bool ScreenCanvas::handleArrowAnnotationMove(const QPoint& pos)
{
    if (m_isArrowDragging && m_annotationLayer->selectedIndex() >= 0) {
        auto* arrowItem = getSelectedArrowAnnotation();
        if (arrowItem) {
            // Get old bounding rect before modification (with margin for gizmo handles)
            QRect oldRect = arrowItem->boundingRect().adjusted(-20, -20, 20, 20);

            if (m_arrowDragHandle == GizmoHandle::Body) {
                QPoint delta = pos - m_dragStartPos;
                arrowItem->moveBy(delta);
                m_dragStartPos = pos;
            } else if (m_arrowDragHandle == GizmoHandle::ArrowStart) {
                arrowItem->setStart(pos);
            } else if (m_arrowDragHandle == GizmoHandle::ArrowEnd) {
                arrowItem->setEnd(pos);
            } else if (m_arrowDragHandle == GizmoHandle::ArrowControl) {
                // User drags the curve midpoint (t=0.5), calculate actual Bézier control point
                // P1 = 2*Mid - 0.5*(Start + End)
                QPointF start = arrowItem->start();
                QPointF end = arrowItem->end();
                QPointF newControl = 2.0 * QPointF(pos) - 0.5 * (start + end);
                arrowItem->setControlPoint(newControl.toPoint());
            }

            // Get new bounding rect after modification
            QRect newRect = arrowItem->boundingRect().adjusted(-20, -20, 20, 20);

            // Mark dirty region instead of invalidating entire cache
            m_annotationLayer->markDirtyRect(oldRect.united(newRect));
            update();
            return true;
        }
    }
    return false;
}

bool ScreenCanvas::handleArrowAnnotationRelease(const QPoint& pos)
{
    Q_UNUSED(pos);
    if (m_isArrowDragging) {
        // Commit dirty region changes to cache
        m_annotationLayer->commitDirtyRegion(size(), devicePixelRatioF());

        m_isArrowDragging = false;
        m_arrowDragHandle = GizmoHandle::None;
        setToolCursor(); // Restore tool cursor
        update();
        return true;
    }
    return false;
}

PolylineAnnotation* ScreenCanvas::getSelectedPolylineAnnotation()
{
    if (!m_annotationLayer) return nullptr;
    int index = m_annotationLayer->selectedIndex();
    if (index >= 0) {
        return dynamic_cast<PolylineAnnotation*>(m_annotationLayer->itemAt(index));
    }
    return nullptr;
}

bool ScreenCanvas::handlePolylineAnnotationPress(const QPoint& pos)
{
    if (auto* polylineItem = getSelectedPolylineAnnotation()) {
        int vertexIndex = TransformationGizmo::hitTestVertex(polylineItem, pos);
        if (vertexIndex >= 0) {
            // Vertex hit
            m_isPolylineDragging = true;
            m_activePolylineVertexIndex = vertexIndex;
            m_dragStartPos = pos;
            update();
            return true;
        } else if (vertexIndex == -1) {
             // Body hit
             m_isPolylineDragging = true;
             m_activePolylineVertexIndex = -1;
             m_dragStartPos = pos;
             update();
             return true;
        }
    }

    // Check hit test for unselected items
    int hitIndex = m_annotationLayer->hitTestPolyline(pos);
    if (hitIndex >= 0) {
        m_annotationLayer->setSelectedIndex(hitIndex);
        m_isPolylineDragging = true;
        m_activePolylineVertexIndex = -1; // Default to body drag
        m_dragStartPos = pos;

        // Check if we actually hit a vertex though
        if (auto* polylineItem = getSelectedPolylineAnnotation()) {
             int vertexIndex = TransformationGizmo::hitTestVertex(polylineItem, pos);
             if (vertexIndex >= 0) {
                 m_activePolylineVertexIndex = vertexIndex;
             }
        }
        
        update();
        return true;
    }
    return false;
}

bool ScreenCanvas::handlePolylineAnnotationMove(const QPoint& pos)
{
    if (m_isPolylineDragging && m_annotationLayer->selectedIndex() >= 0) {
        auto* polylineItem = getSelectedPolylineAnnotation();
        if (polylineItem) {
            // Get old bounding rect before modification (with margin for gizmo handles)
            QRect oldRect = polylineItem->boundingRect().adjusted(-20, -20, 20, 20);

            if (m_activePolylineVertexIndex >= 0) {
                // Move specific vertex
                polylineItem->setPoint(m_activePolylineVertexIndex, pos);
            } else {
                // Move entire polyline
                QPoint delta = pos - m_dragStartPos;
                polylineItem->moveBy(delta);
                m_dragStartPos = pos;
            }

            // Get new bounding rect after modification
            QRect newRect = polylineItem->boundingRect().adjusted(-20, -20, 20, 20);

            // Mark dirty region instead of invalidating entire cache
            m_annotationLayer->markDirtyRect(oldRect.united(newRect));
            update();
            return true;
        }
    }
    return false;
}

bool ScreenCanvas::handlePolylineAnnotationRelease(const QPoint& pos)
{
    Q_UNUSED(pos);
    if (m_isPolylineDragging) {
        // Commit dirty region changes to cache
        m_annotationLayer->commitDirtyRegion(size(), devicePixelRatioF());

        m_isPolylineDragging = false;
        m_activePolylineVertexIndex = -1;
        setToolCursor();
        update();
        return true;
    }
    return false;
}

void ScreenCanvas::updateAnnotationCursor(const QPoint& pos)
{
    auto& cursorManager = CursorManager::instance();

    // Use unified hit testing from CursorManager
    auto result = CursorManager::hitTestAnnotations(
        pos,
        m_annotationLayer,
        getSelectedArrowAnnotation(),
        getSelectedPolylineAnnotation()
    );

    if (result.hit) {
        cursorManager.setHoverTargetForWidget(this, result.target, result.handleIndex);
    } else {
        // No annotation hit - clear hover target to let tool cursor show
        cursorManager.setHoverTargetForWidget(this, HoverTarget::None);
    }
}
