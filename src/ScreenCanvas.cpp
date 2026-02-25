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
#include "tools/handlers/EmojiStickerToolHandler.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/TextBoxAnnotation.h"
#include "annotations/EmojiStickerAnnotation.h"
#include "annotations/PolylineAnnotation.h"
#include "TransformationGizmo.h"
#include "utils/CoordinateHelper.h"

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
#include <QTimer>
#include <QtMath>
#include "tools/ToolRegistry.h"
#include "tools/ToolSectionConfig.h"
#include "tools/ToolTraits.h"
#include "platform/WindowLevel.h"

namespace {
constexpr int kLaserPointerButtonId = 10001;

bool isManagedToolButtonId(int buttonId)
{
    return buttonId >= 0 && buttonId < static_cast<int>(ToolId::Count);
}

bool isLaserPointerButtonId(int buttonId)
{
    return buttonId == kLaserPointerButtonId;
}

qreal normalizeAngleDelta(qreal deltaDegrees)
{
    while (deltaDegrees > 180.0) {
        deltaDegrees -= 360.0;
    }
    while (deltaDegrees < -180.0) {
        deltaDegrees += 360.0;
    }
    return deltaDegrees;
}
}

const std::map<ToolId, ScreenCanvas::ToolbarClickHandler>& ScreenCanvas::toolbarDispatchTable()
{
    static const std::map<ToolId, ToolbarClickHandler> kDispatch = {
        {ToolId::Pencil, &ScreenCanvas::handlePersistentToolClick},
        {ToolId::Marker, &ScreenCanvas::handlePersistentToolClick},
        {ToolId::Arrow, &ScreenCanvas::handlePersistentToolClick},
        {ToolId::Shape, &ScreenCanvas::handlePersistentToolClick},
        {ToolId::Text, &ScreenCanvas::handlePersistentToolClick},
        {ToolId::Eraser, &ScreenCanvas::handlePersistentToolClick},
        {ToolId::StepBadge, &ScreenCanvas::handlePersistentToolClick},
        {ToolId::EmojiSticker, &ScreenCanvas::handlePersistentToolClick},
        {ToolId::CanvasWhiteboard, &ScreenCanvas::handleCanvasModeToggle},
        {ToolId::CanvasBlackboard, &ScreenCanvas::handleCanvasModeToggle},
        {ToolId::Undo, &ScreenCanvas::handleActionToolClick},
        {ToolId::Redo, &ScreenCanvas::handleActionToolClick},
        {ToolId::Clear, &ScreenCanvas::handleActionToolClick},
        {ToolId::Exit, &ScreenCanvas::handleActionToolClick},
    };
    return kDispatch;
}

const std::map<ToolId, ScreenCanvas::ToolbarClickHandler>& ScreenCanvas::actionDispatchTable()
{
    static const std::map<ToolId, ToolbarClickHandler> kActionDispatch = {
        {ToolId::Undo, &ScreenCanvas::handleUndoAction},
        {ToolId::Redo, &ScreenCanvas::handleRedoAction},
        {ToolId::Clear, &ScreenCanvas::handleClearAction},
        {ToolId::Exit, &ScreenCanvas::handleExitAction},
    };
    return kActionDispatch;
}

ScreenCanvas::ScreenCanvas(QWidget* parent)
    : QWidget(parent)
    , m_currentScreen(nullptr)
    , m_devicePixelRatio(1.0)
    , m_annotationLayer(nullptr)
    , m_toolManager(nullptr)
    , m_currentToolId(ToolId::Pencil)
    , m_laserPointerActive(false)
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
    LineEndStyle savedArrowStyle = annotationSettings.loadArrowStyle();
    LineStyle savedLineStyle = annotationSettings.loadLineStyle();
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
    m_shapeAnnotationEditor = new ShapeAnnotationEditor();
    m_shapeAnnotationEditor->setAnnotationLayer(m_annotationLayer);

    // Provide text dependencies to ToolManager (TextToolHandler).
    m_toolManager->setInlineTextEditor(m_textEditor);
    m_toolManager->setTextAnnotationEditor(m_textAnnotationEditor);
    m_toolManager->setShapeAnnotationEditor(m_shapeAnnotationEditor);
    m_toolManager->setTextEditingBounds(rect());
    m_toolManager->setTextColorSyncCallback([this](const QColor& color) {
        syncColorToAllWidgets(color);
    });
    m_toolManager->setHostFocusCallback([this]() {
        setFocus(Qt::OtherFocusReason);
    });
    m_toolManager->setTextReEditStartedCallback([this]() {
        m_consumeNextToolRelease = true;
    });

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
    if (m_colorPickerDialog) {
        m_colorPickerDialog->close();
    }
    delete m_shapeAnnotationEditor;
    m_shapeAnnotationEditor = nullptr;
}

void ScreenCanvas::initializeIcons()
{
    // Use shared IconRenderer for all icons
    IconRenderer& iconRenderer = IconRenderer::instance();
    auto& registry = ToolRegistry::instance();
    for (ToolId toolId : registry.getToolsForToolbar(ToolbarType::ScreenCanvas)) {
        const QString iconKey = registry.getIconKey(toolId);
        if (!iconKey.isEmpty()) {
            iconRenderer.loadIconByKey(iconKey);
        }
    }
    iconRenderer.loadIconByKey("laser-pointer");

    // Shape and arrow style icons for ToolOptionsPanel sections
    iconRenderer.loadIconsByKey({
        "rectangle",
        "ellipse",
        "shape-filled",
        "shape-outline",
        "arrow-none",
        "arrow-end",
        "arrow-end-outline",
        "arrow-end-line",
        "arrow-both",
        "arrow-both-outline",
        "auto-blur"
    });
}

void ScreenCanvas::setupToolbar()
{
    m_toolbar = new ToolbarCore(this);
    auto& registry = ToolRegistry::instance();
    const auto& dispatch = toolbarDispatchTable();
    const QVector<ToolId> toolbarTools = registry.getToolsForToolbar(ToolbarType::ScreenCanvas);

    QVector<ToolbarCore::ButtonConfig> buttons;
    QVector<int> activeButtonIds;
    bool laserInserted = false;
    for (ToolId toolId : toolbarTools) {
        if (!laserInserted && toolId == ToolId::CanvasWhiteboard) {
            buttons.append(ToolbarCore::ButtonConfig(
                kLaserPointerButtonId,
                "laser-pointer",
                tr("Laser Pointer")));
            activeButtonIds.append(kLaserPointerButtonId);
            laserInserted = true;
        }

        const auto& def = registry.get(toolId);
        ToolbarCore::ButtonConfig config(
            static_cast<int>(toolId),
            def.iconKey,
            registry.getTooltipWithShortcut(toolId),
            def.showSeparatorBefore);
        if (toolId == ToolId::Exit) {
            config.cancel();
        }
        buttons.append(config);

        const auto dispatchIt = dispatch.find(toolId);
        const bool supportsActiveState = dispatchIt != dispatch.end() &&
            (dispatchIt->second == &ScreenCanvas::handlePersistentToolClick ||
             dispatchIt->second == &ScreenCanvas::handleCanvasModeToggle);
        if (supportsActiveState) {
            activeButtonIds.append(static_cast<int>(toolId));
        }
    }

    if (!laserInserted) {
        buttons.append(ToolbarCore::ButtonConfig(
            kLaserPointerButtonId,
            "laser-pointer",
            tr("Laser Pointer")));
        activeButtonIds.append(kLaserPointerButtonId);
    }

    m_toolbar->setButtons(buttons);
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
    const bool isManagedToolButton = isManagedToolButtonId(buttonId);
    const ToolId buttonToolId = isManagedToolButton
        ? static_cast<ToolId>(buttonId)
        : ToolId::Selection;

    const bool isToolButtonActive = isManagedToolButton &&
        !m_laserPointerActive &&
        (buttonToolId == m_currentToolId) &&
        isDrawingTool(buttonToolId) &&
        m_showSubToolbar;
    const bool isLaserButtonActive =
        isLaserPointerButtonId(buttonId) && m_laserPointerActive && m_showSubToolbar;
    const bool isBgModeActive =
        (isManagedToolButton && buttonToolId == ToolId::CanvasWhiteboard && m_bgMode == CanvasBackgroundMode::Whiteboard) ||
        (isManagedToolButton && buttonToolId == ToolId::CanvasBlackboard && m_bgMode == CanvasBackgroundMode::Blackboard);

    if (buttonId == static_cast<int>(ToolId::Exit)) {
        return m_toolbarStyleConfig.iconCancelColor;
    }
    if (buttonId == static_cast<int>(ToolId::Clear)) {
        return QColor(255, 180, 100);  // Orange for clear
    }
    if (buttonId == static_cast<int>(ToolId::Undo) && !m_annotationLayer->canUndo()) {
        return QColor(128, 128, 128);  // Gray for disabled undo
    }
    if (buttonId == static_cast<int>(ToolId::Redo) && !m_annotationLayer->canRedo()) {
        return QColor(128, 128, 128);  // Gray for disabled redo
    }
    if (isToolButtonActive || isLaserButtonActive || isBgModeActive) {
        return m_toolbarStyleConfig.iconActiveColor;
    }
    return m_toolbarStyleConfig.iconNormalColor;
}

bool ScreenCanvas::shouldShowColorPalette() const
{
    if (!m_showSubToolbar) return false;
    if (m_laserPointerActive) return true;
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

void ScreenCanvas::syncColorToAllWidgets(const QColor& color)
{
    m_toolManager->setColor(color);
    m_laserRenderer->setColor(color);
    m_colorAndWidthWidget->setCurrentColor(color);
    if (m_textEditor) {
        m_textEditor->setColor(color);
    }
}

void ScreenCanvas::onColorSelected(const QColor& color)
{
    syncColorToAllWidgets(color);
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
    if (m_laserPointerActive) return true;
    return ToolRegistry::instance().showColorWidthWidget(m_currentToolId);
}

bool ScreenCanvas::shouldShowWidthControl() const
{
    if (m_laserPointerActive) return true;
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
    m_toolManager->setTextEditingBounds(rect());

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
    if (m_showSubToolbar) {
        if (m_laserPointerActive) {
            activeButtonId = kLaserPointerButtonId;
        } else if (isDrawingTool(m_currentToolId)) {
            activeButtonId = static_cast<int>(m_currentToolId);
        }
    }
    // Handle Whiteboard/Blackboard background mode
    if (m_bgMode == CanvasBackgroundMode::Whiteboard) {
        activeButtonId = static_cast<int>(ToolId::CanvasWhiteboard);
    }
    if (m_bgMode == CanvasBackgroundMode::Blackboard) {
        activeButtonId = static_cast<int>(ToolId::CanvasBlackboard);
    }
    m_toolbar->setActiveButton(activeButtonId);
    m_toolbar->draw(painter);

    // Use unified color and width widget with data-driven configuration
    QRect toolbarRect = m_toolbar->boundingRect();
    if (shouldShowColorAndWidthWidget()) {
        m_colorAndWidthWidget->setVisible(true);
        // Laser pointer is ScreenCanvas-local and not part of ToolRegistry.
        if (m_laserPointerActive) {
            ToolSectionConfig laserConfig;
            laserConfig.showColorSection = true;
            laserConfig.showWidthSection = true;
            laserConfig.applyTo(m_colorAndWidthWidget);
        } else {
            ToolSectionConfig config = ToolSectionConfig::forTool(m_currentToolId);
            if (m_currentToolId == ToolId::Mosaic) {
                config.showAutoBlurSection = false;
            }
            config.applyTo(m_colorAndWidthWidget);
        }
        m_colorAndWidthWidget->updatePosition(toolbarRect, true, width());
        m_colorAndWidthWidget->draw(painter);
    }
    else {
        m_colorAndWidthWidget->setVisible(false);
    }

    // Draw emoji picker when EmojiSticker tool is selected
    if (!m_laserPointerActive && m_currentToolId == ToolId::EmojiSticker && m_showSubToolbar) {
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
    const bool shapeInteractionActive =
        m_shapeAnnotationEditor &&
        (m_shapeAnnotationEditor->isDragging() || m_shapeAnnotationEditor->isTransforming());

    // Use dirty region optimization during drag operations
    if (m_isEmojiDragging || m_isEmojiScaling || m_isEmojiRotating ||
        m_isArrowDragging || m_isPolylineDragging || shapeInteractionActive) {
        // Draw cached content for non-dragged items, then draw dragged item on top
        m_annotationLayer->drawWithDirtyRegion(painter, size(), devicePixelRatioF(),
                                                m_annotationLayer->selectedIndex());
    } else {
        m_annotationLayer->drawCached(painter, size(), devicePixelRatioF());
    }

    // Draw transformation gizmo for selected text annotation
    if (auto* textItem = getSelectedTextAnnotation()) {
        TransformationGizmo::draw(painter, textItem);
    }

    // Draw transformation gizmo for selected EmojiSticker
    if (auto* emojiItem = getSelectedEmojiStickerAnnotation()) {
        TransformationGizmo::draw(painter, emojiItem);
    }

    // Draw transformation gizmo for selected shape annotation
    if (auto* shapeItem = getSelectedShapeAnnotation()) {
        TransformationGizmo::draw(painter, shapeItem);
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

void ScreenCanvas::handleToolbarClick(int buttonId)
{
    if (isLaserPointerButtonId(buttonId)) {
        handleLaserPointerClick();
        return;
    }

    if (!isManagedToolButtonId(buttonId)) {
        return;
    }

    const ToolId toolId = static_cast<ToolId>(buttonId);
    const auto& dispatch = toolbarDispatchTable();
    auto it = dispatch.find(toolId);
    if (it == dispatch.end() || !it->second) {
        return;
    }
    (this->*(it->second))(toolId);
}

void ScreenCanvas::handlePersistentToolClick(ToolId toolId)
{
    if (m_laserPointerActive) {
        m_laserPointerActive = false;
        if (m_currentToolId != toolId) {
            m_currentToolId = toolId;
            m_toolManager->setCurrentTool(toolId);
        }
        m_showSubToolbar = true;
    } else if (m_currentToolId == toolId) {
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
}

void ScreenCanvas::handleLaserPointerClick()
{
    if (m_laserPointerActive) {
        // Same tool clicked - toggle sub-toolbar visibility.
        m_showSubToolbar = !m_showSubToolbar;
    } else {
        m_laserPointerActive = true;
        m_showSubToolbar = true;
        if (m_toolManager->isDrawing()) {
            m_toolManager->cancelDrawing();
        }
        m_laserRenderer->setColor(m_toolManager->color());
        m_laserRenderer->setWidth(m_toolManager->width());
    }

    setToolCursor();
    update();
}

void ScreenCanvas::handleCanvasModeToggle(ToolId toolId)
{
    if (toolId == ToolId::CanvasWhiteboard) {
        if (m_bgMode == CanvasBackgroundMode::Whiteboard) {
            setBackgroundMode(CanvasBackgroundMode::Screen);
        } else {
            setBackgroundMode(CanvasBackgroundMode::Whiteboard);
        }
        return;
    }

    if (toolId == ToolId::CanvasBlackboard) {
        if (m_bgMode == CanvasBackgroundMode::Blackboard) {
            setBackgroundMode(CanvasBackgroundMode::Screen);
        } else {
            setBackgroundMode(CanvasBackgroundMode::Blackboard);
        }
    }
}

void ScreenCanvas::handleActionToolClick(ToolId toolId)
{
    const auto& dispatch = actionDispatchTable();
    auto it = dispatch.find(toolId);
    if (it == dispatch.end() || !it->second) {
        return;
    }
    (this->*(it->second))(toolId);
}

void ScreenCanvas::handleUndoAction(ToolId)
{
    if (m_annotationLayer->canUndo()) {
        m_annotationLayer->undo();
        update();
    }
}

void ScreenCanvas::handleRedoAction(ToolId)
{
    if (m_annotationLayer->canRedo()) {
        m_annotationLayer->redo();
        update();
    }
}

void ScreenCanvas::handleClearAction(ToolId)
{
    m_annotationLayer->clear();
    update();
}

void ScreenCanvas::handleExitAction(ToolId)
{
    close();
}

bool ScreenCanvas::isDrawingTool(ToolId toolId) const
{
    return ToolTraits::isDrawingTool(toolId);
}

void ScreenCanvas::setToolCursor()
{
    auto& cursorManager = CursorManager::instance();
    if (m_laserPointerActive) {
        cursorManager.pushCursorForWidget(this, CursorContext::Tool, Qt::CrossCursor);
    } else {
        cursorManager.updateToolCursorForWidget(this);
    }
}

void ScreenCanvas::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_consumeNextToolRelease = false;

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
                handleToolbarClick(buttonId);
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
        if (m_laserPointerActive) {
            m_laserRenderer->startDrawing(event->pos());
            update();
            return;
        }

        // Active drawing interactions (e.g., Arrow polyline mode) own press events.
        if (m_toolManager && m_toolManager->isDrawing()) {
            m_toolManager->handleMousePress(event->pos(), event->modifiers());
            update();
            return;
        }

        // Check for Emoji sticker annotation interaction.
        if (handleEmojiStickerAnnotationPress(event->pos())) {
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

        // Check shape interaction after arrow/polyline so top-most handles win.
        if (m_toolManager &&
            m_toolManager->handleShapeInteractionPress(event->pos(), event->modifiers())) {
            return;
        }

        // Text interaction stays global, but after annotation hit tests so
        // overlapping geometry keeps annotation manipulation precedence.
        if (m_toolManager &&
            m_toolManager->handleTextInteractionPress(event->pos(), event->modifiers())) {
            return;
        }

        // Clicked elsewhere: clear active annotation selection.
        if (m_annotationLayer->selectedIndex() >= 0) {
            bool clearedSelectedEmojiInEmojiTool = false;
            if (m_currentToolId == ToolId::EmojiSticker) {
                clearedSelectedEmojiInEmojiTool =
                    dynamic_cast<EmojiStickerAnnotation*>(m_annotationLayer->selectedItem()) != nullptr;
            }
            m_annotationLayer->clearSelection();
            update();
            if (clearedSelectedEmojiInEmojiTool) {
                // Keep this click for deselection only; do not place a new emoji on release.
                m_consumeNextToolRelease = true;
                return;
            }
        }

        // Start annotation drawing
        if (!m_laserPointerActive && isDrawingTool(m_currentToolId)) {
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
    if (m_laserPointerActive && m_laserRenderer->isDrawing()) {
        m_laserRenderer->updateDrawing(event->pos());
        update();
        return;
    }

    if (m_toolManager &&
        m_toolManager->handleTextInteractionMove(event->pos(), event->modifiers())) {
        return;
    }

    // Handle Emoji sticker dragging/scaling/rotating
    if (m_isEmojiDragging || m_isEmojiScaling || m_isEmojiRotating) {
        handleEmojiStickerAnnotationMove(event->pos());
        return;
    }

    if (m_toolManager &&
        m_toolManager->handleShapeInteractionMove(event->pos(), event->modifiers())) {
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
        if (m_laserPointerActive && m_laserRenderer->isDrawing()) {
            m_laserRenderer->stopDrawing();
            update();
            return;
        }

        // Laser pointer mode is ScreenCanvas-local and must not trigger
        // release-driven annotation tools (e.g. StepBadge/EmojiSticker).
        if (m_laserPointerActive) {
            return;
        }

        if (m_toolManager &&
            m_toolManager->handleTextInteractionRelease(event->pos(), event->modifiers())) {
            update();
            return;
        }

        // Text re-edit was entered on this click sequence, so consume this release
        // to avoid triggering release-based tools (StepBadge/EmojiSticker).
        if (m_consumeNextToolRelease) {
            m_consumeNextToolRelease = false;
            return;
        }

        // Handle emoji sticker release
        if (handleEmojiStickerAnnotationRelease(event->pos())) {
            return;
        }

        if (m_toolManager &&
            m_toolManager->handleShapeInteractionRelease(event->pos(), event->modifiers())) {
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

    // Active drawing interactions (e.g., Arrow polyline mode) own double-click.
    if (m_toolManager && m_toolManager->isDrawing()) {
        m_toolManager->handleDoubleClick(event->pos());
        update();
        return;
    }

    if (m_toolManager &&
        m_toolManager->handleTextInteractionDoubleClick(event->pos())) {
        update();
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

void ScreenCanvas::onArrowStyleChanged(LineEndStyle style)
{
    m_toolManager->setArrowStyle(style);
    AnnotationSettingsManager::instance().saveArrowStyle(style);
    update();
}

void ScreenCanvas::onLineStyleChanged(LineStyle style)
{
    m_toolManager->setLineStyle(style);
    AnnotationSettingsManager::instance().saveLineStyle(style);
    update();
}

void ScreenCanvas::onTextEditingFinished(const QString& text, const QPoint& position)
{
    m_textAnnotationEditor->finishEditing(text, position, m_toolManager->color());
}

// Font dropdown handlers

void ScreenCanvas::onFontSizeDropdownRequested(const QPoint& pos)
{
    if (m_annotationContext && m_settingsHelper) {
        m_annotationContext->showTextFontSizeDropdown(*m_settingsHelper, pos);
    }
}

void ScreenCanvas::onFontFamilyDropdownRequested(const QPoint& pos)
{
    if (m_annotationContext && m_settingsHelper) {
        m_annotationContext->showTextFontFamilyDropdown(*m_settingsHelper, pos);
    }
}

void ScreenCanvas::onFontSizeSelected(int size)
{
    if (m_annotationContext) {
        m_annotationContext->applyTextFontSize(size);
    }
}

void ScreenCanvas::onFontFamilySelected(const QString& family)
{
    if (m_annotationContext) {
        m_annotationContext->applyTextFontFamily(family);
    }
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
    const QSize physicalSize = CoordinateHelper::toPhysical(size(), m_devicePixelRatio);
    QPixmap pixmap(physicalSize);
    pixmap.setDevicePixelRatio(m_devicePixelRatio);
    pixmap.fill(color);
    return pixmap;
}

// ============================================================================
// Text, Emoji, Arrow and Polyline Handling
// ============================================================================

TextBoxAnnotation* ScreenCanvas::getSelectedTextAnnotation()
{
    if (!m_annotationLayer) return nullptr;
    int index = m_annotationLayer->selectedIndex();
    if (index >= 0) {
        return dynamic_cast<TextBoxAnnotation*>(m_annotationLayer->itemAt(index));
    }
    return nullptr;
}

EmojiStickerAnnotation* ScreenCanvas::getSelectedEmojiStickerAnnotation()
{
    if (!m_annotationLayer) return nullptr;
    int index = m_annotationLayer->selectedIndex();
    if (index >= 0) {
        return dynamic_cast<EmojiStickerAnnotation*>(m_annotationLayer->itemAt(index));
    }
    return nullptr;
}

ShapeAnnotation* ScreenCanvas::getSelectedShapeAnnotation()
{
    if (!m_annotationLayer) return nullptr;
    int index = m_annotationLayer->selectedIndex();
    if (index >= 0) {
        return dynamic_cast<ShapeAnnotation*>(m_annotationLayer->itemAt(index));
    }
    return nullptr;
}

bool ScreenCanvas::handleEmojiStickerAnnotationPress(const QPoint& pos)
{
    auto& cursorManager = CursorManager::instance();

    // First check if we're clicking a gizmo handle/body of an already-selected emoji.
    if (auto* emojiItem = getSelectedEmojiStickerAnnotation()) {
        GizmoHandle handle = TransformationGizmo::hitTest(emojiItem, pos);
        if (handle != GizmoHandle::None) {
            m_isEmojiDragging = false;
            m_isEmojiScaling = false;
            m_isEmojiRotating = false;
            if (handle == GizmoHandle::Body) {
                m_isEmojiDragging = true;
                m_emojiDragStart = pos;
                cursorManager.setInputStateForWidget(this, InputState::Moving);
            } else if (handle == GizmoHandle::Rotation) {
                m_isEmojiRotating = true;
                m_activeEmojiHandle = handle;
                m_emojiStartCenter = emojiItem->center();
                m_emojiStartRotation = emojiItem->rotation();
                QPointF delta = QPointF(pos) - m_emojiStartCenter;
                m_emojiStartAngle = qRadiansToDegrees(qAtan2(delta.y(), delta.x()));
                cursorManager.setHoverTargetForWidget(
                    this,
                    HoverTarget::GizmoHandle,
                    static_cast<int>(handle));
            } else {
                m_isEmojiScaling = true;
                m_activeEmojiHandle = handle;
                m_emojiStartCenter = emojiItem->center();
                m_emojiStartScale = emojiItem->scale();
                QPointF delta = QPointF(pos) - m_emojiStartCenter;
                m_emojiStartDistance = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
                cursorManager.setHoverTargetForWidget(
                    this,
                    HoverTarget::GizmoHandle,
                    static_cast<int>(handle));
            }
            update();
            return true;
        }
    }

    // Check if clicking on an unselected emoji.
    int hitIndex = m_annotationLayer->hitTestEmojiSticker(pos);
    if (hitIndex < 0) {
        return false;
    }

    m_annotationLayer->setSelectedIndex(hitIndex);
    if (auto* emojiItem = getSelectedEmojiStickerAnnotation()) {
        m_isEmojiDragging = false;
        m_isEmojiScaling = false;
        m_isEmojiRotating = false;
        GizmoHandle handle = TransformationGizmo::hitTest(emojiItem, pos);
        if (handle == GizmoHandle::Body || handle == GizmoHandle::None) {
            m_isEmojiDragging = true;
            m_emojiDragStart = pos;
            cursorManager.setInputStateForWidget(this, InputState::Moving);
        } else if (handle == GizmoHandle::Rotation) {
            m_isEmojiRotating = true;
            m_activeEmojiHandle = handle;
            m_emojiStartCenter = emojiItem->center();
            m_emojiStartRotation = emojiItem->rotation();
            QPointF delta = QPointF(pos) - m_emojiStartCenter;
            m_emojiStartAngle = qRadiansToDegrees(qAtan2(delta.y(), delta.x()));
            cursorManager.setHoverTargetForWidget(
                this,
                HoverTarget::GizmoHandle,
                static_cast<int>(handle));
        } else {
            m_isEmojiScaling = true;
            m_activeEmojiHandle = handle;
            m_emojiStartCenter = emojiItem->center();
            m_emojiStartScale = emojiItem->scale();
            QPointF delta = QPointF(pos) - m_emojiStartCenter;
            m_emojiStartDistance = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
            cursorManager.setHoverTargetForWidget(
                this,
                HoverTarget::GizmoHandle,
                static_cast<int>(handle));
        }
    }

    update();
    return true;
}

bool ScreenCanvas::handleEmojiStickerAnnotationMove(const QPoint& pos)
{
    if (m_annotationLayer->selectedIndex() < 0) {
        return false;
    }

    auto* emojiItem = getSelectedEmojiStickerAnnotation();
    if (!emojiItem) {
        return false;
    }

    QRect oldRect = emojiItem->boundingRect().adjusted(-20, -20, 20, 20);

    if (m_isEmojiDragging) {
        QPoint delta = pos - m_emojiDragStart;
        emojiItem->moveBy(delta);
        m_emojiDragStart = pos;
    } else if (m_isEmojiRotating) {
        QPointF delta = QPointF(pos) - m_emojiStartCenter;
        qreal currentAngle = qRadiansToDegrees(qAtan2(delta.y(), delta.x()));
        qreal angleDelta = normalizeAngleDelta(currentAngle - m_emojiStartAngle);
        emojiItem->setRotation(m_emojiStartRotation + angleDelta);
    } else if (m_isEmojiScaling && m_emojiStartDistance > 0.0) {
        QPointF delta = QPointF(pos) - m_emojiStartCenter;
        qreal currentDistance = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
        qreal scaleFactor = currentDistance / m_emojiStartDistance;
        emojiItem->setScale(m_emojiStartScale * scaleFactor);
    } else {
        return false;
    }

    QRect newRect = emojiItem->boundingRect().adjusted(-20, -20, 20, 20);
    m_annotationLayer->markDirtyRect(oldRect.united(newRect));
    update();
    return true;
}

bool ScreenCanvas::handleEmojiStickerAnnotationRelease(const QPoint& pos)
{
    Q_UNUSED(pos);
    if (!m_isEmojiDragging && !m_isEmojiScaling && !m_isEmojiRotating) {
        return false;
    }

    m_annotationLayer->commitDirtyRegion(size(), devicePixelRatioF());
    m_isEmojiDragging = false;
    m_isEmojiScaling = false;
    m_isEmojiRotating = false;
    m_activeEmojiHandle = GizmoHandle::None;
    m_emojiStartDistance = 0.0;
    m_emojiStartAngle = 0.0;
    CursorManager::instance().setInputStateForWidget(this, InputState::Idle);
    update();
    return true;
}

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

    // Selected text gizmo handles have highest hover priority.
    if (auto* textItem = getSelectedTextAnnotation()) {
        GizmoHandle handle = TransformationGizmo::hitTest(textItem, pos);
        if (handle != GizmoHandle::None) {
            cursorManager.setHoverTargetForWidget(this, HoverTarget::GizmoHandle, static_cast<int>(handle));
            return;
        }
    }

    if (auto* shapeItem = getSelectedShapeAnnotation()) {
        GizmoHandle handle = TransformationGizmo::hitTest(shapeItem, pos);
        if (handle != GizmoHandle::None) {
            cursorManager.setHoverTargetForWidget(this, HoverTarget::GizmoHandle, static_cast<int>(handle));
            return;
        }
    }

    // Use unified hit testing from CursorManager
    auto result = CursorManager::hitTestAnnotations(
        pos,
        m_annotationLayer,
        getSelectedEmojiStickerAnnotation(),
        getSelectedArrowAnnotation(),
        getSelectedPolylineAnnotation(),
        m_currentToolId != ToolId::EmojiSticker
    );

    if (result.hit) {
        cursorManager.setHoverTargetForWidget(this, result.target, result.handleIndex);
    } else if (m_annotationLayer->hitTestShape(pos) >= 0) {
        cursorManager.setHoverTargetForWidget(this, HoverTarget::Annotation);
    } else {
        // No annotation hit - clear hover target to let tool cursor show
        cursorManager.setHoverTargetForWidget(this, HoverTarget::None);
    }
}
