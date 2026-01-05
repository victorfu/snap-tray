#include "RegionSelector.h"
#include "region/RegionPainter.h"
#include "region/RegionInputHandler.h"
#include "region/RegionToolbarHandler.h"
#include "region/RegionSettingsHelper.h"
#include "annotations/AllAnnotations.h"
#include "GlassRenderer.h"
#include "ToolbarStyle.h"
#include "IconRenderer.h"
#include "ColorPaletteWidget.h"
#include "ColorAndWidthWidget.h"
#include "ColorPickerDialog.h"
#include "OCRManager.h"
#include "PlatformFeatures.h"
#include "detection/AutoBlurManager.h"
#include "settings/AnnotationSettingsManager.h"
#include "settings/FileSettingsManager.h"
#include "tools/handlers/EraserToolHandler.h"
#include "tools/handlers/MosaicToolHandler.h"
#include <QTextEdit>

#include <cstring>
#include <map>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QDebug>
#include <QCursor>
#include <QClipboard>
#include <QFileDialog>
#include <QStandardPaths>
#include <QDateTime>
#include <QDir>
#include <QInputDialog>
#include <QToolTip>
#include <QPointer>
#include <QColorDialog>
#include <QLabel>
#include <QTimer>
#include <QSettings>
#include <QtMath>
#include <QMenu>
#include <QFontDatabase>
#include <QDateTime>

// Tool capability lookup table - replaces multiple switch statements
struct ToolCapabilities {
    bool showInPalette;     // Show in color palette
    bool needsWidth;        // Show width control
    bool needsColorOrWidth; // Show unified color/width widget
};

static const std::map<ToolbarButton, ToolCapabilities> kToolCapabilities = {
    {ToolbarButton::Selection,  {false, false, false}},
    {ToolbarButton::Pencil,     {true,  true,  true}},
    {ToolbarButton::Marker,     {true,  false, true}},
    {ToolbarButton::Arrow,      {true,  true,  true}},
    {ToolbarButton::Shape,      {true,  true,  true}},
    {ToolbarButton::Text,       {true,  false, true}},
    {ToolbarButton::Mosaic,     {false, true,  true}},
    {ToolbarButton::StepBadge,  {true,  false, true}},
    {ToolbarButton::Eraser,     {false, true,  false}},
};

// Create a rounded square cursor for mosaic tool (matching EraserToolHandler pattern)
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

    // Draw darker inner outline for contrast on light backgrounds
    painter.setPen(QPen(QColor(100, 100, 100, 180), 0.5, Qt::SolidLine));
    QRect innerOutline = innerRect.adjusted(1, 1, -1, -1);
    painter.drawRoundedRect(innerOutline, 1, 1);

    painter.end();

    int hotspot = cursorSize / 2;
    return QCursor(pixmap, hotspot, hotspot);
}

RegionSelector::RegionSelector(QWidget* parent)
    : QWidget(parent)
    , m_currentScreen(nullptr)
    , m_selectionManager(nullptr)
    , m_devicePixelRatio(1.0)
    , m_toolbar(nullptr)
    , m_annotationLayer(nullptr)
    , m_currentTool(ToolbarButton::Selection)
    , m_showSubToolbar(true)
    , m_annotationColor(Qt::red)  // Will be overwritten by loadAnnotationColor()
    , m_annotationWidth(3)        // Will be overwritten by loadAnnotationWidth()
    , m_isDrawing(false)
    , m_isClosing(false)
    , m_isDialogOpen(false)
    , m_windowDetector(nullptr)
    , m_ocrManager(nullptr)
    , m_ocrInProgress(false)
    , m_autoBlurManager(nullptr)
    , m_autoBlurInProgress(false)
    , m_colorPalette(nullptr)
    , m_textEditor(nullptr)
    , m_colorPickerDialog(nullptr)
    , m_magnifierPanel(nullptr)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);  // 預設已選取完成，使用 ArrowCursor

    // Initialize selection state manager
    m_selectionManager = new SelectionStateManager(this);
    connect(m_selectionManager, &SelectionStateManager::selectionChanged,
            this, [this](const QRect&) { update(); });
    connect(m_selectionManager, &SelectionStateManager::stateChanged,
            this, [this](SelectionStateManager::State newState) {
                // Update cursor based on state
                if (newState == SelectionStateManager::State::None ||
                    newState == SelectionStateManager::State::Selecting) {
                    setCursor(Qt::CrossCursor);
                } else if (newState == SelectionStateManager::State::Moving) {
                    setCursor(Qt::SizeAllCursor);
                } else {
                    setCursor(Qt::ArrowCursor);
                }
            });

    // Initialize annotation layer
    m_annotationLayer = new AnnotationLayer(this);

    // Load saved annotation settings (or defaults)
    auto& settings = AnnotationSettingsManager::instance();
    m_annotationColor = settings.loadColor();
    m_annotationWidth = settings.loadWidth();
    m_arrowStyle = RegionSettingsHelper::loadArrowStyle();
    m_lineStyle = RegionSettingsHelper::loadLineStyle();
    m_stepBadgeSize = settings.loadStepBadgeSize();
    m_mosaicBlurType = settings.loadMosaicBlurType();

    // Initialize tool manager
    m_toolManager = new ToolManager(this);
    m_toolManager->registerDefaultHandlers();
    m_toolManager->setAnnotationLayer(m_annotationLayer);
    m_toolManager->setColor(m_annotationColor);
    m_toolManager->setWidth(m_annotationWidth);
    m_toolManager->setArrowStyle(m_arrowStyle);
    m_toolManager->setLineStyle(m_lineStyle);
    m_toolManager->setMosaicBlurType(static_cast<MosaicStroke::BlurType>(m_mosaicBlurType));
    connect(m_toolManager, &ToolManager::needsRepaint, this, QOverload<>::of(&QWidget::update));

    // Initialize OCR manager if available on this platform
    m_ocrManager = PlatformFeatures::instance().createOCRManager(this);

    // Initialize auto-blur manager
    m_autoBlurManager = new AutoBlurManager(this);
    m_autoBlurManager->initialize();

    // Initialize toolbar widget
    m_toolbar = new ToolbarWidget(this);
    connect(m_toolbar, &ToolbarWidget::buttonClicked, this, [this](int buttonId) {
        handleToolbarClick(static_cast<ToolbarButton>(buttonId));
        });

    // Initialize inline text editor
    m_textEditor = new InlineTextEditor(this);
    connect(m_textEditor, &InlineTextEditor::editingFinished,
        this, &RegionSelector::onTextEditingFinished);
    connect(m_textEditor, &InlineTextEditor::editingCancelled,
        this, [this]() {
            m_textAnnotationEditor->cancelEditing();
        });

    // Initialize text annotation editor component
    m_textAnnotationEditor = new TextAnnotationEditor(this);
    m_textAnnotationEditor->setAnnotationLayer(m_annotationLayer);
    m_textAnnotationEditor->setTextEditor(m_textEditor);
    m_textAnnotationEditor->setParentWidget(this);
    connect(m_textAnnotationEditor, &TextAnnotationEditor::updateRequested,
        this, QOverload<>::of(&QWidget::update));

    // Initialize color palette widget
    m_colorPalette = new ColorPaletteWidget(this);
    m_colorPalette->setCurrentColor(m_annotationColor);
    connect(m_colorPalette, &ColorPaletteWidget::colorSelected,
        this, &RegionSelector::onColorSelected);
    connect(m_colorPalette, &ColorPaletteWidget::moreColorsRequested,
        this, &RegionSelector::onMoreColorsRequested);

    // Initialize unified color and width widget
    m_colorAndWidthWidget = new ColorAndWidthWidget(this);
    m_colorAndWidthWidget->setCurrentColor(m_annotationColor);
    m_colorAndWidthWidget->setCurrentWidth(m_annotationWidth);
    m_colorAndWidthWidget->setWidthRange(1, 20);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::colorSelected,
        this, &RegionSelector::onColorSelected);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::moreColorsRequested,
        this, &RegionSelector::onMoreColorsRequested);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::widthChanged,
        this, &RegionSelector::onLineWidthChanged);
    // Mosaic now uses widthChanged signal like other tools (no separate mosaicWidthChanged)

    // Configure text annotation editor with ColorAndWidthWidget
    m_textAnnotationEditor->setColorAndWidthWidget(m_colorAndWidthWidget);

    // Load text formatting settings from TextAnnotationEditor
    TextFormattingState textFormatting = m_textAnnotationEditor->formatting();
    m_colorAndWidthWidget->setBold(textFormatting.bold);
    m_colorAndWidthWidget->setItalic(textFormatting.italic);
    m_colorAndWidthWidget->setUnderline(textFormatting.underline);
    m_colorAndWidthWidget->setFontSize(textFormatting.fontSize);
    m_colorAndWidthWidget->setFontFamily(textFormatting.fontFamily);

    // Connect text formatting signals to TextAnnotationEditor
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::boldToggled,
        m_textAnnotationEditor, &TextAnnotationEditor::setBold);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::italicToggled,
        m_textAnnotationEditor, &TextAnnotationEditor::setItalic);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::underlineToggled,
        m_textAnnotationEditor, &TextAnnotationEditor::setUnderline);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::fontSizeDropdownRequested,
        this, &RegionSelector::onFontSizeDropdownRequested);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::fontFamilyDropdownRequested,
        this, &RegionSelector::onFontFamilyDropdownRequested);

    // Connect arrow style signal
    m_colorAndWidthWidget->setArrowStyle(m_arrowStyle);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::arrowStyleChanged,
        this, &RegionSelector::onArrowStyleChanged);

    // Connect line style signal
    m_colorAndWidthWidget->setLineStyle(m_lineStyle);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::lineStyleChanged,
        this, &RegionSelector::onLineStyleChanged);

    // Connect shape section signals
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
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::stepBadgeSizeChanged,
        this, &RegionSelector::onStepBadgeSizeChanged);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::autoBlurRequested,
        this, &RegionSelector::performAutoBlur);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::mosaicBlurTypeChanged,
        this, [this](MosaicBlurTypeSection::BlurType type) {
            m_mosaicBlurType = type;
            m_toolManager->setMosaicBlurType(static_cast<MosaicStroke::BlurType>(type));
            AnnotationSettingsManager::instance().saveMosaicBlurType(type);
        });

    // Initialize loading spinner for OCR
    m_loadingSpinner = new LoadingSpinnerRenderer(this);
    connect(m_loadingSpinner, &LoadingSpinnerRenderer::needsRepaint,
        this, QOverload<>::of(&QWidget::update));

    // OCR toast label (shows success/failure message)
    m_ocrToastLabel = new QLabel(this);
    m_ocrToastLabel->hide();

    m_ocrToastTimer = new QTimer(this);
    m_ocrToastTimer->setSingleShot(true);
    connect(m_ocrToastTimer, &QTimer::timeout, m_ocrToastLabel, &QLabel::hide);

    // Install application-level event filter to capture ESC even when window loses focus
    qApp->installEventFilter(this);

    // Start creation timer for startup protection
    m_createdAt.start();

    // Initialize magnifier panel component
    m_magnifierPanel = new MagnifierPanel(this);

    // Initialize corner radius slider widget
    m_radiusSliderWidget = new RadiusSliderWidget(this);
    m_cornerRadius = settings.loadCornerRadius();
    m_radiusSliderWidget->setCurrentRadius(m_cornerRadius);
    connect(m_radiusSliderWidget, &RadiusSliderWidget::radiusChanged,
        this, &RegionSelector::onCornerRadiusChanged);

    // Initialize painting component
    m_painter = new RegionPainter(this);
    m_painter->setSelectionManager(m_selectionManager);
    m_painter->setAnnotationLayer(m_annotationLayer);
    m_painter->setToolManager(m_toolManager);
    m_painter->setToolbar(m_toolbar);
    m_painter->setRadiusSliderWidget(m_radiusSliderWidget);
    m_painter->setParentWidget(this);

    // Initialize input handling component
    m_inputHandler = new RegionInputHandler(this);
    m_inputHandler->setSelectionManager(m_selectionManager);
    m_inputHandler->setAnnotationLayer(m_annotationLayer);
    m_inputHandler->setToolManager(m_toolManager);
    m_inputHandler->setToolbar(m_toolbar);
    m_inputHandler->setTextEditor(m_textEditor);
    m_inputHandler->setTextAnnotationEditor(m_textAnnotationEditor);
    m_inputHandler->setColorAndWidthWidget(m_colorAndWidthWidget);
    m_inputHandler->setColorPalette(m_colorPalette);
    m_inputHandler->setRadiusSliderWidget(m_radiusSliderWidget);
    m_inputHandler->setUpdateThrottler(&m_updateThrottler);
    m_inputHandler->setParentWidget(this);

    // Connect input handler signals
    connect(m_inputHandler, &RegionInputHandler::cursorChangeRequested,
        this, [this](Qt::CursorShape cursor) { setCursor(cursor); });
    connect(m_inputHandler, &RegionInputHandler::toolCursorRequested,
        this, &RegionSelector::setToolCursor);
    connect(m_inputHandler, qOverload<>(&RegionInputHandler::updateRequested),
        this, qOverload<>(&QWidget::update));
    connect(m_inputHandler, qOverload<const QRect&>(&RegionInputHandler::updateRequested),
        this, qOverload<const QRect&>(&QWidget::update));
    connect(m_inputHandler, &RegionInputHandler::toolbarClickRequested,
        this, [this](int buttonId) { handleToolbarClick(static_cast<ToolbarButton>(buttonId)); });
    connect(m_inputHandler, &RegionInputHandler::windowDetectionRequested,
        this, &RegionSelector::updateWindowDetection);
    connect(m_inputHandler, &RegionInputHandler::textReEditingRequested,
        this, &RegionSelector::startTextReEditing);
    connect(m_inputHandler, &RegionInputHandler::selectionFinished,
        this, [this]() {
            m_selectionManager->finishSelection();
            setCursor(Qt::ArrowCursor);
        });
    connect(m_inputHandler, &RegionInputHandler::fullScreenSelectionRequested,
        this, [this]() {
            QRect screenGeom = m_currentScreen->geometry();
            m_selectionManager->setFromDetectedWindow(QRect(0, 0, screenGeom.width(), screenGeom.height()));
            setCursor(Qt::ArrowCursor);
        });
    connect(m_inputHandler, &RegionInputHandler::drawingStateChanged,
        this, [this](bool isDrawing) { m_isDrawing = isDrawing; });
    connect(m_inputHandler, &RegionInputHandler::detectionCleared,
        this, [this]() {
            m_highlightedWindowRect = QRect();
            m_detectedWindow.reset();
        });

    // Initialize toolbar handler component
    m_toolbarHandler = new RegionToolbarHandler(this);
    m_toolbarHandler->setToolbar(m_toolbar);
    m_toolbarHandler->setToolManager(m_toolManager);
    m_toolbarHandler->setAnnotationLayer(m_annotationLayer);
    m_toolbarHandler->setColorAndWidthWidget(m_colorAndWidthWidget);
    m_toolbarHandler->setSelectionManager(m_selectionManager);
    m_toolbarHandler->setOCRManager(m_ocrManager);
    m_toolbarHandler->setParentWidget(this);

    // Connect toolbar handler signals
    connect(m_toolbarHandler, &RegionToolbarHandler::toolChanged,
        this, [this](ToolbarButton tool, bool showSubToolbar) {
            m_currentTool = tool;
            m_showSubToolbar = showSubToolbar;
        });
    connect(m_toolbarHandler, &RegionToolbarHandler::updateRequested,
        this, qOverload<>(&QWidget::update));
    connect(m_toolbarHandler, &RegionToolbarHandler::undoRequested,
        m_annotationLayer, &AnnotationLayer::undo);
    connect(m_toolbarHandler, &RegionToolbarHandler::redoRequested,
        m_annotationLayer, &AnnotationLayer::redo);
    connect(m_toolbarHandler, &RegionToolbarHandler::cancelRequested,
        this, [this]() {
            emit selectionCancelled();
            close();
        });
    connect(m_toolbarHandler, &RegionToolbarHandler::pinRequested,
        this, &RegionSelector::finishSelection);
    connect(m_toolbarHandler, &RegionToolbarHandler::recordRequested,
        this, [this]() {
            emit recordingRequested(localToGlobal(m_selectionManager->selectionRect()), m_currentScreen);
            close();
        });
    connect(m_toolbarHandler, &RegionToolbarHandler::scrollCaptureRequested,
        this, [this]() {
            emit scrollingCaptureRequested(localToGlobal(m_selectionManager->selectionRect()), m_currentScreen);
            close();
        });
    connect(m_toolbarHandler, &RegionToolbarHandler::saveRequested,
        this, &RegionSelector::saveToFile);
    connect(m_toolbarHandler, &RegionToolbarHandler::copyRequested,
        this, &RegionSelector::copyToClipboard);
    connect(m_toolbarHandler, &RegionToolbarHandler::ocrRequested,
        this, &RegionSelector::performOCR);

    // Connect ColorAndWidthWidget configuration signals
    connect(m_toolbarHandler, &RegionToolbarHandler::showSizeSectionRequested,
        m_colorAndWidthWidget, &ColorAndWidthWidget::setShowSizeSection);
    connect(m_toolbarHandler, &RegionToolbarHandler::showWidthSectionRequested,
        m_colorAndWidthWidget, &ColorAndWidthWidget::setShowWidthSection);
    connect(m_toolbarHandler, &RegionToolbarHandler::widthSectionHiddenRequested,
        m_colorAndWidthWidget, &ColorAndWidthWidget::setWidthSectionHidden);
    connect(m_toolbarHandler, &RegionToolbarHandler::showColorSectionRequested,
        m_colorAndWidthWidget, &ColorAndWidthWidget::setShowColorSection);
    connect(m_toolbarHandler, &RegionToolbarHandler::showMosaicBlurTypeSectionRequested,
        m_colorAndWidthWidget, &ColorAndWidthWidget::setShowMosaicBlurTypeSection);
    connect(m_toolbarHandler, &RegionToolbarHandler::widthRangeRequested,
        m_colorAndWidthWidget, &ColorAndWidthWidget::setWidthRange);
    connect(m_toolbarHandler, &RegionToolbarHandler::currentWidthRequested,
        m_colorAndWidthWidget, &ColorAndWidthWidget::setCurrentWidth);
    connect(m_toolbarHandler, &RegionToolbarHandler::stepBadgeSizeRequested,
        m_colorAndWidthWidget, &ColorAndWidthWidget::setStepBadgeSize);
    connect(m_toolbarHandler, &RegionToolbarHandler::mosaicBlurTypeRequested,
        this, [this](int type) {
            m_colorAndWidthWidget->setMosaicBlurType(static_cast<MosaicBlurTypeSection::BlurType>(type));
        });

    // Connect eraser hover clear signal
    connect(m_toolbarHandler, &RegionToolbarHandler::eraserHoverClearRequested,
        this, [this]() {
            if (auto* eraser = dynamic_cast<EraserToolHandler*>(m_toolManager->currentHandler())) {
                eraser->clearHoverPoint();
            }
        });

    // Setup toolbar buttons via handler
    m_toolbarHandler->setupToolbarButtons();

    // Initialize settings helper
    m_settingsHelper = new RegionSettingsHelper(this);
    m_settingsHelper->setParentWidget(this);
    connect(m_settingsHelper, &RegionSettingsHelper::fontSizeSelected,
        this, &RegionSelector::onFontSizeSelected);
    connect(m_settingsHelper, &RegionSettingsHelper::fontFamilySelected,
        this, &RegionSelector::onFontFamilySelected);

    // Initialize update throttler
    m_updateThrottler.startAll();

    // 注意: 不在此處初始化螢幕，由 CaptureManager 調用 initializeForScreen()
}

RegionSelector::~RegionSelector()
{
    // Clean up color picker dialog
    if (m_colorPickerDialog) {
        delete m_colorPickerDialog;
        m_colorPickerDialog = nullptr;
    }

    // Remove event filter
    qApp->removeEventFilter(this);

    qDebug() << "RegionSelector: Destroyed";
}

bool RegionSelector::shouldShowColorPalette() const
{
    if (!m_selectionManager->isComplete()) return false;
    if (!m_showSubToolbar) return false;
    auto it = kToolCapabilities.find(m_currentTool);
    return it != kToolCapabilities.end() && it->second.showInPalette;
}

void RegionSelector::syncColorToAllWidgets(const QColor& color)
{
    m_annotationColor = color;
    AnnotationSettingsManager::instance().saveColor(color);
    m_toolManager->setColor(color);
    m_colorAndWidthWidget->setCurrentColor(color);
    if (m_textEditor->isEditing()) {
        m_textEditor->setColor(color);
    }
    update();
}

void RegionSelector::onColorSelected(const QColor& color)
{
    syncColorToAllWidgets(color);
}

void RegionSelector::onMoreColorsRequested()
{
    if (!m_colorPickerDialog) {
        m_colorPickerDialog = new ColorPickerDialog();
        connect(m_colorPickerDialog, &ColorPickerDialog::colorSelected,
            this, [this](const QColor& color) {
                syncColorToAllWidgets(color);
                m_colorPalette->setCurrentColor(color);
                qDebug() << "Custom color selected:" << color.name();
            });
    }

    m_colorPickerDialog->setCurrentColor(m_annotationColor);
    m_colorAndWidthWidget->setCurrentColor(m_annotationColor);

    // Position at center of screen
    QPoint center = geometry().center();
    m_colorPickerDialog->move(center.x() - 170, center.y() - 210);
    m_colorPickerDialog->show();
    m_colorPickerDialog->raise();
    m_colorPickerDialog->activateWindow();
}

void RegionSelector::onLineWidthChanged(int width)
{
    if (m_currentTool == ToolbarButton::Eraser) {
        m_eraserWidth = width;
        // Don't save eraser width to settings
    }
    else if (m_currentTool == ToolbarButton::Mosaic) {
        m_annotationWidth = width;
        // Update cursor to reflect new width
        setToolCursor();
        AnnotationSettingsManager::instance().saveWidth(width);
    }
    else {
        m_annotationWidth = width;
        AnnotationSettingsManager::instance().saveWidth(width);
    }
    m_toolManager->setWidth(width);
    update();
}

void RegionSelector::onStepBadgeSizeChanged(StepBadgeSize size)
{
    m_stepBadgeSize = size;
    AnnotationSettingsManager::instance().saveStepBadgeSize(size);
    int radius = StepBadgeAnnotation::radiusForSize(size);
    m_toolManager->setWidth(radius);
    update();
}

void RegionSelector::onCornerRadiusChanged(int radius)
{
    m_cornerRadius = radius;
    AnnotationSettingsManager::instance().saveCornerRadius(radius);
    update();
}

int RegionSelector::effectiveCornerRadius() const
{
    QRect sel = m_selectionManager->selectionRect();
    if (sel.isEmpty()) return 0;
    // Cap radius at half the smaller dimension
    int maxRadius = qMin(sel.width(), sel.height()) / 2;
    return qMin(m_cornerRadius, maxRadius);
}

bool RegionSelector::shouldShowColorAndWidthWidget() const
{
    if (!m_selectionManager->isComplete()) return false;
    if (!m_showSubToolbar) return false;
    auto it = kToolCapabilities.find(m_currentTool);
    return it != kToolCapabilities.end() && it->second.needsColorOrWidth;
}

bool RegionSelector::shouldShowWidthControl() const
{
    auto it = kToolCapabilities.find(m_currentTool);
    return it != kToolCapabilities.end() && it->second.needsWidth;
}

int RegionSelector::toolWidthForCurrentTool() const
{
    if (m_currentTool == ToolbarButton::Eraser) {
        return m_eraserWidth;
    }
    if (m_currentTool == ToolbarButton::StepBadge) {
        return StepBadgeAnnotation::radiusForSize(m_stepBadgeSize);
    }
    // Mosaic now uses m_annotationWidth (synced with other tools)
    return m_annotationWidth;
}

const QImage& RegionSelector::getBackgroundImage() const
{
    // Lazy initialization: convert pixmap to image only when first needed (for magnifier)
    // This saves ~126MB on 4K displays until magnifier is actually used
    if (!m_backgroundImageCacheValid) {
        m_backgroundImageCache = m_backgroundPixmap.toImage();
        m_backgroundImageCacheValid = true;
    }
    return m_backgroundImageCache;
}

void RegionSelector::setupScreenGeometry(QScreen* screen)
{
    m_currentScreen = screen ? screen : QGuiApplication::primaryScreen();
    m_devicePixelRatio = m_currentScreen->devicePixelRatio();

    QRect screenGeom = m_currentScreen->geometry();
    setFixedSize(screenGeom.size());
    m_selectionManager->setBounds(QRect(0, 0, screenGeom.width(), screenGeom.height()));
}

void RegionSelector::initializeForScreen(QScreen* screen, const QPixmap& preCapture)
{
    setupScreenGeometry(screen);

    // Use pre-captured pixmap if provided, otherwise capture now
    // Pre-capture allows including popup menus in the screenshot (like Snipaste)
    if (!preCapture.isNull()) {
        m_backgroundPixmap = preCapture;
        qDebug() << "RegionSelector: Using pre-captured screenshot, size:" << preCapture.size();
    }
    else {
        m_backgroundPixmap = m_currentScreen->grabWindow(0);
        qDebug() << "RegionSelector: Captured screenshot now, size:" << m_backgroundPixmap.size();
    }
    // Lazy-load image cache: don't convert now, create on first magnifier access
    m_backgroundImageCacheValid = false;

    // Update tool manager with background pixmap for mosaic tool
    m_toolManager->setSourcePixmap(&m_backgroundPixmap);
    m_toolManager->setDevicePixelRatio(m_devicePixelRatio);

    qDebug() << "RegionSelector: Initialized for screen" << m_currentScreen->name()
        << "logical size:" << m_currentScreen->geometry().size()
        << "pixmap size:" << m_backgroundPixmap.size()
        << "devicePixelRatio:" << m_devicePixelRatio;

    // 不預設選取整個螢幕，等待用戶操作
    m_selectionManager->clearSelection();

    // 將 cursor 全域座標轉換為 widget 本地座標，用於 crosshair/magnifier 初始位置
    QRect screenGeom = m_currentScreen->geometry();
    QPoint globalCursor = QCursor::pos();
    m_currentPoint = globalCursor - screenGeom.topLeft();

    // 鎖定視窗大小，防止 macOS 原生 resize 行為
    setFixedSize(screenGeom.size());

    // Set bounds for selection manager
    m_selectionManager->setBounds(QRect(0, 0, screenGeom.width(), screenGeom.height()));

    // Configure magnifier panel with device pixel ratio
    m_magnifierPanel->setDevicePixelRatio(m_devicePixelRatio);
    m_magnifierPanel->invalidateCache();

    setCursor(Qt::CrossCursor);

    // Initial window detection at cursor position
    if (m_windowDetector && m_windowDetector->isEnabled()) {
        updateWindowDetection(m_currentPoint);
    }
}

// 保留舊方法以保持向後兼容 (但不再使用)
void RegionSelector::captureCurrentScreen()
{
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    initializeForScreen(screen);
}

void RegionSelector::initializeWithRegion(QScreen* screen, const QRect& region)
{
    setupScreenGeometry(screen);

    // Capture the screen first
    m_backgroundPixmap = m_currentScreen->grabWindow(0);
    m_backgroundImageCache = m_backgroundPixmap.toImage();

    // Update tool manager with background pixmap for mosaic tool
    m_toolManager->setSourcePixmap(&m_backgroundPixmap);
    m_toolManager->setDevicePixelRatio(m_devicePixelRatio);

    qDebug() << "RegionSelector: Initialized with region" << region
        << "on screen" << m_currentScreen->name();

    // Convert global region to local coordinates
    QRect screenGeom = m_currentScreen->geometry();
    QRect localRegion = region.translated(-screenGeom.topLeft());

    // Set selection from the region (this marks selection as complete)
    m_selectionManager->setSelectionRect(localRegion);
    m_selectionManager->setFromDetectedWindow(localRegion);

    // Set cursor position
    QPoint globalCursor = QCursor::pos();
    m_currentPoint = globalCursor - screenGeom.topLeft();

    // Trigger repaint to show toolbar (toolbar is drawn in paintEvent when m_selectionComplete is true)
    QTimer::singleShot(0, this, [this]() {
        update();
        });
}

void RegionSelector::setWindowDetector(WindowDetector* detector)
{
    m_windowDetector = detector;
}

void RegionSelector::updateWindowDetection(const QPoint& localPos)
{
    if (!m_windowDetector || !m_windowDetector->isEnabled() || !m_currentScreen) {
        return;
    }

    auto detected = m_windowDetector->detectWindowAt(localToGlobal(localPos));

    if (detected.has_value()) {
        QRect localBounds = globalToLocal(detected->bounds).intersected(rect());

        if (localBounds != m_highlightedWindowRect) {
            m_highlightedWindowRect = localBounds;
            m_detectedWindow = detected;
            update();
        }
    }
    else {
        if (!m_highlightedWindowRect.isNull()) {
            m_highlightedWindowRect = QRect();
            m_detectedWindow.reset();
            update();
        }
    }
}

void RegionSelector::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::Antialiasing);

    // Update painter state before painting
    m_painter->setHighlightedWindowRect(m_highlightedWindowRect);
    m_painter->setDetectedWindowTitle(
        m_detectedWindow.has_value()
            ? QString("%1x%2").arg(m_detectedWindow->bounds.width()).arg(m_detectedWindow->bounds.height())
            : QString());
    m_painter->setCornerRadius(m_cornerRadius);
    m_painter->setShowSubToolbar(m_showSubToolbar);
    m_painter->setCurrentTool(static_cast<int>(m_currentTool));
    m_painter->setDevicePixelRatio(m_devicePixelRatio);

    // Delegate core painting (background, overlay, selection, annotations)
    m_painter->paint(painter, m_backgroundPixmap);

    // Draw toolbar and widgets (UI management stays in RegionSelector)
    QRect selectionRect = m_selectionManager->selectionRect();
    if (m_selectionManager->hasActiveSelection() && selectionRect.isValid()) {
        if (m_selectionManager->hasSelection()) {
            // Update toolbar position and draw
            // Only show active indicator when sub-toolbar is visible
            m_toolbar->setActiveButton(m_showSubToolbar ? static_cast<int>(m_currentTool) : -1);
            m_toolbar->setViewportWidth(width());
            m_toolbar->setPositionForSelection(selectionRect, height());
            m_toolbar->draw(painter);

            // Use unified color and width widget
            if (shouldShowColorAndWidthWidget()) {
                m_colorAndWidthWidget->setVisible(true);
                m_colorAndWidthWidget->setShowColorSection(shouldShowColorPalette());
                bool isMosaicTool = (m_currentTool == ToolbarButton::Mosaic);
                // All width-enabled tools use shared WidthSection (including Mosaic)
                m_colorAndWidthWidget->setShowWidthSection(shouldShowWidthControl());
                m_colorAndWidthWidget->setShowMosaicWidthSection(false);  // No longer used
                m_colorAndWidthWidget->setWidthSectionHidden(false);
                // Show arrow style section only for Arrow tool
                m_colorAndWidthWidget->setShowArrowStyleSection(m_currentTool == ToolbarButton::Arrow);
                // Show line style section for Pencil and Arrow tools
                bool showLineStyle = (m_currentTool == ToolbarButton::Pencil ||
                                      m_currentTool == ToolbarButton::Arrow);
                m_colorAndWidthWidget->setShowLineStyleSection(showLineStyle);
                // Show text section only for Text tool
                m_colorAndWidthWidget->setShowTextSection(m_currentTool == ToolbarButton::Text);
                // Show shape section only for Shape tool
                m_colorAndWidthWidget->setShowShapeSection(m_currentTool == ToolbarButton::Shape);
                // Show auto blur section and blur type section only for Mosaic tool
                m_colorAndWidthWidget->setShowAutoBlurSection(isMosaicTool);
                m_colorAndWidthWidget->setShowMosaicBlurTypeSection(isMosaicTool);
                if (isMosaicTool) {
                    bool autoBlurAvailable = m_autoBlurManager && m_autoBlurManager->isInitialized();
                    m_colorAndWidthWidget->setAutoBlurEnabled(autoBlurAvailable);
                }
                m_colorAndWidthWidget->updatePosition(m_toolbar->boundingRect(), false, width());
                m_colorAndWidthWidget->draw(painter);
            }
            else {
                m_colorAndWidthWidget->setVisible(false);
            }

            // Legacy widgets (keep for compatibility, but hidden when unified widget is shown)
            if (!shouldShowColorAndWidthWidget()) {
                if (shouldShowColorPalette()) {
                    m_colorPalette->setVisible(true);
                    m_colorPalette->updatePosition(m_toolbar->boundingRect(), false);
                    m_colorPalette->draw(painter);
                }
                else {
                    m_colorPalette->setVisible(false);
                }
            }

            m_toolbar->drawTooltip(painter);

            // Draw loading spinner when OCR is in progress
            if (m_ocrInProgress) {
                QPoint center = selectionRect.center();
                m_loadingSpinner->draw(painter, center);
            }
        }
    }

    // Draw crosshair at cursor - show during selection process and inside selection when complete
    // Hide magnifier when any annotation tool is selected or when drawing
    bool shouldShowCrosshair = !m_isDrawing && !isAnnotationTool(m_currentTool);
    if (m_selectionManager->isComplete()) {
        // Only show crosshair inside selection area, not on toolbar
        shouldShowCrosshair = shouldShowCrosshair &&
            selectionRect.contains(m_currentPoint) &&
            !m_toolbar->contains(m_currentPoint);
    }

    if (shouldShowCrosshair) {
        drawCrosshair(painter);
        drawMagnifier(painter);
    }
}

void RegionSelector::drawCrosshair(QPainter& painter)
{
    // 深藍色實線
    QPen pen(QColor(65, 105, 225), 1, Qt::SolidLine);
    painter.setPen(pen);

    // 水平線和垂直線延伸到邊緣
    painter.drawLine(0, m_currentPoint.y(), width(), m_currentPoint.y());
    painter.drawLine(m_currentPoint.x(), 0, m_currentPoint.x(), height());

    // 中心小方框 (8x8 像素)
    int boxSize = 8;
    QRect centerBox(m_currentPoint.x() - boxSize / 2, m_currentPoint.y() - boxSize / 2, boxSize, boxSize);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(centerBox);
}

void RegionSelector::drawMagnifier(QPainter& painter)
{
    // Delegate to MagnifierPanel component
    m_magnifierPanel->draw(painter, m_currentPoint, size(), getBackgroundImage());
}

QCursor RegionSelector::getMosaicCursor(int width)
{
    if (width != m_mosaicCursorCacheWidth) {
        m_mosaicCursorCache = createMosaicCursor(width);
        m_mosaicCursorCacheWidth = width;
    }
    return m_mosaicCursorCache;
}

void RegionSelector::setToolCursor()
{
    switch (m_currentTool) {
    case ToolbarButton::Text:
        if (!m_textEditor->isEditing()) {
            setCursor(Qt::IBeamCursor);
        }
        break;
    case ToolbarButton::Mosaic: {
        int mosaicWidth = m_toolManager ? m_toolManager->width() : MosaicToolHandler::kDefaultBrushWidth;
        setCursor(getMosaicCursor(mosaicWidth));
        break;
    }
    case ToolbarButton::Eraser:
        if (auto* eraser = dynamic_cast<EraserToolHandler*>(m_toolManager->currentHandler())) {
            setCursor(eraser->cursor());
        }
        break;
    case ToolbarButton::Selection:
        // Selection tool uses updateCursorForHandle separately
        break;
    default:
        if (isAnnotationTool(m_currentTool)) {
            setCursor(Qt::CrossCursor);
        }
        break;
    }
}

void RegionSelector::handleToolbarClick(ToolbarButton button)
{
    // Sync current state to handler
    m_toolbarHandler->setCurrentTool(m_currentTool);
    m_toolbarHandler->setShowSubToolbar(m_showSubToolbar);
    m_toolbarHandler->setEraserWidth(m_eraserWidth);
    m_toolbarHandler->setAnnotationWidth(m_annotationWidth);
    m_toolbarHandler->setAnnotationColor(m_annotationColor);
    m_toolbarHandler->setStepBadgeSize(m_stepBadgeSize);
    m_toolbarHandler->setMosaicBlurType(static_cast<int>(m_mosaicBlurType));
    m_toolbarHandler->setOCRInProgress(m_ocrInProgress);

    // Delegate to handler
    m_toolbarHandler->handleToolbarClick(button);
}

QPixmap RegionSelector::getSelectedRegion()
{
    QRect sel = m_selectionManager->selectionRect();
    int radius = effectiveCornerRadius();

    // 使用 device pixels 座標來裁切
    QPixmap selectedRegion = m_backgroundPixmap.copy(
        static_cast<int>(sel.x() * m_devicePixelRatio),
        static_cast<int>(sel.y() * m_devicePixelRatio),
        static_cast<int>(sel.width() * m_devicePixelRatio),
        static_cast<int>(sel.height() * m_devicePixelRatio)
    );

    // KEY FIX: Set DPR BEFORE painting so Qt handles scaling automatically
    // This ensures annotation sizes (radius, pen width, font) stay in logical units
    // while positions are automatically converted to device pixels by Qt
    selectedRegion.setDevicePixelRatio(m_devicePixelRatio);

    // Draw annotations onto the selected region
    if (!m_annotationLayer->isEmpty()) {
        QPainter painter(&selectedRegion);
        painter.setRenderHint(QPainter::Antialiasing, true);

        // Only translate - no manual scale() needed
        // Qt automatically handles DPR conversion because we set devicePixelRatio above
        // Annotation at screen position P will appear at (P - sel.topLeft) in logical coords
        painter.translate(-sel.x(), -sel.y());

        qDebug() << "Drawing annotations: sel=" << sel << "DPR=" << m_devicePixelRatio
            << "pixmap=" << selectedRegion.size()
            << "transform=" << painter.transform();
        m_annotationLayer->draw(painter);
    }

    // Apply rounded corners mask if radius > 0
    if (radius > 0) {
        // Use an explicit alpha mask to guarantee transparent corners across platforms.
        QImage sourceImage = selectedRegion.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        sourceImage.setDevicePixelRatio(m_devicePixelRatio);

        QImage alphaMask(sourceImage.size(), QImage::Format_ARGB32_Premultiplied);
        alphaMask.setDevicePixelRatio(m_devicePixelRatio);
        alphaMask.fill(Qt::transparent);

        QPainter maskPainter(&alphaMask);
        maskPainter.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath clipPath;
        clipPath.addRoundedRect(QRectF(0, 0, sel.width(), sel.height()), radius, radius);
        maskPainter.fillPath(clipPath, Qt::white);
        maskPainter.end();

        QPainter imagePainter(&sourceImage);
        imagePainter.setRenderHint(QPainter::Antialiasing, true);
        imagePainter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        imagePainter.drawImage(0, 0, alphaMask);
        imagePainter.end();

        QPixmap maskedPixmap = QPixmap::fromImage(sourceImage, Qt::NoOpaqueDetection);
        maskedPixmap.setDevicePixelRatio(m_devicePixelRatio);
        return maskedPixmap;
    }

    return selectedRegion;
}

void RegionSelector::copyToClipboard()
{
    QPixmap selectedRegion = getSelectedRegion();
    QGuiApplication::clipboard()->setPixmap(selectedRegion);
    qDebug() << "RegionSelector: Copied to clipboard";

    emit copyRequested(selectedRegion);
    close();
}

void RegionSelector::saveToFile()
{
    QPixmap selectedRegion = getSelectedRegion();

    auto& fileSettings = FileSettingsManager::instance();
    QString savePath = fileSettings.loadScreenshotPath();
    QString dateFormat = fileSettings.loadDateFormat();
    QString prefix = fileSettings.loadFilenamePrefix();
    QString timestamp = QDateTime::currentDateTime().toString(dateFormat);

    QString filename;
    if (prefix.isEmpty()) {
        filename = QString("Screenshot_%1.png").arg(timestamp);
    } else {
        filename = QString("%1_Screenshot_%2.png").arg(prefix).arg(timestamp);
    }
    QString defaultName = QDir(savePath).filePath(filename);

    // Hide fullscreen window so save dialog is visible
    m_isDialogOpen = true;
    hide();

    QString filePath = QFileDialog::getSaveFileName(
        nullptr,
        "Save Screenshot",
        defaultName,
        "PNG Image (*.png);;JPEG Image (*.jpg *.jpeg);;All Files (*)"
    );

    if (!filePath.isEmpty()) {
        // User selected a file -> save and close
        if (selectedRegion.save(filePath)) {
            qDebug() << "RegionSelector: Saved to" << filePath;
        }
        else {
            qDebug() << "RegionSelector: Failed to save to" << filePath;
        }
        emit saveRequested(selectedRegion);
        m_isDialogOpen = false;
        close();
    }
    else {
        // User cancelled -> restore display
        m_isDialogOpen = false;
        show();
        activateWindow();
        raise();
    }
}

void RegionSelector::finishSelection()
{
    QPixmap selectedRegion = getSelectedRegion();
    QRect sel = m_selectionManager->selectionRect();

    qDebug() << "finishSelection: selectionRect=" << sel
        << "globalPos=" << localToGlobal(sel.topLeft());

    emit regionSelected(selectedRegion, localToGlobal(sel.topLeft()));
    close();
}

void RegionSelector::mousePressEvent(QMouseEvent* event)
{
    // Sync state to input handler
    m_inputHandler->setCurrentTool(m_currentTool);
    m_inputHandler->setShowSubToolbar(m_showSubToolbar);
    m_inputHandler->setHighlightedWindowRect(m_highlightedWindowRect);
    m_inputHandler->setDetectedWindow(m_detectedWindow.has_value(),
        m_detectedWindow.has_value() ? m_detectedWindow->bounds : QRect());
    m_inputHandler->setAnnotationColor(m_annotationColor);
    m_inputHandler->setAnnotationWidth(m_annotationWidth);
    m_inputHandler->setArrowStyle(static_cast<int>(m_arrowStyle));
    m_inputHandler->setLineStyle(static_cast<int>(m_lineStyle));
    m_inputHandler->setShapeType(static_cast<int>(m_shapeType));
    m_inputHandler->setShapeFillMode(static_cast<int>(m_shapeFillMode));

    // Delegate to input handler
    m_inputHandler->handleMousePress(event);

    // Sync state back from handler
    m_startPoint = m_inputHandler->startPoint();
    m_currentPoint = m_inputHandler->currentPoint();
    m_isDrawing = m_inputHandler->isDrawing();
    m_lastMagnifierRect = m_inputHandler->lastMagnifierRect();
}

void RegionSelector::mouseMoveEvent(QMouseEvent* event)
{
    // Sync state to input handler
    m_inputHandler->setCurrentTool(m_currentTool);
    m_inputHandler->setShowSubToolbar(m_showSubToolbar);
    m_inputHandler->setHighlightedWindowRect(m_highlightedWindowRect);
    m_inputHandler->setDetectedWindow(m_detectedWindow.has_value(),
        m_detectedWindow.has_value() ? m_detectedWindow->bounds : QRect());

    // Delegate to input handler
    m_inputHandler->handleMouseMove(event);

    // Sync state back from handler
    m_currentPoint = m_inputHandler->currentPoint();
    m_isDrawing = m_inputHandler->isDrawing();
    m_lastMagnifierRect = m_inputHandler->lastMagnifierRect();
}

void RegionSelector::mouseReleaseEvent(QMouseEvent* event)
{
    // Sync state to input handler
    m_inputHandler->setCurrentTool(m_currentTool);
    m_inputHandler->setShowSubToolbar(m_showSubToolbar);
    m_inputHandler->setHighlightedWindowRect(m_highlightedWindowRect);
    m_inputHandler->setDetectedWindow(m_detectedWindow.has_value(),
        m_detectedWindow.has_value() ? m_detectedWindow->bounds : QRect());

    // Delegate to input handler
    m_inputHandler->handleMouseRelease(event);

    // Sync state back from handler
    m_currentPoint = m_inputHandler->currentPoint();
    m_isDrawing = m_inputHandler->isDrawing();
}

void RegionSelector::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        return;
    }

    // Forward double-click to tool manager if selection is complete
    if (m_selectionManager->isComplete()) {
        m_toolManager->handleDoubleClick(event->pos());
        update();
    }
}

void RegionSelector::wheelEvent(QWheelEvent* event)
{
    // Handle scroll wheel for StepBadge size adjustment
    if (m_currentTool == ToolbarButton::StepBadge) {
        int delta = event->angleDelta().y();
        if (delta != 0) {
            // Cycle through sizes: Small -> Medium -> Large -> Small
            int current = static_cast<int>(m_stepBadgeSize);
            if (delta > 0) {
                current = (current + 1) % 3;  // Scroll up = increase size
            } else {
                current = (current + 2) % 3;  // Scroll down = decrease size
            }
            StepBadgeSize newSize = static_cast<StepBadgeSize>(current);
            onStepBadgeSizeChanged(newSize);
            m_colorAndWidthWidget->setStepBadgeSize(newSize);
            event->accept();
            return;
        }
    }

    // Handle scroll wheel for Eraser width adjustment (widget is hidden but wheel still works)
    if (m_currentTool == ToolbarButton::Eraser) {
        int delta = event->angleDelta().y();
        if (delta != 0) {
            int step = (delta > 0) ? 5 : -5;
            int newWidth = qBound(5, m_eraserWidth + step, 100);
            if (newWidth != m_eraserWidth) {
                m_eraserWidth = newWidth;
                m_toolManager->setWidth(m_eraserWidth);
                update();
            }
            event->accept();
            return;
        }
    }

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

void RegionSelector::keyPressEvent(QKeyEvent* event)
{
    // Handle inline text editing keys first
    if (m_textEditor->isEditing()) {
        if (event->key() == Qt::Key_Escape) {
            m_textEditor->cancelEditing();
            return;
        }
        if (m_textEditor->isConfirmMode()) {
            // In confirm mode: Enter finishes editing
            if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
                m_textEditor->finishEditing();
                return;
            }
        }
        else {
            // In typing mode: Ctrl+Enter or Shift+Enter enters confirm mode
            if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
                (event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
                if (!m_textEditor->textEdit()->toPlainText().trimmed().isEmpty()) {
                    m_textEditor->enterConfirmMode();
                }
                return;
            }
            // Let QTextEdit handle other keys (including Enter for newlines)
        }
        return;
    }

    if (event->key() == Qt::Key_Escape) {
        // ESC 直接離開 capture mode
        qDebug() << "RegionSelector: Cancelled via Escape";
        emit selectionCancelled();
        close();
    }
    else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (m_selectionManager->isComplete()) {
            finishSelection();
        }
    }
    else if (event->matches(QKeySequence::Copy)) {
        if (m_selectionManager->isComplete()) {
            copyToClipboard();
        }
    }
    else if (event->matches(QKeySequence::Save)) {
        if (m_selectionManager->isComplete()) {
            saveToFile();
        }
    }
    else if (event->key() == Qt::Key_R && !event->modifiers()) {
        if (m_selectionManager->isComplete()) {
            handleToolbarClick(ToolbarButton::Record);
        }
    }
#ifdef SNAPTRAY_ENABLE_DEV_FEATURES
    else if (event->key() == Qt::Key_S && !event->modifiers()) {
        if (m_selectionManager->isComplete()) {
            handleToolbarClick(ToolbarButton::ScrollCapture);
        }
    }
#endif
    else if (event->key() == Qt::Key_Shift && !m_selectionManager->isComplete()) {
        // Switch RGB/HEX color format display (only when magnifier is shown)
        m_magnifierPanel->toggleColorFormat();
        update();
    }
    else if (event->key() == Qt::Key_C && !m_selectionManager->isComplete()) {
        // Copy color to clipboard (only when selection not complete)
        // Use the color already calculated by MagnifierPanel
        QString colorText = m_magnifierPanel->colorString();
        // Adjust format for clipboard (use parentheses for RGB)
        if (!m_magnifierPanel->showHexColor()) {
            QColor c = m_magnifierPanel->currentColor();
            colorText = QString("rgb(%1, %2, %3)")
                .arg(c.red())
                .arg(c.green())
                .arg(c.blue());
        }
        QGuiApplication::clipboard()->setText(colorText);
        qDebug() << "RegionSelector: Copied color to clipboard:" << colorText;
    }
    else if (event->matches(QKeySequence::Undo)) {
        if (m_selectionManager->isComplete() && m_annotationLayer->canUndo()) {
            m_annotationLayer->undo();
            update();
        }
    }
    else if (event->matches(QKeySequence::Redo)) {
        if (m_selectionManager->isComplete() && m_annotationLayer->canRedo()) {
            m_annotationLayer->redo();
            update();
        }
    }
    else if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        // Delete selected text annotation
        if (m_selectionManager->isComplete() && m_annotationLayer->selectedIndex() >= 0) {
            m_annotationLayer->removeSelectedItem();
            update();
        }
    }
    // Arrow keys for precise selection adjustment
    else if (m_selectionManager->isComplete() && !m_selectionManager->selectionRect().isEmpty()) {
        bool handled = true;
        QRect sel = m_selectionManager->selectionRect();

        if (event->modifiers() & Qt::ShiftModifier) {
            // Shift + Arrow: Resize selection (adjust corresponding edge)
            QRect newRect = sel;
            switch (event->key()) {
            case Qt::Key_Left:  newRect.setRight(newRect.right() - 1); break;
            case Qt::Key_Right: newRect.setRight(newRect.right() + 1); break;
            case Qt::Key_Up:    newRect.setBottom(newRect.bottom() - 1); break;
            case Qt::Key_Down:  newRect.setBottom(newRect.bottom() + 1); break;
            default: handled = false; break;
            }
            if (handled && newRect.width() >= 10 && newRect.height() >= 10) {
                m_selectionManager->setSelectionRect(newRect);
                update();
            }
        }
        else {
            // Arrow only: Move selection
            QRect newRect = sel;
            switch (event->key()) {
            case Qt::Key_Left:  newRect.translate(-1, 0); break;
            case Qt::Key_Right: newRect.translate(1, 0);  break;
            case Qt::Key_Up:    newRect.translate(0, -1); break;
            case Qt::Key_Down:  newRect.translate(0, 1);  break;
            default: handled = false; break;
            }
            if (handled) {
                // Clamp to screen bounds
                QRect bounds = m_selectionManager->bounds();
                if (!bounds.isEmpty()) {
                    if (newRect.left() < bounds.left())
                        newRect.moveLeft(bounds.left());
                    if (newRect.top() < bounds.top())
                        newRect.moveTop(bounds.top());
                    if (newRect.right() > bounds.right())
                        newRect.moveRight(bounds.right());
                    if (newRect.bottom() > bounds.bottom())
                        newRect.moveBottom(bounds.bottom());
                }
                m_selectionManager->setSelectionRect(newRect);
                update();
            }
        }
    }
}

void RegionSelector::closeEvent(QCloseEvent* event)
{
    m_isClosing = true;
    QWidget::closeEvent(event);
}

bool RegionSelector::eventFilter(QObject* obj, QEvent* event)
{
    if (m_isClosing) {
        return QWidget::eventFilter(obj, event);
    }

    // Capture ESC key at application level to handle when window loses focus
    // (e.g., user clicks on macOS menu bar)
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            // If editing text, let keyPressEvent handle Esc
            if (m_textEditor->isEditing()) {
                return false;
            }
            qDebug() << "RegionSelector: Cancelled via Escape (event filter)";
            emit selectionCancelled();
            close();
            return true;  // Event handled
        }
    }
    else if (event->type() == QEvent::ApplicationDeactivate) {
        // Don't cancel if a dialog is open (e.g., save dialog)
        if (m_isDialogOpen) {
            return false;
        }
        // Startup protection: ignore early deactivation (within 300ms of creation)
        // This prevents false cancellation when popup menus close during startup
        if (m_createdAt.elapsed() < 300) {
            qDebug() << "RegionSelector: Ignoring early ApplicationDeactivate (elapsed:"
                << m_createdAt.elapsed() << "ms)";
            return false;
        }
        qDebug() << "RegionSelector: Cancelled due to app deactivation";
        emit selectionCancelled();
        close();
        return false;
    }
    return QWidget::eventFilter(obj, event);
}

// ============================================================================
// Annotation Helper Functions
// ============================================================================

bool RegionSelector::isAnnotationTool(ToolbarButton tool) const
{
    switch (tool) {
    case ToolbarButton::Pencil:
    case ToolbarButton::Marker:
    case ToolbarButton::Arrow:
    case ToolbarButton::Shape:
    case ToolbarButton::Text:
    case ToolbarButton::Mosaic:
    case ToolbarButton::StepBadge:
    case ToolbarButton::Eraser:
        return true;
    default:
        return false;
    }
}

void RegionSelector::showTextInputDialog(const QPoint& pos)
{
    bool ok;
    QString text = QInputDialog::getText(this, "Add Text", "Enter text:",
        QLineEdit::Normal, QString(), &ok);
    if (ok && !text.isEmpty()) {
        QFont font;
        font.setPointSize(16);
        font.setBold(true);

        auto textAnnotation = std::make_unique<TextAnnotation>(pos, text, font, m_annotationColor);
        m_annotationLayer->addItem(std::move(textAnnotation));
        update();
    }
}

void RegionSelector::onTextEditingFinished(const QString& text, const QPoint& position)
{
    m_textAnnotationEditor->finishEditing(text, position, m_annotationColor);
}

void RegionSelector::startTextReEditing(int annotationIndex)
{
    m_textAnnotationEditor->startReEditing(annotationIndex, m_annotationColor);
    // Update local annotation color from the text item
    auto* textItem = dynamic_cast<TextAnnotation*>(m_annotationLayer->itemAt(annotationIndex));
    if (textItem) {
        m_annotationColor = textItem->color();
        m_colorAndWidthWidget->setCurrentColor(m_annotationColor);
    }
}

void RegionSelector::performOCR()
{
    if (!m_ocrManager || m_ocrInProgress || !m_selectionManager->isComplete()) {
        if (!m_ocrManager) {
            qDebug() << "RegionSelector: OCR not available on this platform";
        }
        return;
    }

    m_ocrInProgress = true;
    m_loadingSpinner->start();
    update();

    qDebug() << "RegionSelector: Starting OCR recognition...";

    // Get the selected region (without annotations for OCR)
    QRect sel = m_selectionManager->selectionRect();
    QPixmap selectedRegion = m_backgroundPixmap.copy(
        static_cast<int>(sel.x() * m_devicePixelRatio),
        static_cast<int>(sel.y() * m_devicePixelRatio),
        static_cast<int>(sel.width() * m_devicePixelRatio),
        static_cast<int>(sel.height() * m_devicePixelRatio)
    );
    selectedRegion.setDevicePixelRatio(m_devicePixelRatio);

    QPointer<RegionSelector> safeThis = this;
    m_ocrManager->recognizeText(selectedRegion,
        [safeThis](bool success, const QString& text, const QString& error) {
            if (safeThis) {
                safeThis->onOCRComplete(success, text, error);
            }
            else {
                qDebug() << "RegionSelector: OCR completed but widget was destroyed, ignoring result";
            }
        });
}

void RegionSelector::onOCRComplete(bool success, const QString& text, const QString& error)
{
    m_ocrInProgress = false;
    m_loadingSpinner->stop();

    QString msg;
    QString bgColor;
    if (success && !text.isEmpty()) {
        QGuiApplication::clipboard()->setText(text);
        qDebug() << "RegionSelector: OCR complete, copied" << text.length() << "characters to clipboard";
        msg = tr("Copied %1 characters").arg(text.length());
        bgColor = "rgba(34, 139, 34, 220)";  // Green for success
    }
    else {
        msg = error.isEmpty() ? tr("No text found") : error;
        qDebug() << "RegionSelector: OCR failed:" << msg;
        bgColor = "rgba(200, 60, 60, 220)";  // Red for failure
    }

    m_ocrToastLabel->setStyleSheet(QString(
        "QLabel {"
        "  background-color: %1;"
        "  color: white;"
        "  padding: 8px 16px;"
        "  border-radius: 6px;"
        "  font-size: 13px;"
        "  font-weight: bold;"
        "}"
    ).arg(bgColor));

    // Display the toast centered at top of selection area
    m_ocrToastLabel->setText(msg);
    m_ocrToastLabel->adjustSize();
    QRect sel = m_selectionManager->selectionRect();
    int x = sel.center().x() - m_ocrToastLabel->width() / 2;
    int y = sel.top() + 12;
    m_ocrToastLabel->move(x, y);
    m_ocrToastLabel->show();
    m_ocrToastLabel->raise();
    m_ocrToastTimer->start(2500);

    update();
}

void RegionSelector::performAutoBlur()
{
    if (!m_autoBlurManager || !m_autoBlurManager->isInitialized() ||
        m_autoBlurInProgress || !m_selectionManager->isComplete()) {
        if (!m_autoBlurManager || !m_autoBlurManager->isInitialized()) {
            qDebug() << "RegionSelector: Auto-blur not available";
        }
        return;
    }

    m_autoBlurInProgress = true;
    m_colorAndWidthWidget->setAutoBlurProcessing(true);
    m_loadingSpinner->start();
    update();

    // Get the selected region as QImage
    QRect sel = m_selectionManager->selectionRect();

    qDebug() << "RegionSelector: Starting auto-blur detection..."
             << "selection=" << sel
             << "dpr=" << m_devicePixelRatio
             << "bgPixmap size=" << m_backgroundPixmap.size()
             << "bgPixmap dpr=" << m_backgroundPixmap.devicePixelRatio();

    QPixmap selectedPixmap = m_backgroundPixmap.copy(
        static_cast<int>(sel.x() * m_devicePixelRatio),
        static_cast<int>(sel.y() * m_devicePixelRatio),
        static_cast<int>(sel.width() * m_devicePixelRatio),
        static_cast<int>(sel.height() * m_devicePixelRatio)
    );
    QImage selectedImage = selectedPixmap.toImage();

    qDebug() << "RegionSelector: Cropped image for detection:"
             << "size=" << selectedImage.size();

    // Run detection
    auto result = m_autoBlurManager->detect(selectedImage);

    if (result.success) {
        int faceCount = result.faceRegions.size();

        // Create mosaic annotations for detected faces (skip text regions)
        if (faceCount > 0) {
            // Detection coordinates are in device pixels (relative to the cropped region).
            // Convert to logical coordinates for annotations.
            for (int i = 0; i < result.faceRegions.size(); ++i) {
                const QRect& r = result.faceRegions[i];

                qDebug() << "RegionSelector: Face" << i
                         << "detection rect (device px, relative to crop)=" << r;

                // Convert from device pixels to logical coordinates
                QRect logicalRect(
                    sel.x() + static_cast<int>(r.x() / m_devicePixelRatio),
                    sel.y() + static_cast<int>(r.y() / m_devicePixelRatio),
                    static_cast<int>(r.width() / m_devicePixelRatio),
                    static_cast<int>(r.height() / m_devicePixelRatio)
                );

                qDebug() << "RegionSelector: Face" << i
                         << "logical rect (for annotation)=" << logicalRect;

                // Create mosaic annotation for this face region
                // Use current blur type setting (convert from MosaicBlurTypeSection to MosaicRectAnnotation enum)
                auto blurType = static_cast<MosaicRectAnnotation::BlurType>(
                    static_cast<int>(m_colorAndWidthWidget->mosaicBlurType())
                );
                auto mosaic = std::make_unique<MosaicRectAnnotation>(
                    logicalRect, m_backgroundPixmap, 12, blurType
                );
                m_annotationLayer->addItem(std::move(mosaic));
            }
        }

        onAutoBlurComplete(true, faceCount, 0, QString());
    } else {
        onAutoBlurComplete(false, 0, 0, result.errorMessage);
    }
}

void RegionSelector::onAutoBlurComplete(bool success, int faceCount, int /*textCount*/, const QString& error)
{
    m_autoBlurInProgress = false;
    m_colorAndWidthWidget->setAutoBlurProcessing(false);
    m_loadingSpinner->stop();

    QString msg;
    QString bgColor;
    if (success && faceCount > 0) {
        qDebug() << "RegionSelector: Auto-blur complete, blurred" << faceCount << "faces";
        msg = tr("Blurred %1 face(s)").arg(faceCount);
        bgColor = "rgba(34, 139, 34, 220)";  // Green for success
    }
    else if (success) {
        msg = tr("No faces detected");
        qDebug() << "RegionSelector: Auto-blur found nothing to blur";
        bgColor = "rgba(100, 100, 100, 220)";  // Gray for nothing found
    }
    else {
        msg = error.isEmpty() ? tr("Detection failed") : error;
        qDebug() << "RegionSelector: Auto-blur failed:" << msg;
        bgColor = "rgba(200, 60, 60, 220)";  // Red for failure
    }

    m_ocrToastLabel->setStyleSheet(QString(
        "QLabel {"
        "  background-color: %1;"
        "  color: white;"
        "  padding: 8px 16px;"
        "  border-radius: 6px;"
        "  font-size: 13px;"
        "  font-weight: bold;"
        "}"
    ).arg(bgColor));

    // Display the toast centered at top of selection area
    m_ocrToastLabel->setText(msg);
    m_ocrToastLabel->adjustSize();
    QRect sel = m_selectionManager->selectionRect();
    int x = sel.center().x() - m_ocrToastLabel->width() / 2;
    int y = sel.top() + 12;
    m_ocrToastLabel->move(x, y);
    m_ocrToastLabel->show();
    m_ocrToastLabel->raise();
    m_ocrToastTimer->start(2500);

    update();
}

QPoint RegionSelector::localToGlobal(const QPoint& localPos) const
{
    return m_currentScreen->geometry().topLeft() + localPos;
}

QPoint RegionSelector::globalToLocal(const QPoint& globalPos) const
{
    return globalPos - m_currentScreen->geometry().topLeft();
}

QRect RegionSelector::localToGlobal(const QRect& localRect) const
{
    return localRect.translated(m_currentScreen->geometry().topLeft());
}

QRect RegionSelector::globalToLocal(const QRect& globalRect) const
{
    return globalRect.translated(-m_currentScreen->geometry().topLeft());
}

void RegionSelector::onArrowStyleChanged(LineEndStyle style)
{
    m_arrowStyle = style;
    m_toolManager->setArrowStyle(style);
    RegionSettingsHelper::saveArrowStyle(style);
    update();
}

void RegionSelector::onLineStyleChanged(LineStyle style)
{
    m_lineStyle = style;
    m_toolManager->setLineStyle(style);
    RegionSettingsHelper::saveLineStyle(style);
    update();
}

void RegionSelector::onFontSizeDropdownRequested(const QPoint& pos)
{
    TextFormattingState formatting = m_textAnnotationEditor->formatting();
    m_settingsHelper->showFontSizeDropdown(pos, formatting.fontSize);
}

void RegionSelector::onFontFamilyDropdownRequested(const QPoint& pos)
{
    TextFormattingState formatting = m_textAnnotationEditor->formatting();
    m_settingsHelper->showFontFamilyDropdown(pos, formatting.fontFamily);
}

void RegionSelector::onFontSizeSelected(int size)
{
    m_textAnnotationEditor->setFontSize(size);
}

void RegionSelector::onFontFamilySelected(const QString& family)
{
    m_textAnnotationEditor->setFontFamily(family);
}
