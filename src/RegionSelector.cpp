#include "RegionSelector.h"
#include "annotations/AllAnnotations.h"
#include "IconRenderer.h"
#include "ColorPaletteWidget.h"
#include "LineWidthWidget.h"
#include "ColorAndWidthWidget.h"
#include "ColorPickerDialog.h"
#include "OCRManager.h"
#include "PlatformFeatures.h"
#include "settings/AnnotationSettingsManager.h"
#include "tools/handlers/EraserToolHandler.h"
#include "tools/handlers/MosaicToolHandler.h"
#include <QTextEdit>

#include <cstring>
#include <map>
#include <QPainter>
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

static const char* SETTINGS_KEY_TEXT_BOLD = "textBold";
static const char* SETTINGS_KEY_TEXT_ITALIC = "textItalic";
static const char* SETTINGS_KEY_TEXT_UNDERLINE = "textUnderline";
static const char* SETTINGS_KEY_TEXT_SIZE = "textFontSize";
static const char* SETTINGS_KEY_TEXT_FAMILY = "textFontFamily";

// Dropdown menu stylesheet (shared by font size and font family menus)
static const char* kDropdownMenuStyle =
    "QMenu { background: #2d2d2d; color: white; border: 1px solid #3d3d3d; } "
    "QMenu::item { padding: 4px 20px; } "
    "QMenu::item:selected { background: #0078d4; }";

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
    {ToolbarButton::Eraser,     {false, true,  true}},
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
    , m_annotationColor(Qt::red)  // Will be overwritten by loadAnnotationColor()
    , m_annotationWidth(3)        // Will be overwritten by loadAnnotationWidth()
    , m_isDrawing(false)
    , m_isClosing(false)
    , m_isDialogOpen(false)
    , m_windowDetector(nullptr)
    , m_ocrManager(nullptr)
    , m_ocrInProgress(false)
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
    m_arrowStyle = loadArrowStyle();
    m_lineStyle = loadLineStyle();
    m_stepBadgeSize = settings.loadStepBadgeSize();

    // Initialize tool manager
    m_toolManager = new ToolManager(this);
    m_toolManager->registerDefaultHandlers();
    m_toolManager->setAnnotationLayer(m_annotationLayer);
    m_toolManager->setColor(m_annotationColor);
    m_toolManager->setWidth(m_annotationWidth);
    m_toolManager->setArrowStyle(m_arrowStyle);
    m_toolManager->setLineStyle(m_lineStyle);
    m_toolManager->setPolylineMode(settings.loadPolylineMode());
    connect(m_toolManager, &ToolManager::needsRepaint, this, QOverload<>::of(&QWidget::update));

    // Initialize OCR manager if available on this platform
    m_ocrManager = PlatformFeatures::instance().createOCRManager(this);

    // Initialize toolbar widget
    m_toolbar = new ToolbarWidget(this);
    setupToolbarButtons();
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

    // Initialize line width widget
    m_lineWidthWidget = new LineWidthWidget(this);
    m_lineWidthWidget->setWidthRange(1, 20);
    m_lineWidthWidget->setCurrentWidth(m_annotationWidth);
    m_lineWidthWidget->setPreviewColor(m_annotationColor);
    connect(m_lineWidthWidget, &LineWidthWidget::widthChanged,
        this, &RegionSelector::onLineWidthChanged);

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

    // Connect polyline mode signal
    m_colorAndWidthWidget->setPolylineMode(settings.loadPolylineMode());
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::polylineModeChanged,
        this, [this](bool enabled) {
            m_toolManager->setPolylineMode(enabled);
            AnnotationSettingsManager::instance().savePolylineMode(enabled);
        });

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

void RegionSelector::setupToolbarButtons()
{
    // Load icons
    IconRenderer& iconRenderer = IconRenderer::instance();
    iconRenderer.loadIcon("selection", ":/icons/icons/selection.svg");
    iconRenderer.loadIcon("arrow", ":/icons/icons/arrow.svg");
    iconRenderer.loadIcon("polyline", ":/icons/icons/polyline.svg");
    iconRenderer.loadIcon("pencil", ":/icons/icons/pencil.svg");
    iconRenderer.loadIcon("marker", ":/icons/icons/marker.svg");
    iconRenderer.loadIcon("rectangle", ":/icons/icons/rectangle.svg");
    iconRenderer.loadIcon("ellipse", ":/icons/icons/ellipse.svg");
    iconRenderer.loadIcon("text", ":/icons/icons/text.svg");
    iconRenderer.loadIcon("mosaic", ":/icons/icons/mosaic.svg");
    iconRenderer.loadIcon("step-badge", ":/icons/icons/step-badge.svg");
    iconRenderer.loadIcon("eraser", ":/icons/icons/eraser.svg");
    iconRenderer.loadIcon("undo", ":/icons/icons/undo.svg");
    iconRenderer.loadIcon("redo", ":/icons/icons/redo.svg");
    iconRenderer.loadIcon("cancel", ":/icons/icons/cancel.svg");
    if (PlatformFeatures::instance().isOCRAvailable()) {
        iconRenderer.loadIcon("ocr", ":/icons/icons/ocr.svg");
    }
    iconRenderer.loadIcon("pin", ":/icons/icons/pin.svg");
    iconRenderer.loadIcon("record", ":/icons/icons/record.svg");
    iconRenderer.loadIcon("save", ":/icons/icons/save.svg");
    iconRenderer.loadIcon("copy", ":/icons/icons/copy.svg");

    // Configure buttons
    QVector<ToolbarWidget::ButtonConfig> buttons;
    buttons.append({ static_cast<int>(ToolbarButton::Selection), "selection", "Selection", false });
    buttons.append({ static_cast<int>(ToolbarButton::Arrow), "arrow", "Arrow", false });
    buttons.append({ static_cast<int>(ToolbarButton::Pencil), "pencil", "Pencil", false });
    buttons.append({ static_cast<int>(ToolbarButton::Marker), "marker", "Marker", false });
    buttons.append({ static_cast<int>(ToolbarButton::Shape), "rectangle", "Shape", false });
    buttons.append({ static_cast<int>(ToolbarButton::Text), "text", "Text", false });
    buttons.append({ static_cast<int>(ToolbarButton::Mosaic), "mosaic", "Mosaic", false });
    buttons.append({ static_cast<int>(ToolbarButton::StepBadge), "step-badge", "Step Badge", false });
    buttons.append({ static_cast<int>(ToolbarButton::Eraser), "eraser", "Eraser", false });
    buttons.append({ static_cast<int>(ToolbarButton::Undo), "undo", "Undo", false });
    buttons.append({ static_cast<int>(ToolbarButton::Redo), "redo", "Redo", false });
    buttons.append({ static_cast<int>(ToolbarButton::Cancel), "cancel", "Cancel (Esc)", true });  // separator before
    if (PlatformFeatures::instance().isOCRAvailable()) {
        buttons.append({ static_cast<int>(ToolbarButton::OCR), "ocr", "OCR Text Recognition", false });
    }
    buttons.append({ static_cast<int>(ToolbarButton::Record), "record", "Screen Recording (R)", false });
    buttons.append({ static_cast<int>(ToolbarButton::Pin), "pin", "Pin to Screen (Enter)", false });
    buttons.append({ static_cast<int>(ToolbarButton::Save), "save", "Save (Ctrl+S)", false });
    buttons.append({ static_cast<int>(ToolbarButton::Copy), "copy", "Copy (Ctrl+C)", false });

    m_toolbar->setButtons(buttons);

    // Set which buttons are "active" type (annotation tools that stay highlighted)
    QVector<int> activeButtonIds = {
        static_cast<int>(ToolbarButton::Selection),
        static_cast<int>(ToolbarButton::Arrow),
        static_cast<int>(ToolbarButton::Pencil),
        static_cast<int>(ToolbarButton::Marker),
        static_cast<int>(ToolbarButton::Shape),
        static_cast<int>(ToolbarButton::Text),
        static_cast<int>(ToolbarButton::Mosaic),
        static_cast<int>(ToolbarButton::StepBadge),
        static_cast<int>(ToolbarButton::Eraser)
    };
    m_toolbar->setActiveButtonIds(activeButtonIds);

    // Set icon color provider
    m_toolbar->setIconColorProvider([this](int buttonId, bool isActive, bool isHovered) {
        return getToolbarIconColor(buttonId, isActive, isHovered);
        });
}

QColor RegionSelector::getToolbarIconColor(int buttonId, bool isActive, bool isHovered) const
{
    Q_UNUSED(isHovered);

    const auto& style = m_toolbar->styleConfig();
    ToolbarButton btn = static_cast<ToolbarButton>(buttonId);

    if (buttonId == static_cast<int>(ToolbarButton::Cancel)) {
        return style.iconCancelColor;
    }
    if (btn == ToolbarButton::Record) {
        return style.iconRecordColor;
    }
    if (btn == ToolbarButton::OCR) {
        if (m_ocrInProgress) {
            return QColor(255, 200, 100);  // Yellow when processing
        }
        else if (!m_ocrManager) {
            return QColor(128, 128, 128);  // Gray if unavailable
        }
        else {
            return style.iconActionColor;
        }
    }
    if (buttonId >= static_cast<int>(ToolbarButton::Pin)) {
        return style.iconActionColor;
    }
    if (isActive) {
        return style.iconActiveColor;
    }
    return style.iconNormalColor;
}

bool RegionSelector::shouldShowColorPalette() const
{
    if (!m_selectionManager->isComplete()) return false;
    auto it = kToolCapabilities.find(m_currentTool);
    return it != kToolCapabilities.end() && it->second.showInPalette;
}

void RegionSelector::syncColorToAllWidgets(const QColor& color)
{
    m_annotationColor = color;
    AnnotationSettingsManager::instance().saveColor(color);
    m_toolManager->setColor(color);
    m_lineWidthWidget->setPreviewColor(color);
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

bool RegionSelector::shouldShowLineWidthWidget() const
{
    if (!m_selectionManager->isComplete()) return false;

    // Show for tools that use m_annotationWidth
    return m_currentTool == ToolbarButton::Pencil ||
        m_currentTool == ToolbarButton::Arrow ||
        m_currentTool == ToolbarButton::Shape;
}

void RegionSelector::onLineWidthChanged(int width)
{
    if (m_currentTool == ToolbarButton::Eraser) {
        m_eraserWidth = width;
        // Don't save eraser width to settings
    }
    else if (m_currentTool == ToolbarButton::Mosaic) {
        m_mosaicWidth = width;
        // Update cursor to reflect new width
        setToolCursor();
        // Don't save mosaic width to settings
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

bool RegionSelector::shouldShowColorAndWidthWidget() const
{
    if (!m_selectionManager->isComplete()) return false;
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
    if (m_currentTool == ToolbarButton::Mosaic) {
        return m_mosaicWidth;
    }
    if (m_currentTool == ToolbarButton::Eraser) {
        return m_eraserWidth;
    }
    if (m_currentTool == ToolbarButton::StepBadge) {
        return StepBadgeAnnotation::radiusForSize(m_stepBadgeSize);
    }
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

void RegionSelector::drawDetectedWindow(QPainter& painter)
{
    if (m_highlightedWindowRect.isNull() || m_selectionManager->hasActiveSelection()) {
        return;
    }

    // Dashed border to distinguish from final selection
    QPen dashedPen(QColor(0, 174, 255), 2, Qt::DashLine);
    painter.setPen(dashedPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(m_highlightedWindowRect);

    // Show window dimensions hint
    if (m_detectedWindow.has_value()) {
        QString displayText = QString("%1x%2")
            .arg(m_detectedWindow->bounds.width())
            .arg(m_detectedWindow->bounds.height());
        drawWindowHint(painter, displayText);
    }
}

void RegionSelector::drawWindowHint(QPainter& painter, const QString& title)
{
    QFont font = painter.font();
    font.setPointSize(11);
    painter.setFont(font);

    QFontMetrics fm(font);
    QString displayTitle = fm.elidedText(title, Qt::ElideRight, 200);
    QRect textRect = fm.boundingRect(displayTitle);
    textRect.adjust(-8, -4, 8, 4);

    // For small elements (like menu bar icons), position to the right
    // For larger windows, position below
    bool isSmallElement = m_highlightedWindowRect.width() < 60 || m_highlightedWindowRect.height() < 60;

    int hintX, hintY;

    if (isSmallElement) {
        // Position to the right of the element
        hintX = m_highlightedWindowRect.right() + 4;
        hintY = m_highlightedWindowRect.top();

        // If no space on right, try left
        if (hintX + textRect.width() > width() - 5) {
            hintX = m_highlightedWindowRect.left() - textRect.width() - 4;
        }
        // If no space on left either, position below
        if (hintX < 5) {
            hintX = m_highlightedWindowRect.left();
            hintY = m_highlightedWindowRect.bottom() + 4;
        }
    }
    else {
        // Position below the detected window
        hintX = m_highlightedWindowRect.left();
        hintY = m_highlightedWindowRect.bottom() + 4;

        // If no space below, position above
        if (hintY + textRect.height() > height() - 5) {
            hintY = m_highlightedWindowRect.top() - textRect.height() - 4;
        }
    }

    textRect.moveTo(hintX, hintY);

    // Keep on screen
    if (textRect.right() > width() - 5) {
        textRect.moveRight(width() - 5);
    }
    if (textRect.left() < 5) {
        textRect.moveLeft(5);
    }
    if (textRect.bottom() > height() - 5) {
        textRect.moveBottom(height() - 5);
    }
    if (textRect.top() < 5) {
        textRect.moveTop(5);
    }

    // Draw background pill
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 180));
    painter.drawRoundedRect(textRect, 4, 4);

    // Draw text
    painter.setPen(Qt::white);
    painter.drawText(textRect, Qt::AlignCenter, displayTitle);
}

void RegionSelector::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::Antialiasing);

    // Draw the captured background scaled to fit the widget (logical pixels)
    painter.drawPixmap(rect(), m_backgroundPixmap);

    // Draw dimmed overlay
    drawOverlay(painter);

    // Draw detected window highlight (only during hover, before any selection)
    if (!m_selectionManager->hasActiveSelection() && m_windowDetector) {
        drawDetectedWindow(painter);
    }

    // Draw selection if active or complete
    QRect selectionRect = m_selectionManager->selectionRect();
    if (m_selectionManager->hasActiveSelection() && selectionRect.isValid()) {
        drawSelection(painter);
        drawDimensionInfo(painter);

        // Draw annotations on top of selection (only when selection is established)
        if (m_selectionManager->hasSelection()) {
            drawAnnotations(painter);
            drawCurrentAnnotation(painter);

            // Update toolbar position and draw
            m_toolbar->setActiveButton(static_cast<int>(m_currentTool));
            m_toolbar->setViewportWidth(width());
            m_toolbar->setPositionForSelection(selectionRect, height());
            m_toolbar->draw(painter);

            // Use unified color and width widget
            if (shouldShowColorAndWidthWidget()) {
                m_colorAndWidthWidget->setVisible(true);
                m_colorAndWidthWidget->setShowColorSection(shouldShowColorPalette());
                m_colorAndWidthWidget->setShowWidthSection(shouldShowWidthControl());
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

                // Draw line width widget below color palette (for Pencil tool)
                if (shouldShowLineWidthWidget()) {
                    m_lineWidthWidget->setVisible(true);
                    // Position below color palette if visible, otherwise below toolbar
                    QRect anchorRect = shouldShowColorPalette()
                        ? m_colorPalette->boundingRect()
                        : m_toolbar->boundingRect();
                    m_lineWidthWidget->updatePosition(anchorRect, false, width());
                    m_lineWidthWidget->draw(painter);
                }
                else {
                    m_lineWidthWidget->setVisible(false);
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

void RegionSelector::drawDimmingOverlay(QPainter& painter, const QRect& clearRect, const QColor& dimColor)
{
    painter.fillRect(QRect(0, 0, width(), clearRect.top()), dimColor);                                    // Top
    painter.fillRect(QRect(0, clearRect.bottom() + 1, width(), height() - clearRect.bottom() - 1), dimColor);  // Bottom
    painter.fillRect(QRect(0, clearRect.top(), clearRect.left(), clearRect.height()), dimColor);          // Left
    painter.fillRect(QRect(clearRect.right() + 1, clearRect.top(), width() - clearRect.right() - 1, clearRect.height()), dimColor);  // Right
}

void RegionSelector::drawOverlay(QPainter& painter)
{
    QColor dimColor(0, 0, 0, 100);

    QRect sel = m_selectionManager->selectionRect();
    bool hasSelection = m_selectionManager->hasActiveSelection() && sel.isValid();

    if (hasSelection) {
        drawDimmingOverlay(painter, sel, dimColor);
    }
    else if (!m_highlightedWindowRect.isNull()) {
        drawDimmingOverlay(painter, m_highlightedWindowRect, dimColor);
    }
    else {
        painter.fillRect(rect(), dimColor);
    }
}

void RegionSelector::drawSelection(QPainter& painter)
{
    QRect sel = m_selectionManager->selectionRect();

    // Draw selection border
    painter.setPen(QPen(QColor(0, 174, 255), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(sel);

    // Draw corner and edge handles
    const int handleSize = 8;
    QColor handleColor(0, 174, 255);
    painter.setBrush(handleColor);
    painter.setPen(Qt::white);

    // Corner handles (circles for Snipaste style)
    auto drawHandle = [&](int x, int y) {
        painter.drawEllipse(QPoint(x, y), handleSize / 2, handleSize / 2);
        };

    drawHandle(sel.left(), sel.top());
    drawHandle(sel.right(), sel.top());
    drawHandle(sel.left(), sel.bottom());
    drawHandle(sel.right(), sel.bottom());
    drawHandle(sel.center().x(), sel.top());
    drawHandle(sel.center().x(), sel.bottom());
    drawHandle(sel.left(), sel.center().y());
    drawHandle(sel.right(), sel.center().y());
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

void RegionSelector::drawDimensionInfo(QPainter& painter)
{
    QRect sel = m_selectionManager->selectionRect();

    // Show dimensions with "pt" suffix like Snipaste
    QString dimensions = QString("%1 x %2  pt").arg(sel.width()).arg(sel.height());

    QFont font = painter.font();
    font.setPointSize(12);
    painter.setFont(font);

    QFontMetrics fm(font);
    QRect textRect = fm.boundingRect(dimensions);
    textRect.adjust(-12, -6, 12, 6);

    // Position above selection
    int textX = sel.left();
    int textY = sel.top() - textRect.height() - 8;
    if (textY < 5) {
        textY = sel.top() + 5;
        textX = sel.left() + 5;
    }

    textRect.moveTo(textX, textY);

    // Draw rounded background
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(40, 40, 40, 220));
    painter.drawRoundedRect(textRect, 6, 6);

    // Draw text
    painter.setPen(Qt::white);
    painter.drawText(textRect, Qt::AlignCenter, dimensions);
}

void RegionSelector::saveEraserWidthAndClearHover()
{
    m_eraserWidth = m_colorAndWidthWidget->currentWidth();
    if (auto* eraser = dynamic_cast<EraserToolHandler*>(m_toolManager->currentHandler())) {
        eraser->clearHoverPoint();
    }
}

void RegionSelector::restoreStandardWidthFromEraser()
{
    if (m_currentTool == ToolbarButton::Eraser) {
        m_colorAndWidthWidget->setWidthRange(1, 20);
        m_colorAndWidthWidget->setCurrentWidth(m_annotationWidth);
        m_toolManager->setWidth(m_annotationWidth);
    }
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

Qt::CursorShape RegionSelector::getCursorForGizmoHandle(GizmoHandle handle) const
{
    switch (handle) {
    case GizmoHandle::Rotation:
        return Qt::CrossCursor;
    case GizmoHandle::TopLeft:
    case GizmoHandle::BottomRight:
        return Qt::SizeFDiagCursor;
    case GizmoHandle::TopRight:
    case GizmoHandle::BottomLeft:
        return Qt::SizeBDiagCursor;
    case GizmoHandle::Body:
        return Qt::SizeAllCursor;
    default:
        return Qt::ArrowCursor;
    }
}

void RegionSelector::handleToolbarClick(ToolbarButton button)
{
    // Save eraser width and clear hover when switching FROM Eraser to another tool
    if (m_currentTool == ToolbarButton::Eraser && button != ToolbarButton::Eraser) {
        saveEraserWidthAndClearHover();
    }

    // Restore standard width when switching to annotation tools (except Eraser)
    if (button != ToolbarButton::Eraser && isAnnotationTool(button)) {
        restoreStandardWidthFromEraser();
    }

    switch (button) {
    case ToolbarButton::Selection:
    case ToolbarButton::Arrow:
    case ToolbarButton::Pencil:
    case ToolbarButton::Marker:
    case ToolbarButton::Shape:
    case ToolbarButton::Text:
        m_currentTool = button;
        m_colorAndWidthWidget->setShowSizeSection(false);
        qDebug() << "Tool selected:" << static_cast<int>(button);
        update();
        break;

    case ToolbarButton::StepBadge:
        m_currentTool = button;
        m_toolManager->setCurrentTool(ToolId::StepBadge);
        // Show size section, hide width section for step badge
        m_colorAndWidthWidget->setShowSizeSection(true);
        m_colorAndWidthWidget->setShowWidthSection(false);
        m_colorAndWidthWidget->setStepBadgeSize(m_stepBadgeSize);
        // Set width to radius for tool context
        m_toolManager->setWidth(StepBadgeAnnotation::radiusForSize(m_stepBadgeSize));
        qDebug() << "StepBadge tool selected";
        update();
        break;

    case ToolbarButton::Mosaic:
        m_currentTool = button;
        m_toolManager->setCurrentTool(ToolId::Mosaic);
        // Configure mosaic-specific width range (10-100)
        m_colorAndWidthWidget->setWidthRange(10, 100);
        m_colorAndWidthWidget->setCurrentWidth(m_mosaicWidth);
        m_toolManager->setWidth(m_mosaicWidth);
        m_colorAndWidthWidget->setShowSizeSection(false);
        // Mosaic tool doesn't use color, hide color section
        m_colorAndWidthWidget->setShowColorSection(false);
        qDebug() << "Mosaic tool selected";
        update();
        break;

    case ToolbarButton::Eraser:
        m_currentTool = button;
        m_toolManager->setCurrentTool(ToolId::Eraser);
        // Configure eraser-specific width range (5-100)
        m_colorAndWidthWidget->setWidthRange(5, 100);
        m_colorAndWidthWidget->setCurrentWidth(m_eraserWidth);
        m_toolManager->setWidth(m_eraserWidth);
        m_colorAndWidthWidget->setShowSizeSection(false);
        qDebug() << "Eraser tool selected - drag over annotations to erase them";
        update();
        break;

    case ToolbarButton::Undo:
        if (m_annotationLayer->canUndo()) {
            m_annotationLayer->undo();
            qDebug() << "Undo";
            update();
        }
        break;

    case ToolbarButton::Redo:
        if (m_annotationLayer->canRedo()) {
            m_annotationLayer->redo();
            qDebug() << "Redo";
            update();
        }
        break;

    case ToolbarButton::Cancel:
        emit selectionCancelled();
        close();
        break;

    case ToolbarButton::OCR:
        performOCR();
        break;

    case ToolbarButton::Pin:
        finishSelection();
        break;

    case ToolbarButton::Record:
        emit recordingRequested(localToGlobal(m_selectionManager->selectionRect()), m_currentScreen);
        close();
        break;

    case ToolbarButton::Save:
        saveToFile();
        break;

    case ToolbarButton::Copy:
        copyToClipboard();
        break;

    default:
        break;
    }
}

QPixmap RegionSelector::getSelectedRegion()
{
    QRect sel = m_selectionManager->selectionRect();

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

    QString picturesPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString defaultName = QString("%1/Screenshot_%2.png").arg(picturesPath).arg(timestamp);

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
    if (event->button() == Qt::LeftButton) {
        if (m_selectionManager->isComplete()) {
            // Handle inline text editing
            if (handleInlineTextEditorPress(event->pos())) {
                return;
            }

            auto finalizePolylineForUiClick = [&](const QPoint& pos) {
                if (m_currentTool == ToolbarButton::Arrow &&
                    m_toolManager->polylineMode() &&
                    m_toolManager->isDrawing()) {
                    m_toolManager->handleDoubleClick(pos);
                    m_isDrawing = m_toolManager->isDrawing();
                }
            };

            // Check if clicked on toolbar FIRST (before widgets that may overlap)
            int buttonIdx = m_toolbar->buttonAtPosition(event->pos());
            if (buttonIdx >= 0) {
                int buttonId = m_toolbar->buttonIdAt(buttonIdx);
                if (buttonId >= 0) {
                    finalizePolylineForUiClick(event->pos());
                    handleToolbarClick(static_cast<ToolbarButton>(buttonId));
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
            }

            // Check if clicking on gizmo handle of currently selected text annotation
            if (auto* textItem = getSelectedTextAnnotation()) {
                GizmoHandle handle = TransformationGizmo::hitTest(textItem, event->pos());
                if (handle != GizmoHandle::None) {
                    if (handle == GizmoHandle::Body) {
                        // Check for double-click to start re-editing
                        qint64 now = QDateTime::currentMSecsSinceEpoch();
                        if (m_textAnnotationEditor->isDoubleClick(event->pos(), now)) {
                            startTextReEditing(m_annotationLayer->selectedIndex());
                            m_textAnnotationEditor->recordClick(QPoint(), 0);  // Reset
                            return;
                        }
                        m_textAnnotationEditor->recordClick(event->pos(), now);

                        // Start dragging (move)
                        m_textAnnotationEditor->startDragging(event->pos());
                    }
                    else {
                        // Start rotation or scaling
                        m_textAnnotationEditor->startTransformation(event->pos(), handle);
                    }
                    setFocus();
                    update();
                    return;
                }
            }

            // Check if clicked on existing text annotation (for selection or re-editing)
            int hitIndex = m_annotationLayer->hitTestText(event->pos());
            if (hitIndex >= 0) {
                // Check for double-click to start re-editing
                qint64 now = QDateTime::currentMSecsSinceEpoch();
                if (m_textAnnotationEditor->isDoubleClick(event->pos(), now) &&
                    hitIndex == m_annotationLayer->selectedIndex()) {
                    // Double-click detected - start re-editing
                    startTextReEditing(hitIndex);
                    m_textAnnotationEditor->recordClick(QPoint(), 0);  // Reset to prevent triple-click
                    return;
                }
                m_textAnnotationEditor->recordClick(event->pos(), now);

                m_annotationLayer->setSelectedIndex(hitIndex);
                // If clicking on a different text, check its gizmo handles
                if (auto* textItem = getSelectedTextAnnotation()) {
                    GizmoHandle handle = TransformationGizmo::hitTest(textItem, event->pos());
                    if (handle == GizmoHandle::Body || handle == GizmoHandle::None) {
                        // Start dragging if on body, otherwise just select
                        m_textAnnotationEditor->startDragging(event->pos());
                    }
                    else {
                        // Start transformation if on a handle
                        m_textAnnotationEditor->startTransformation(event->pos(), handle);
                    }
                }
                setFocus();  // Ensure we have keyboard focus for Delete key
                update();
                return;
            }

            // Click elsewhere clears selection (auto-deselect)
            if (m_annotationLayer->selectedIndex() >= 0) {
                m_annotationLayer->clearSelection();
                update();
            }

            // Handle Text tool - can be placed anywhere on screen
            QRect sel = m_selectionManager->selectionRect();
            if (m_currentTool == ToolbarButton::Text) {
                m_textAnnotationEditor->startEditing(event->pos(), rect(), m_annotationColor);
                return;
            }

            // Check if we're using an annotation tool and clicking inside selection
            if (isAnnotationTool(m_currentTool) && m_currentTool != ToolbarButton::Selection && sel.contains(event->pos())) {
                // Start annotation drawing
                qDebug() << "Starting annotation with tool:" << static_cast<int>(m_currentTool) << "at pos:" << event->pos();
                startAnnotation(event->pos());
                return;
            }

            // Check for resize handles or move (Selection tool)
            if (m_currentTool == ToolbarButton::Selection) {
                auto handle = m_selectionManager->hitTestHandle(event->pos());
                if (handle != SelectionStateManager::ResizeHandle::None) {
                    // Start resizing
                    m_selectionManager->startResize(event->pos(), handle);
                    update();
                    return;
                }

                // Check for move (interior of selection)
                if (m_selectionManager->hitTestMove(event->pos())) {
                    m_selectionManager->startMove(event->pos());
                    update();
                    return;
                }

                // Click outside selection - cancel selection (same as ESC)
                qDebug() << "RegionSelector: Cancelled via click outside selection";
                emit selectionCancelled();
                close();
                return;
            }
        }
        else {
            // Start new selection
            m_selectionManager->startSelection(event->pos());
            m_startPoint = event->pos();
            m_currentPoint = m_startPoint;
            m_lastSelectionRect = QRect();  // Reset dirty region tracking
        }
        update();
    }
    else if (event->button() == Qt::RightButton) {
        if (m_selectionManager->isComplete()) {
            // If drawing, cancel current annotation
            if (m_isDrawing) {
                m_isDrawing = false;
                m_toolManager->cancelDrawing();
                update();
                return;
            }
            // If resizing/moving, cancel
            if (m_selectionManager->isResizing() || m_selectionManager->isMoving()) {
                m_selectionManager->cancelResizeOrMove();
                update();
                return;
            }
            // Cancel selection, go back to selection mode
            m_selectionManager->clearSelection();
            m_annotationLayer->clear();
            setCursor(Qt::CrossCursor);
            update();
        }
        else {
            // Before selection is made, right-click is a no-op (stay in Region Capture mode)
            return;
        }
    }
}

void RegionSelector::mouseMoveEvent(QMouseEvent* event)
{
    m_currentPoint = event->pos();

    // Handle text editor in confirm mode
    if (m_textEditor->isEditing() && m_textEditor->isConfirmMode()) {
        m_textEditor->handleMouseMove(event->pos());
        // Show appropriate cursor when in confirm mode
        if (m_textEditor->contains(event->pos())) {
            setCursor(Qt::SizeAllCursor);
        }
        else {
            setCursor(Qt::ArrowCursor);
        }
        update();
        return;
    }

    // Handle text annotation transformation (rotation/scale)
    if (m_textAnnotationEditor->isTransforming() && m_annotationLayer->selectedIndex() >= 0) {
        m_textAnnotationEditor->updateTransformation(event->pos());
        update();
        return;
    }

    // Handle dragging selected text annotation
    if (m_textAnnotationEditor->isDragging() && m_annotationLayer->selectedIndex() >= 0) {
        m_textAnnotationEditor->updateDragging(event->pos());
        update();
        return;
    }

    // Window detection during hover (before any selection or dragging)
    // Throttle by distance to avoid expensive window enumeration on every mouse move
    if (!m_selectionManager->hasActiveSelection() && m_windowDetector) {
        int dx = event->pos().x() - m_lastWindowDetectionPos.x();
        int dy = event->pos().y() - m_lastWindowDetectionPos.y();
        if (dx * dx + dy * dy >= WINDOW_DETECTION_MIN_DISTANCE_SQ) {
            updateWindowDetection(event->pos());
            m_lastWindowDetectionPos = event->pos();
        }
    }

    if (m_selectionManager->isSelecting()) {
        // Clear window detection when dragging
        m_highlightedWindowRect = QRect();
        m_detectedWindow.reset();
        m_selectionManager->updateSelection(m_currentPoint);
    }
    else if (m_selectionManager->isResizing()) {
        // Update selection rect based on resize handle
        m_selectionManager->updateResize(event->pos());
    }
    else if (m_selectionManager->isMoving()) {
        // Move selection rect
        m_selectionManager->updateMove(event->pos());
    }
    else if (m_isDrawing) {
        // Update current annotation
        updateAnnotation(event->pos());

        // Keep correct cursor during drawing for Mosaic/Eraser
        if (m_currentTool == ToolbarButton::Mosaic || m_currentTool == ToolbarButton::Eraser) {
            setToolCursor();
        }
    }
    else if (m_selectionManager->isComplete()) {
        bool colorPaletteHovered = false;
        bool lineWidthHovered = false;
        bool unifiedWidgetHovered = false;
        bool textAnnotationHovered = false;
        bool gizmoHandleHovered = false;

        // Update eraser hover point when eraser tool is active
        if (m_currentTool == ToolbarButton::Eraser && !m_isDrawing) {
            if (auto* eraser = dynamic_cast<EraserToolHandler*>(m_toolManager->currentHandler())) {
                eraser->setHoverPoint(event->pos());
            }
        }

        // Check if hovering over gizmo handles of selected text annotation
        if (auto* textItem = getSelectedTextAnnotation()) {
            GizmoHandle handle = TransformationGizmo::hitTest(textItem, event->pos());
            if (handle != GizmoHandle::None) {
                setCursor(getCursorForGizmoHandle(handle));
                gizmoHandleHovered = true;
            }
        }

        // Check if hovering over any text annotation (including unselected ones)
        if (!gizmoHandleHovered && m_annotationLayer->hitTestText(event->pos()) >= 0) {
            setCursor(Qt::SizeAllCursor);
            textAnnotationHovered = true;
        }

        // Update unified color and width widget
        if (shouldShowColorAndWidthWidget()) {
            if (m_colorAndWidthWidget->handleMouseMove(event->pos(), event->buttons() & Qt::LeftButton)) {
                update();
            }
            if (m_colorAndWidthWidget->updateHovered(event->pos())) {
                update();
            }
            if (m_colorAndWidthWidget->contains(event->pos())) {
                setCursor(Qt::PointingHandCursor);
                unifiedWidgetHovered = true;
            }
        }

        // Legacy widgets (only handle if unified widget not shown)
        if (!shouldShowColorAndWidthWidget()) {
            // Update hovered color swatch
            if (shouldShowColorPalette()) {
                if (m_colorPalette->updateHoveredSwatch(event->pos())) {
                    if (m_colorPalette->contains(event->pos())) {
                        setCursor(Qt::PointingHandCursor);
                        colorPaletteHovered = true;
                    }
                }
                else if (m_colorPalette->contains(event->pos())) {
                    colorPaletteHovered = true;
                }
            }

            // Update line width widget (handle dragging and hover)
            if (shouldShowLineWidthWidget()) {
                if (m_lineWidthWidget->handleMouseMove(event->pos(), event->buttons() & Qt::LeftButton)) {
                    update();
                }
                if (m_lineWidthWidget->contains(event->pos())) {
                    setCursor(Qt::PointingHandCursor);
                    lineWidthHovered = true;
                }
            }
        }

        // Update hovered button using toolbar widget
        bool hoverChanged = m_toolbar->updateHoveredButton(event->pos());
        int hoveredButton = m_toolbar->hoveredButton();
        if (hoverChanged) {
            if (hoveredButton >= 0) {
                setCursor(Qt::PointingHandCursor);
            }
            else if (colorPaletteHovered || lineWidthHovered || unifiedWidgetHovered || textAnnotationHovered || gizmoHandleHovered) {
                // Already set cursor for color swatch, line width widget, unified widget, text annotation, or gizmo handle
            }
            else if (m_currentTool == ToolbarButton::Selection) {
                // Selection tool: update cursor based on handle position
                auto handle = m_selectionManager->hitTestHandle(event->pos());
                updateCursorForHandle(handle);
            }
            else {
                setToolCursor();
            }
        }
        // Always update cursor based on current tool when not hovering any special UI element
        if (hoveredButton < 0 && !colorPaletteHovered && !lineWidthHovered && !unifiedWidgetHovered && !textAnnotationHovered && !gizmoHandleHovered) {
            // Update cursor based on current tool
            if (m_currentTool == ToolbarButton::Selection) {
                auto handle = m_selectionManager->hitTestHandle(event->pos());
                updateCursorForHandle(handle);
            }
            else {
                setToolCursor();
            }
        }
    }

    // Per-operation throttling for smooth 60fps experience on high-resolution displays
    // Different operations have different update frequency requirements
    if (m_selectionManager->isSelecting() || m_selectionManager->isResizing() || m_selectionManager->isMoving()) {
        // Selection/resize/move: high frequency (120fps) for responsiveness
        // Note: Full update required because overlay affects entire screen outside selection
        if (m_updateThrottler.shouldUpdate(UpdateThrottler::ThrottleType::Selection)) {
            m_updateThrottler.reset(UpdateThrottler::ThrottleType::Selection);
            update();
        }
    }
    else if (m_isDrawing) {
        // Annotation drawing: medium frequency (80fps) to balance smoothness and performance
        if (m_updateThrottler.shouldUpdate(UpdateThrottler::ThrottleType::Annotation)) {
            m_updateThrottler.reset(UpdateThrottler::ThrottleType::Annotation);
            update();
        }
    }
    else if (m_selectionManager->isComplete()) {
        // Hover effects in selection complete mode: lower frequency (30fps)
        if (m_updateThrottler.shouldUpdate(UpdateThrottler::ThrottleType::Hover)) {
            m_updateThrottler.reset(UpdateThrottler::ThrottleType::Hover);
            update();
        }
    }
    else {
        // Magnifier-only mode (pre-selection): use dirty region for magnifier
        if (m_updateThrottler.shouldUpdate(UpdateThrottler::ThrottleType::Magnifier)) {
            m_updateThrottler.reset(UpdateThrottler::ThrottleType::Magnifier);
            // Calculate magnifier panel rect and update only that region
            const int panelWidth = MagnifierPanel::kWidth;
            const int totalHeight = MagnifierPanel::kHeight + 85;  // magnifier + info area
            int panelX = m_currentPoint.x() - panelWidth / 2;
            panelX = qMax(10, qMin(panelX, width() - panelWidth - 10));

            // Calculate two possible panel positions (below and above), ensure dirty region covers flip case
            int panelYBelow = m_currentPoint.y() + 25;
            int panelYAbove = m_currentPoint.y() - totalHeight - 25;
            QRect belowRect(panelX - 5, panelYBelow - 5, panelWidth + 10, totalHeight + 10);
            QRect aboveRect(panelX - 5, panelYAbove - 5, panelWidth + 10, totalHeight + 10);
            QRect currentMagRect = belowRect.united(aboveRect);

            QRect dirtyRect = m_lastMagnifierRect.united(currentMagRect);

            // Crosshair region - widen clear range and include last position
            dirtyRect = dirtyRect.united(QRect(0, m_currentPoint.y() - 3, width(), 6));
            dirtyRect = dirtyRect.united(QRect(m_currentPoint.x() - 3, 0, 6, height()));

            m_lastMagnifierRect = currentMagRect;
            update(dirtyRect);
        }
    }
}

void RegionSelector::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        // Handle text editor drag release
        if (m_textEditor->isEditing() && m_textEditor->isConfirmMode()) {
            m_textEditor->handleMouseRelease(event->pos());
            return;
        }

        // Handle text annotation transformation release
        if (m_textAnnotationEditor->isTransforming()) {
            m_textAnnotationEditor->finishTransformation();
            return;
        }

        // Handle text annotation drag release
        if (m_textAnnotationEditor->isDragging()) {
            m_textAnnotationEditor->finishDragging();
            return;
        }

        if (m_selectionManager->isSelecting()) {
            QRect sel = m_selectionManager->selectionRect();
            if (sel.width() > 5 && sel.height() > 5) {
                // 有拖曳 - 使用拖曳選取的區域
                m_selectionManager->finishSelection();
                setCursor(Qt::ArrowCursor);
                qDebug() << "RegionSelector: Selection complete via drag";
            }
            else if (m_detectedWindow.has_value() && m_highlightedWindowRect.isValid()) {
                // Click on detected window - use its bounds
                m_selectionManager->setFromDetectedWindow(m_highlightedWindowRect);
                setCursor(Qt::ArrowCursor);
                qDebug() << "RegionSelector: Selection complete via detected window:" << m_detectedWindow->windowTitle;

                // Clear detection state
                m_highlightedWindowRect = QRect();
                m_detectedWindow.reset();
            }
            else {
                // 點擊無拖曳且無偵測到視窗 - 選取整個螢幕（包含 menu bar）
                QRect screenGeom = m_currentScreen->geometry();
                m_selectionManager->setFromDetectedWindow(QRect(0, 0, screenGeom.width(), screenGeom.height()));
                setCursor(Qt::ArrowCursor);
                qDebug() << "RegionSelector: Click without drag - selecting full screen (including menu bar)";
            }

            update();
        }
        else if (m_selectionManager->isResizing()) {
            // Finish resize
            m_selectionManager->finishResize();
            update();
        }
        else if (m_selectionManager->isMoving()) {
            // Finish move
            m_selectionManager->finishMove();
            update();
        }
        else if (m_isDrawing) {
            // Finish annotation
            finishAnnotation();
            update();
        }
        else if (shouldShowColorAndWidthWidget() && m_colorAndWidthWidget->handleMouseRelease(event->pos())) {
            update();
        }
        else if (!shouldShowColorAndWidthWidget() && shouldShowLineWidthWidget() && m_lineWidthWidget->handleMouseRelease(event->pos())) {
            update();
        }
    }
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

void RegionSelector::drawAnnotations(QPainter& painter)
{
    m_annotationLayer->draw(painter);

    // Draw transformation gizmo for selected text annotation
    if (auto* textItem = getSelectedTextAnnotation()) {
        TransformationGizmo::draw(painter, textItem);
    }
}

void RegionSelector::drawCurrentAnnotation(QPainter& painter)
{
    // Use ToolManager for tools it handles
    if (isToolManagerHandledTool(m_currentTool)) {
        m_toolManager->drawCurrentPreview(painter);
        return;
    }

    // Text tool is handled by InlineTextEditor, no preview needed here
}

void RegionSelector::startAnnotation(const QPoint& pos)
{
    // Use ToolManager for tools it handles
    if (isToolManagerHandledTool(m_currentTool)) {
        // Sync tool manager settings before starting
        m_toolManager->setColor(m_annotationColor);
        m_toolManager->setWidth(toolWidthForCurrentTool());
        m_toolManager->setArrowStyle(m_arrowStyle);
        m_toolManager->setLineStyle(m_lineStyle);
        m_toolManager->setShapeType(static_cast<int>(m_shapeType));
        m_toolManager->setShapeFillMode(static_cast<int>(m_shapeFillMode));

        ToolId toolId = toolbarButtonToToolId(m_currentTool);
        m_toolManager->setCurrentTool(toolId);
        m_toolManager->handleMousePress(pos);
        m_isDrawing = m_toolManager->isDrawing();

        // For click-to-place tools (like StepBadge), set flag so mouseRelease still calls finishAnnotation
        if (!m_isDrawing && m_currentTool == ToolbarButton::StepBadge) {
            m_isDrawing = true;  // Force mouseRelease to call finishAnnotation
        }
        return;
    }

    // Text tool is handled by InlineTextEditor, not here
}

void RegionSelector::updateAnnotation(const QPoint& pos)
{
    // Use ToolManager for tools it handles
    if (isToolManagerHandledTool(m_currentTool)) {
        m_toolManager->handleMouseMove(pos);
        return;
    }

    // Text tool is handled by InlineTextEditor, no mouse move handling needed
}

void RegionSelector::finishAnnotation()
{
    // Use ToolManager for tools it handles
    if (isToolManagerHandledTool(m_currentTool)) {
        m_toolManager->handleMouseRelease(m_currentPoint);
        m_isDrawing = m_toolManager->isDrawing();
        return;
    }

    // Text tool is handled by InlineTextEditor
    m_isDrawing = false;
}

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

TextAnnotation* RegionSelector::getSelectedTextAnnotation() const
{
    if (m_annotationLayer->selectedIndex() >= 0) {
        return dynamic_cast<TextAnnotation*>(m_annotationLayer->selectedItem());
    }
    return nullptr;
}

bool RegionSelector::handleInlineTextEditorPress(const QPoint& pos)
{
    if (!m_textEditor->isEditing()) {
        return false;
    }

    if (m_textEditor->isConfirmMode()) {
        // In confirm mode: click outside finishes, click inside starts drag
        if (!m_textEditor->contains(pos)) {
            m_textEditor->finishEditing();
            return false;  // Continue processing click for next action
        }
        // Start dragging
        m_textEditor->handleMousePress(pos);
        return true;
    }

    // In typing mode: click outside finishes editing immediately
    if (!m_textEditor->contains(pos)) {
        if (!m_textEditor->textEdit()->toPlainText().trimmed().isEmpty()) {
            m_textEditor->finishEditing();
        } else {
            m_textEditor->cancelEditing();
        }
        return false;  // Continue processing click for next action (e.g., start new text)
    }

    // Click inside text edit - let it handle
    return true;
}

// ============================================================================
// Selection Resize/Move Helper Functions
// ============================================================================

void RegionSelector::updateCursorForHandle(SelectionStateManager::ResizeHandle handle)
{
    using ResizeHandle = SelectionStateManager::ResizeHandle;
    switch (handle) {
    case ResizeHandle::TopLeft:
    case ResizeHandle::BottomRight:
        setCursor(Qt::SizeFDiagCursor);
        break;
    case ResizeHandle::TopRight:
    case ResizeHandle::BottomLeft:
        setCursor(Qt::SizeBDiagCursor);
        break;
    case ResizeHandle::Top:
    case ResizeHandle::Bottom:
        setCursor(Qt::SizeVerCursor);
        break;
    case ResizeHandle::Left:
    case ResizeHandle::Right:
        setCursor(Qt::SizeHorCursor);
        break;
    case ResizeHandle::None:
        // Check if inside selection (for move cursor)
        if (m_selectionManager->hitTestMove(m_currentPoint)) {
            setCursor(Qt::SizeAllCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }
        break;
    default:
        setCursor(Qt::ArrowCursor);
        break;
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

QSettings RegionSelector::getSettings() const
{
    return QSettings("Victor Fu", "SnapTray");
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

LineEndStyle RegionSelector::loadArrowStyle() const
{
    int style = getSettings().value("annotation/arrowStyle", static_cast<int>(LineEndStyle::EndArrow)).toInt();
    return static_cast<LineEndStyle>(style);
}

void RegionSelector::saveArrowStyle(LineEndStyle style)
{
    getSettings().setValue("annotation/arrowStyle", static_cast<int>(style));
}

void RegionSelector::onArrowStyleChanged(LineEndStyle style)
{
    m_arrowStyle = style;
    m_toolManager->setArrowStyle(style);
    saveArrowStyle(style);
    update();
}

LineStyle RegionSelector::loadLineStyle() const
{
    int style = getSettings().value("annotation/lineStyle", static_cast<int>(LineStyle::Solid)).toInt();
    return static_cast<LineStyle>(style);
}

void RegionSelector::saveLineStyle(LineStyle style)
{
    getSettings().setValue("annotation/lineStyle", static_cast<int>(style));
}

void RegionSelector::onLineStyleChanged(LineStyle style)
{
    m_lineStyle = style;
    m_toolManager->setLineStyle(style);
    saveLineStyle(style);
    update();
}

TextFormattingState RegionSelector::loadTextFormatting() const
{
    QSettings settings = getSettings();
    TextFormattingState state;
    state.bold = settings.value(SETTINGS_KEY_TEXT_BOLD, true).toBool();
    state.italic = settings.value(SETTINGS_KEY_TEXT_ITALIC, false).toBool();
    state.underline = settings.value(SETTINGS_KEY_TEXT_UNDERLINE, false).toBool();
    state.fontSize = settings.value(SETTINGS_KEY_TEXT_SIZE, 16).toInt();
    state.fontFamily = settings.value(SETTINGS_KEY_TEXT_FAMILY, QString()).toString();
    return state;
}

void RegionSelector::saveTextFormatting()
{
    m_textAnnotationEditor->saveSettings();
}

void RegionSelector::onFontSizeDropdownRequested(const QPoint& pos)
{
    QMenu menu(this);
    menu.setStyleSheet(kDropdownMenuStyle);

    TextFormattingState formatting = m_textAnnotationEditor->formatting();
    static const int sizes[] = { 8, 10, 12, 14, 16, 18, 20, 24, 28, 32, 36, 48, 72 };
    for (int size : sizes) {
        QAction* action = menu.addAction(QString::number(size));
        action->setCheckable(true);
        action->setChecked(size == formatting.fontSize);
        connect(action, &QAction::triggered, this, [this, size]() {
            m_textAnnotationEditor->setFontSize(size);
        });
    }
    menu.exec(mapToGlobal(pos));
}

void RegionSelector::onFontFamilyDropdownRequested(const QPoint& pos)
{
    QMenu menu(this);
    menu.setStyleSheet(kDropdownMenuStyle);

    TextFormattingState formatting = m_textAnnotationEditor->formatting();

    // Add "Default" option
    QAction* defaultAction = menu.addAction(tr("Default"));
    defaultAction->setCheckable(true);
    defaultAction->setChecked(formatting.fontFamily.isEmpty());
    connect(defaultAction, &QAction::triggered, this, [this]() {
        m_textAnnotationEditor->setFontFamily(QString());
    });

    menu.addSeparator();

    // Add common font families
    QStringList families = QFontDatabase::families();
    QStringList commonFonts = { "Arial", "Helvetica", "Times New Roman", "Courier New",
                               "Verdana", "Georgia", "Trebuchet MS", "Impact" };
    for (const QString& family : commonFonts) {
        if (families.contains(family)) {
            QAction* action = menu.addAction(family);
            action->setCheckable(true);
            action->setChecked(family == formatting.fontFamily);
            connect(action, &QAction::triggered, this, [this, family]() {
                m_textAnnotationEditor->setFontFamily(family);
            });
        }
    }

    menu.exec(mapToGlobal(pos));
}
