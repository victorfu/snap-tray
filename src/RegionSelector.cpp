#include "RegionSelector.h"
#include "AnnotationLayer.h"
#include "IconRenderer.h"
#include "ColorPaletteWidget.h"
#include "LineWidthWidget.h"
#include "ColorAndWidthWidget.h"
#include "ColorPickerDialog.h"
#include "OCRManager.h"
#include "PlatformFeatures.h"
#include <QTextEdit>

#include <cstring>
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

static const char* SETTINGS_KEY_ANNOTATION_COLOR = "annotationColor";
static const char* SETTINGS_KEY_ANNOTATION_WIDTH = "annotationWidth";
static const char* SETTINGS_KEY_TEXT_BOLD = "textBold";
static const char* SETTINGS_KEY_TEXT_ITALIC = "textItalic";
static const char* SETTINGS_KEY_TEXT_UNDERLINE = "textUnderline";
static const char* SETTINGS_KEY_TEXT_SIZE = "textFontSize";
static const char* SETTINGS_KEY_TEXT_FAMILY = "textFontFamily";
static const char* SETTINGS_KEY_MOSAIC_STRENGTH = "mosaicStrength";

// Create a rounded square cursor for mosaic tool
static QCursor createMosaicCursor(int size) {
    qreal dpr = qApp->devicePixelRatio();
    int pixelSize = static_cast<int>(size * dpr);

    QPixmap pixmap(pixelSize, pixelSize);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Gray border with semi-transparent fill
    painter.setPen(QPen(QColor(128, 128, 128), 1.5));
    painter.setBrush(QColor(255, 255, 255, 30));
    painter.drawRoundedRect(1, 1, size - 2, size - 2, 2, 2);

    painter.end();

    // Hotspot at center
    return QCursor(pixmap, pixelSize / 2, pixelSize / 2);
}

RegionSelector::RegionSelector(QWidget* parent)
    : QWidget(parent)
    , m_isSelecting(false)
    , m_selectionComplete(false)
    , m_currentScreen(nullptr)
    , m_devicePixelRatio(1.0)
    , m_showHexColor(true)
    , m_toolbar(nullptr)
    , m_annotationLayer(nullptr)
    , m_currentTool(ToolbarButton::Selection)
    , m_annotationColor(Qt::red)  // Will be overwritten by loadAnnotationColor()
    , m_annotationWidth(3)        // Will be overwritten by loadAnnotationWidth()
    , m_isDrawing(false)
    , m_activeHandle(ResizeHandle::None)
    , m_isResizing(false)
    , m_isMoving(false)
    , m_isClosing(false)
    , m_isDialogOpen(false)
    , m_windowDetector(nullptr)
    , m_ocrManager(nullptr)
    , m_ocrInProgress(false)
    , m_colorPalette(nullptr)
    , m_textEditor(nullptr)
    , m_colorPickerDialog(nullptr)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);  // 預設已選取完成，使用 ArrowCursor

    // Initialize annotation layer
    m_annotationLayer = new AnnotationLayer(this);

    // Load saved annotation settings (or defaults)
    m_annotationColor = loadAnnotationColor();
    m_annotationWidth = loadAnnotationWidth();
    m_arrowStyle = loadArrowStyle();

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
                // Restore visibility if we were re-editing
                if (m_editingTextIndex >= 0) {
                    auto* textItem = dynamic_cast<TextAnnotation*>(m_annotationLayer->itemAt(m_editingTextIndex));
                    if (textItem) {
                        textItem->setVisible(true);
                    }
                    m_annotationLayer->setSelectedIndex(m_editingTextIndex);
                    m_editingTextIndex = -1;
                }
                update();
            });

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

    // Load text formatting settings
    m_textFormatting = loadTextFormatting();
    m_colorAndWidthWidget->setBold(m_textFormatting.bold);
    m_colorAndWidthWidget->setItalic(m_textFormatting.italic);
    m_colorAndWidthWidget->setUnderline(m_textFormatting.underline);
    m_colorAndWidthWidget->setFontSize(m_textFormatting.fontSize);
    m_colorAndWidthWidget->setFontFamily(m_textFormatting.fontFamily);

    // Connect text formatting signals
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::boldToggled,
            this, &RegionSelector::onBoldToggled);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::italicToggled,
            this, &RegionSelector::onItalicToggled);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::underlineToggled,
            this, &RegionSelector::onUnderlineToggled);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::fontSizeDropdownRequested,
            this, &RegionSelector::onFontSizeDropdownRequested);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::fontFamilyDropdownRequested,
            this, &RegionSelector::onFontFamilyDropdownRequested);

    // Connect arrow style signal
    m_colorAndWidthWidget->setArrowStyle(m_arrowStyle);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::arrowStyleChanged,
            this, &RegionSelector::onArrowStyleChanged);

    // Connect mosaic strength signal and load saved setting
    {
        QSettings settings;
        int savedStrength = settings.value(SETTINGS_KEY_MOSAIC_STRENGTH,
            static_cast<int>(MosaicStrength::Strong)).toInt();
        m_colorAndWidthWidget->setMosaicStrength(static_cast<MosaicStrength>(savedStrength));
    }
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::mosaicStrengthChanged,
            this, [](MosaicStrength strength) {
                QSettings settings;
                settings.setValue(SETTINGS_KEY_MOSAIC_STRENGTH, static_cast<int>(strength));
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

    // Initialize magnifier update timer for throttling
    m_magnifierUpdateTimer.start();

    // Initialize update throttle timers
    m_selectionUpdateTimer.start();
    m_annotationUpdateTimer.start();
    m_hoverUpdateTimer.start();

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

void RegionSelector::initializeMagnifierGridCache()
{
    const int pixelSize = MAGNIFIER_SIZE / MAGNIFIER_GRID_COUNT;

    m_gridOverlayCache = QPixmap(MAGNIFIER_SIZE, MAGNIFIER_SIZE);
    m_gridOverlayCache.fill(Qt::transparent);

    QPainter gridPainter(&m_gridOverlayCache);

    // Draw grid lines
    gridPainter.setPen(QPen(QColor(100, 100, 100, 100), 1));
    for (int i = 1; i < MAGNIFIER_GRID_COUNT; ++i) {
        int pos = i * pixelSize;
        gridPainter.drawLine(pos, 0, pos, MAGNIFIER_SIZE);
        gridPainter.drawLine(0, pos, MAGNIFIER_SIZE, pos);
    }

    // Draw center crosshair
    gridPainter.setPen(QPen(QColor(65, 105, 225), 1));
    int cx = MAGNIFIER_SIZE / 2;
    int cy = MAGNIFIER_SIZE / 2;
    gridPainter.drawLine(cx - pixelSize / 2, cy, cx + pixelSize / 2, cy);
    gridPainter.drawLine(cx, cy - pixelSize / 2, cx, cy + pixelSize / 2);
}

void RegionSelector::invalidateMagnifierCache()
{
    m_magnifierCacheValid = false;
}

void RegionSelector::setupToolbarButtons()
{
    // Load icons
    IconRenderer& iconRenderer = IconRenderer::instance();
    iconRenderer.loadIcon("selection", ":/icons/icons/selection.svg");
    iconRenderer.loadIcon("arrow", ":/icons/icons/arrow.svg");
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
    buttons.append({static_cast<int>(ToolbarButton::Selection), "selection", "Selection", false});
    buttons.append({static_cast<int>(ToolbarButton::Arrow), "arrow", "Arrow", false});
    buttons.append({static_cast<int>(ToolbarButton::Pencil), "pencil", "Pencil", false});
    buttons.append({static_cast<int>(ToolbarButton::Marker), "marker", "Marker", false});
    buttons.append({static_cast<int>(ToolbarButton::Rectangle), "rectangle", "Rectangle", false});
    buttons.append({static_cast<int>(ToolbarButton::Ellipse), "ellipse", "Ellipse", false});
    buttons.append({static_cast<int>(ToolbarButton::Text), "text", "Text", false});
    buttons.append({static_cast<int>(ToolbarButton::Mosaic), "mosaic", "Mosaic", false});
    buttons.append({static_cast<int>(ToolbarButton::StepBadge), "step-badge", "Step Badge", false});
    buttons.append({static_cast<int>(ToolbarButton::Eraser), "eraser", "Eraser", false});
    buttons.append({static_cast<int>(ToolbarButton::Undo), "undo", "Undo", false});
    buttons.append({static_cast<int>(ToolbarButton::Redo), "redo", "Redo", false});
    buttons.append({static_cast<int>(ToolbarButton::Cancel), "cancel", "Cancel (Esc)", true});  // separator before
    if (PlatformFeatures::instance().isOCRAvailable()) {
        buttons.append({static_cast<int>(ToolbarButton::OCR), "ocr", "OCR Text Recognition", false});
    }
    buttons.append({static_cast<int>(ToolbarButton::Record), "record", "Screen Recording (R)", false});
    buttons.append({static_cast<int>(ToolbarButton::Pin), "pin", "Pin to Screen (Enter)", false});
    buttons.append({static_cast<int>(ToolbarButton::Save), "save", "Save (Ctrl+S)", false});
    buttons.append({static_cast<int>(ToolbarButton::Copy), "copy", "Copy (Ctrl+C)", false});

    m_toolbar->setButtons(buttons);

    // Set which buttons are "active" type (annotation tools that stay highlighted)
    QVector<int> activeButtonIds = {
        static_cast<int>(ToolbarButton::Selection),
        static_cast<int>(ToolbarButton::Arrow),
        static_cast<int>(ToolbarButton::Pencil),
        static_cast<int>(ToolbarButton::Marker),
        static_cast<int>(ToolbarButton::Rectangle),
        static_cast<int>(ToolbarButton::Ellipse),
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

    ToolbarButton btn = static_cast<ToolbarButton>(buttonId);

    if (buttonId == static_cast<int>(ToolbarButton::Cancel)) {
        return QColor(255, 100, 100);  // Red for cancel
    }
    if (btn == ToolbarButton::Record) {
        return QColor(255, 80, 80);  // Red for recording
    }
    if (btn == ToolbarButton::OCR) {
        if (m_ocrInProgress) {
            return QColor(255, 200, 100);  // Yellow when processing
        } else if (!m_ocrManager) {
            return QColor(128, 128, 128);  // Gray if unavailable
        } else {
            return QColor(100, 200, 255);  // Blue accent
        }
    }
    if (buttonId >= static_cast<int>(ToolbarButton::Pin)) {
        return QColor(100, 200, 255);  // Blue for action buttons
    }
    if (isActive) {
        return Qt::white;
    }
    return QColor(220, 220, 220);  // Off-white for normal state
}

bool RegionSelector::shouldShowColorPalette() const
{
    if (!m_selectionComplete) return false;

    // Show palette for color-enabled tools (not Mosaic)
    switch (m_currentTool) {
    case ToolbarButton::Pencil:
    case ToolbarButton::Marker:
    case ToolbarButton::Arrow:
    case ToolbarButton::Rectangle:
    case ToolbarButton::Ellipse:
    case ToolbarButton::Text:
    case ToolbarButton::StepBadge:
        return true;
    default:
        return false;
    }
}

void RegionSelector::onColorSelected(const QColor &color)
{
    m_annotationColor = color;

    // Update line width widget preview color
    m_lineWidthWidget->setPreviewColor(color);

    // Update unified widget color
    m_colorAndWidthWidget->setCurrentColor(color);

    // Update text editor color if currently editing
    if (m_textEditor->isEditing()) {
        m_textEditor->setColor(color);
    }

    // Save for next session
    saveAnnotationColor(color);

    update();
}

void RegionSelector::onMoreColorsRequested()
{
    if (!m_colorPickerDialog) {
        m_colorPickerDialog = new ColorPickerDialog();
        connect(m_colorPickerDialog, &ColorPickerDialog::colorSelected,
                this, [this](const QColor &color) {
            m_annotationColor = color;
            m_colorPalette->setCurrentColor(color);
            m_lineWidthWidget->setPreviewColor(color);
            m_colorAndWidthWidget->setCurrentColor(color);

            // Update text editor color if currently editing
            if (m_textEditor->isEditing()) {
                m_textEditor->setColor(color);
            }

            qDebug() << "Custom color selected:" << color.name();
            update();
        });
    }

    m_colorPickerDialog->setCurrentColor(m_annotationColor);

    // Keep unified color/width widget in sync with current annotation color
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
    if (!m_selectionComplete) return false;

    // Show for tools that use m_annotationWidth
    return m_currentTool == ToolbarButton::Pencil ||
           m_currentTool == ToolbarButton::Arrow ||
           m_currentTool == ToolbarButton::Rectangle ||
           m_currentTool == ToolbarButton::Ellipse;
}

void RegionSelector::onLineWidthChanged(int width)
{
    m_annotationWidth = width;
    saveAnnotationWidth(width);
    update();
}

bool RegionSelector::shouldShowColorAndWidthWidget() const
{
    if (!m_selectionComplete) return false;

    // Show for tools that need either color or width (union of both)
    switch (m_currentTool) {
    case ToolbarButton::Pencil:      // Needs both
    case ToolbarButton::Marker:      // Needs color only
    case ToolbarButton::Arrow:       // Needs both
    case ToolbarButton::Rectangle:   // Needs both
    case ToolbarButton::Ellipse:     // Needs both
    case ToolbarButton::Text:        // Needs color only
    case ToolbarButton::StepBadge:   // Needs color only
    case ToolbarButton::Mosaic:      // Needs mosaic strength only
        return true;
    default:
        return false;
    }
}

bool RegionSelector::shouldShowWidthControl() const
{
    // Marker, Text, and StepBadge have fixed width, so don't show width control
    switch (m_currentTool) {
    case ToolbarButton::Pencil:
    case ToolbarButton::Arrow:
    case ToolbarButton::Rectangle:
    case ToolbarButton::Ellipse:
        return true;
    default:
        return false;  // Marker, Text, StepBadge don't need width control
    }
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

void RegionSelector::initializeForScreen(QScreen* screen, const QPixmap &preCapture)
{
    // 使用傳入的螢幕，若為空則使用主螢幕
    m_currentScreen = screen;
    if (!m_currentScreen) {
        m_currentScreen = QGuiApplication::primaryScreen();
    }

    m_devicePixelRatio = m_currentScreen->devicePixelRatio();

    // Use pre-captured pixmap if provided, otherwise capture now
    // Pre-capture allows including popup menus in the screenshot (like Snipaste)
    if (!preCapture.isNull()) {
        m_backgroundPixmap = preCapture;
        qDebug() << "RegionSelector: Using pre-captured screenshot, size:" << preCapture.size();
    } else {
        m_backgroundPixmap = m_currentScreen->grabWindow(0);
        qDebug() << "RegionSelector: Captured screenshot now, size:" << m_backgroundPixmap.size();
    }
    // Lazy-load image cache: don't convert now, create on first magnifier access
    m_backgroundImageCacheValid = false;

    qDebug() << "RegionSelector: Initialized for screen" << m_currentScreen->name()
        << "logical size:" << m_currentScreen->geometry().size()
        << "pixmap size:" << m_backgroundPixmap.size()
        << "devicePixelRatio:" << m_devicePixelRatio;

    // 不預設選取整個螢幕，等待用戶操作
    m_selectionRect = QRect();
    m_selectionComplete = false;
    m_isSelecting = false;

    // 將 cursor 全域座標轉換為 widget 本地座標，用於 crosshair/magnifier 初始位置
    QRect screenGeom = m_currentScreen->geometry();
    QPoint globalCursor = QCursor::pos();
    m_currentPoint = globalCursor - screenGeom.topLeft();

    // 鎖定視窗大小，防止 macOS 原生 resize 行為
    setFixedSize(screenGeom.size());

    // Initialize magnifier caches
    initializeMagnifierGridCache();
    invalidateMagnifierCache();

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

void RegionSelector::initializeWithRegion(QScreen *screen, const QRect &region)
{
    // Use provided screen or fallback to primary
    m_currentScreen = screen;
    if (!m_currentScreen) {
        m_currentScreen = QGuiApplication::primaryScreen();
    }

    m_devicePixelRatio = m_currentScreen->devicePixelRatio();

    // Capture the screen first
    m_backgroundPixmap = m_currentScreen->grabWindow(0);
    m_backgroundImageCache = m_backgroundPixmap.toImage();

    qDebug() << "RegionSelector: Initialized with region" << region
             << "on screen" << m_currentScreen->name();

    // Convert global region to local coordinates
    QRect screenGeom = m_currentScreen->geometry();
    m_selectionRect = region.translated(-screenGeom.topLeft());

    // Mark selection as complete
    m_selectionComplete = true;
    m_isSelecting = false;

    // Set cursor position
    QPoint globalCursor = QCursor::pos();
    m_currentPoint = globalCursor - screenGeom.topLeft();

    // Lock window size
    setFixedSize(screenGeom.size());

    // Trigger repaint to show toolbar (toolbar is drawn in paintEvent when m_selectionComplete is true)
    QTimer::singleShot(0, this, [this]() {
        update();
    });
}

void RegionSelector::setWindowDetector(WindowDetector *detector)
{
    m_windowDetector = detector;
}

void RegionSelector::updateWindowDetection(const QPoint &localPos)
{
    if (!m_windowDetector || !m_windowDetector->isEnabled() || !m_currentScreen) {
        return;
    }

    // Convert local position to screen coordinates
    QPoint screenPos = m_currentScreen->geometry().topLeft() + localPos;

    // Detect window at position
    auto detected = m_windowDetector->detectWindowAt(screenPos);

    if (detected.has_value()) {
        // Convert screen coords to local widget coords
        QRect localBounds = detected->bounds.translated(
            -m_currentScreen->geometry().topLeft()
        );

        // Clip to screen bounds
        localBounds = localBounds.intersected(rect());

        if (localBounds != m_highlightedWindowRect) {
            m_highlightedWindowRect = localBounds;
            m_detectedWindow = detected;
            update();
        }
    } else {
        if (!m_highlightedWindowRect.isNull()) {
            m_highlightedWindowRect = QRect();
            m_detectedWindow.reset();
            update();
        }
    }
}

void RegionSelector::drawDetectedWindow(QPainter &painter)
{
    if (m_highlightedWindowRect.isNull() || m_selectionComplete || m_isSelecting) {
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

void RegionSelector::drawWindowHint(QPainter &painter, const QString &title)
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
    } else {
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

    // Draw detected window highlight (only during hover, before selection)
    if (!m_isSelecting && !m_selectionComplete && m_windowDetector) {
        drawDetectedWindow(painter);
    }

    // Draw selection if active or complete
    if ((m_isSelecting || m_selectionComplete) && m_selectionRect.isValid()) {
        drawSelection(painter);
        drawDimensionInfo(painter);

        // Draw annotations on top of selection
        if (m_selectionComplete) {
            drawAnnotations(painter);
            drawCurrentAnnotation(painter);

            // Update toolbar position and draw
            m_toolbar->setActiveButton(static_cast<int>(m_currentTool));
            m_toolbar->setViewportWidth(width());
            m_toolbar->setPositionForSelection(m_selectionRect.normalized(), height());
            m_toolbar->draw(painter);

            // Use unified color and width widget
            if (shouldShowColorAndWidthWidget()) {
                m_colorAndWidthWidget->setVisible(true);
                m_colorAndWidthWidget->setShowWidthSection(shouldShowWidthControl());
                // Show arrow style section only for Arrow tool
                m_colorAndWidthWidget->setShowArrowStyleSection(m_currentTool == ToolbarButton::Arrow);
                // Show text section only for Text tool
                m_colorAndWidthWidget->setShowTextSection(m_currentTool == ToolbarButton::Text);
                // Show mosaic strength section only for Mosaic tool
                m_colorAndWidthWidget->setShowMosaicStrengthSection(m_currentTool == ToolbarButton::Mosaic);
                m_colorAndWidthWidget->updatePosition(m_toolbar->boundingRect(), false, width());
                m_colorAndWidthWidget->draw(painter);
            } else {
                m_colorAndWidthWidget->setVisible(false);
            }

            // Legacy widgets (keep for compatibility, but hidden when unified widget is shown)
            if (!shouldShowColorAndWidthWidget()) {
                if (shouldShowColorPalette()) {
                    m_colorPalette->setVisible(true);
                    m_colorPalette->updatePosition(m_toolbar->boundingRect(), false);
                    m_colorPalette->draw(painter);
                } else {
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
                } else {
                    m_lineWidthWidget->setVisible(false);
                }
            }

            m_toolbar->drawTooltip(painter);

            // Draw loading spinner when OCR is in progress
            if (m_ocrInProgress) {
                QRect sel = m_selectionRect.normalized();
                QPoint center = sel.center();
                m_loadingSpinner->draw(painter, center);
            }
        }
    }

    // Draw crosshair at cursor - only show inside selection when selection is complete
    // Hide magnifier when any annotation tool is selected
    bool shouldShowCrosshair = !m_isSelecting && !m_isDrawing && !isAnnotationTool(m_currentTool);
    if (m_selectionComplete) {
        QRect sel = m_selectionRect.normalized();
        // Only show crosshair inside selection area, not on toolbar
        shouldShowCrosshair = shouldShowCrosshair &&
                              sel.contains(m_currentPoint) &&
                              !m_toolbar->contains(m_currentPoint);
    }

    if (shouldShowCrosshair) {
        drawCrosshair(painter);
        drawMagnifier(painter);
    }
}

void RegionSelector::drawOverlay(QPainter& painter)
{
    QColor dimColor(0, 0, 0, 100);

    if ((m_isSelecting || m_selectionComplete) && m_selectionRect.isValid()) {
        // 選取模式：選取區域外繪製 overlay
        QRect sel = m_selectionRect.normalized();

        // Top region
        painter.fillRect(QRect(0, 0, width(), sel.top()), dimColor);
        // Bottom region
        painter.fillRect(QRect(0, sel.bottom() + 1, width(), height() - sel.bottom() - 1), dimColor);
        // Left region
        painter.fillRect(QRect(0, sel.top(), sel.left(), sel.height()), dimColor);
        // Right region
        painter.fillRect(QRect(sel.right() + 1, sel.top(), width() - sel.right() - 1, sel.height()), dimColor);
    }
    else if (!m_highlightedWindowRect.isNull()) {
        // 窗口偵測模式：偵測到的窗口區域外繪製 overlay
        QRect win = m_highlightedWindowRect;

        // Top region
        painter.fillRect(QRect(0, 0, width(), win.top()), dimColor);
        // Bottom region
        painter.fillRect(QRect(0, win.bottom() + 1, width(), height() - win.bottom() - 1), dimColor);
        // Left region
        painter.fillRect(QRect(0, win.top(), win.left(), win.height()), dimColor);
        // Right region
        painter.fillRect(QRect(win.right() + 1, win.top(), width() - win.right() - 1, win.height()), dimColor);
    }
    else {
        // 無選取、無偵測窗口：整個畫面繪製 overlay
        painter.fillRect(rect(), dimColor);
    }
}

void RegionSelector::drawSelection(QPainter& painter)
{
    QRect sel = m_selectionRect.normalized();

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
    const int magnifierSize = MAGNIFIER_SIZE;  // 放大區域顯示大小
    const int gridCount = MAGNIFIER_GRID_COUNT;       // 顯示 15x15 個像素格子
    const int panelWidth = 180;     // 面板寬度
    const int panelPadding = 10;

    // 取得當前像素顏色 (設備座標)
    int deviceX = static_cast<int>(m_currentPoint.x() * m_devicePixelRatio);
    int deviceY = static_cast<int>(m_currentPoint.y() * m_devicePixelRatio);
    const QImage &img = getBackgroundImage();  // Lazy-loaded for memory efficiency
    QColor pixelColor;
    if (deviceX >= 0 && deviceX < img.width() && deviceY >= 0 && deviceY < img.height()) {
        pixelColor = QColor(img.pixel(deviceX, deviceY));
    }

    // 計算面板位置 (在 cursor 下方)
    int panelX = m_currentPoint.x() - panelWidth / 2;
    int panelY = m_currentPoint.y() + 25;

    // 計算面板總高度
    int totalHeight = magnifierSize + 75;  // 放大鏡 + 座標 + 顏色

    // 邊界檢查
    panelX = qMax(10, qMin(panelX, width() - panelWidth - 10));
    if (panelY + totalHeight > height()) {
        panelY = m_currentPoint.y() - totalHeight - 25;  // 改放上方
    }

    // 繪製面板背景
    QRect panelRect(panelX, panelY, panelWidth, totalHeight);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(30, 35, 45, 240));
    painter.drawRoundedRect(panelRect, 4, 4);

    // 計算放大鏡顯示區域
    int magX = panelX + (panelWidth - magnifierSize) / 2;
    int magY = panelY + panelPadding;

    // Check if magnifier cache is still valid (same device pixel position)
    QPoint currentDevicePos(deviceX, deviceY);
    if (!m_magnifierCacheValid || m_cachedDevicePosition != currentDevicePos) {
        // 從設備像素 pixmap 中取樣
        // 取 gridCount 個「邏輯像素」，每個邏輯像素 = devicePixelRatio 個設備像素
        int deviceGridCount = static_cast<int>(gridCount * m_devicePixelRatio);
        int sampleX = deviceX - deviceGridCount / 2;
        int sampleY = deviceY - deviceGridCount / 2;

        // 建立一個以游標為中心的取樣圖像，超出邊界的部分填充黑色
        QImage sampleImage(deviceGridCount, deviceGridCount, QImage::Format_ARGB32);
        sampleImage.fill(Qt::black);

        // Optimized pixel sampling using scanLine() for direct memory access
        // Pre-calculate valid source region bounds
        int srcLeft = qMax(0, sampleX);
        int srcTop = qMax(0, sampleY);
        int srcRight = qMin(img.width(), sampleX + deviceGridCount);
        int srcBottom = qMin(img.height(), sampleY + deviceGridCount);

        // Calculate destination offsets for out-of-bounds handling
        int dstOffsetX = srcLeft - sampleX;
        int dstOffsetY = srcTop - sampleY;

        // Copy valid region using scanLine for direct memory access (much faster than pixel-by-pixel)
        if (srcRight > srcLeft && srcBottom > srcTop) {
            int copyWidth = srcRight - srcLeft;
            for (int srcY = srcTop; srcY < srcBottom; ++srcY) {
                const QRgb *srcLine = reinterpret_cast<const QRgb*>(img.constScanLine(srcY));
                QRgb *dstLine = reinterpret_cast<QRgb*>(sampleImage.scanLine(srcY - srcTop + dstOffsetY));
                memcpy(dstLine + dstOffsetX, srcLine + srcLeft, copyWidth * sizeof(QRgb));
            }
        }

        // 使用 IgnoreAspectRatio 確保填滿整個區域
        m_magnifierPixmapCache = QPixmap::fromImage(sampleImage).scaled(magnifierSize, magnifierSize,
            Qt::IgnoreAspectRatio,
            Qt::FastTransformation);

        m_cachedDevicePosition = currentDevicePos;
        m_magnifierCacheValid = true;
    }

    // Draw cached magnified pixmap
    painter.drawPixmap(magX, magY, m_magnifierPixmapCache);

    // Draw cached grid overlay (pre-rendered in initializeMagnifierGridCache)
    painter.drawPixmap(magX, magY, m_gridOverlayCache);

    // 2. 座標資訊
    int infoY = magY + magnifierSize + 8;
    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setPointSize(11);
    painter.setFont(font);

    QString coordText = QString("(%1, %2)").arg(m_currentPoint.x()).arg(m_currentPoint.y());
    painter.drawText(panelX + panelPadding, infoY, panelWidth - 2 * panelPadding, 20, Qt::AlignCenter, coordText);

    // 3. 顏色預覽 + HEX/RGB (置中對齊)
    infoY += 20;
    int colorBoxSize = 14;

    // 計算顏色文字
    QString colorText;
    if (m_showHexColor) {
        colorText = QString("#%1%2%3")
            .arg(pixelColor.red(), 2, 16, QChar('0'))
            .arg(pixelColor.green(), 2, 16, QChar('0'))
            .arg(pixelColor.blue(), 2, 16, QChar('0'));
    }
    else {
        colorText = QString("RGB(%1, %2, %3)")
            .arg(pixelColor.red())
            .arg(pixelColor.green())
            .arg(pixelColor.blue());
    }

    // 計算整體寬度以置中：色塊 + 間距 + 文字
    QFontMetrics fm(font);
    int textWidth = fm.horizontalAdvance(colorText);
    int totalWidth = colorBoxSize + 8 + textWidth;
    int startX = panelX + (panelWidth - totalWidth) / 2;

    // 繪製顏色方塊
    painter.fillRect(startX, infoY, colorBoxSize, colorBoxSize, pixelColor);
    painter.setPen(QPen(QColor(80, 80, 80), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(startX, infoY, colorBoxSize, colorBoxSize);

    // 繪製顏色文字
    painter.setPen(Qt::white);
    painter.drawText(startX + colorBoxSize + 8, infoY, textWidth, colorBoxSize, Qt::AlignVCenter, colorText);
}

void RegionSelector::drawDimensionInfo(QPainter& painter)
{
    QRect sel = m_selectionRect.normalized();

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

void RegionSelector::handleToolbarClick(ToolbarButton button)
{
    switch (button) {
    case ToolbarButton::Selection:
    case ToolbarButton::Arrow:
    case ToolbarButton::Pencil:
    case ToolbarButton::Marker:
    case ToolbarButton::Rectangle:
    case ToolbarButton::Ellipse:
    case ToolbarButton::Mosaic:
        m_currentTool = button;
        qDebug() << "Tool selected:" << static_cast<int>(button);
        update();
        break;

    case ToolbarButton::Text:
        m_currentTool = button;
        qDebug() << "Text tool selected - click in selection to add text";
        update();
        break;

    case ToolbarButton::StepBadge:
        m_currentTool = button;
        qDebug() << "StepBadge tool selected - click in selection to place numbered badge";
        update();
        break;

    case ToolbarButton::Eraser:
        m_currentTool = button;
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
        {
            // Emit recording request with the selected region in global coordinates
            QRect globalRect = m_selectionRect.normalized()
                               .translated(m_currentScreen->geometry().topLeft());
            emit recordingRequested(globalRect, m_currentScreen);
            close();
        }
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
    QRect sel = m_selectionRect.normalized();

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
        } else {
            qDebug() << "RegionSelector: Failed to save to" << filePath;
        }
        emit saveRequested(selectedRegion);
        m_isDialogOpen = false;
        close();
    } else {
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

    // 直接計算全局座標：螢幕左上角 + 選區左上角
    QPoint globalPos = m_currentScreen->geometry().topLeft() + m_selectionRect.normalized().topLeft();

    qDebug() << "finishSelection: selectionRect=" << m_selectionRect.normalized()
             << "globalPos=" << globalPos;

    emit regionSelected(selectedRegion, globalPos);
    close();
}

void RegionSelector::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_selectionComplete) {
            // Handle inline text editing
            if (m_textEditor->isEditing()) {
                if (m_textEditor->isConfirmMode()) {
                    // In confirm mode: click outside finishes, click inside starts drag
                    if (!m_textEditor->contains(event->pos())) {
                        m_textEditor->finishEditing();
                        // Continue processing click for next action
                    } else {
                        // Start dragging
                        m_textEditor->handleMousePress(event->pos());
                        return;
                    }
                } else {
                    // In typing mode: click outside enters confirm mode
                    if (!m_textEditor->contains(event->pos())) {
                        if (!m_textEditor->textEdit()->toPlainText().trimmed().isEmpty()) {
                            m_textEditor->enterConfirmMode();
                        } else {
                            m_textEditor->cancelEditing();
                        }
                        return;
                    } else {
                        // Click inside text edit - let it handle
                        return;
                    }
                }
            }

            // Check if clicked on toolbar FIRST (before widgets that may overlap)
            int buttonIdx = m_toolbar->buttonAtPosition(event->pos());
            if (buttonIdx >= 0) {
                int buttonId = m_toolbar->buttonIdAt(buttonIdx);
                if (buttonId >= 0) {
                    handleToolbarClick(static_cast<ToolbarButton>(buttonId));
                }
                return;
            }

            // Check if clicked on unified color and width widget
            if (shouldShowColorAndWidthWidget() && m_colorAndWidthWidget->handleClick(event->pos())) {
                update();
                return;
            }

            // Legacy widgets (only handle if unified widget not shown)
            if (!shouldShowColorAndWidthWidget()) {
                // Check if clicked on color palette
                if (shouldShowColorPalette() && m_colorPalette->handleClick(event->pos())) {
                    update();
                    return;
                }

                // Check if clicked on line width widget
                if (shouldShowLineWidthWidget() && m_lineWidthWidget->handleMousePress(event->pos())) {
                    update();
                    return;
                }
            }

            // Check if clicking on gizmo handle of currently selected text annotation
            if (m_annotationLayer->selectedIndex() >= 0) {
                if (auto* textItem = dynamic_cast<TextAnnotation*>(m_annotationLayer->selectedItem())) {
                    GizmoHandle handle = TransformationGizmo::hitTest(textItem, event->pos());
                    if (handle != GizmoHandle::None) {
                        if (handle == GizmoHandle::Body) {
                            // Check for double-click to start re-editing
                            qint64 now = QDateTime::currentMSecsSinceEpoch();
                            if (now - m_lastTextClickTime < 400 &&
                                (event->pos() - m_lastTextClickPos).manhattanLength() < 5) {
                                startTextReEditing(m_annotationLayer->selectedIndex());
                                m_lastTextClickTime = 0;
                                return;
                            }
                            m_lastTextClickPos = event->pos();
                            m_lastTextClickTime = now;

                            // Start dragging (move)
                            m_isDraggingAnnotation = true;
                            m_annotationDragStart = event->pos();
                        } else {
                            // Start rotation or scaling
                            startTextTransformation(event->pos(), handle);
                        }
                        setFocus();
                        update();
                        return;
                    }
                }
            }

            // Check if clicked on existing text annotation (for selection or re-editing)
            int hitIndex = m_annotationLayer->hitTestText(event->pos());
            if (hitIndex >= 0) {
                // Check for double-click to start re-editing
                qint64 now = QDateTime::currentMSecsSinceEpoch();
                if (now - m_lastTextClickTime < 400 &&
                    (event->pos() - m_lastTextClickPos).manhattanLength() < 5 &&
                    hitIndex == m_annotationLayer->selectedIndex()) {
                    // Double-click detected - start re-editing
                    startTextReEditing(hitIndex);
                    m_lastTextClickTime = 0;  // Reset to prevent triple-click triggering
                    return;
                }
                m_lastTextClickPos = event->pos();
                m_lastTextClickTime = now;

                m_annotationLayer->setSelectedIndex(hitIndex);
                // If clicking on a different text, check its gizmo handles
                if (auto* textItem = dynamic_cast<TextAnnotation*>(m_annotationLayer->selectedItem())) {
                    GizmoHandle handle = TransformationGizmo::hitTest(textItem, event->pos());
                    if (handle == GizmoHandle::Body || handle == GizmoHandle::None) {
                        // Start dragging if on body, otherwise just select
                        m_isDraggingAnnotation = true;
                        m_annotationDragStart = event->pos();
                    } else {
                        // Start transformation if on a handle
                        startTextTransformation(event->pos(), handle);
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
            QRect sel = m_selectionRect.normalized();
            if (m_currentTool == ToolbarButton::Text) {
                m_editingTextIndex = -1;  // Creating new text
                m_textEditor->setColor(m_annotationColor);
                m_textEditor->setFont(m_textFormatting.toQFont());
                m_textEditor->startEditing(event->pos(), rect());  // Use full screen as bounds
                return;
            }

            // Check if we're using an annotation tool and clicking inside selection
            if (isAnnotationTool(m_currentTool) && m_currentTool != ToolbarButton::Selection && sel.contains(event->pos())) {
                // Handle StepBadge tool - place badge on click
                if (m_currentTool == ToolbarButton::StepBadge) {
                    placeStepBadge(event->pos());
                    return;
                }
                // Start annotation drawing
                qDebug() << "Starting annotation with tool:" << static_cast<int>(m_currentTool) << "at pos:" << event->pos();
                startAnnotation(event->pos());
                return;
            }

            // Check for resize handles or move (Selection tool)
            if (m_currentTool == ToolbarButton::Selection) {
                ResizeHandle handle = getHandleAtPosition(event->pos());
                if (handle != ResizeHandle::None) {
                    if (handle == ResizeHandle::Center) {
                        // Start moving
                        m_isMoving = true;
                        m_isResizing = false;
                    } else {
                        // Start resizing
                        m_isResizing = true;
                        m_isMoving = false;
                    }
                    m_activeHandle = handle;
                    m_resizeStartPoint = event->pos();
                    m_originalRect = m_selectionRect;
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
            m_isSelecting = true;
            m_startPoint = event->pos();
            m_currentPoint = m_startPoint;
            m_selectionRect = QRect();
            m_lastSelectionRect = QRect();  // Reset dirty region tracking
        }
        update();
    }
    else if (event->button() == Qt::RightButton) {
        if (m_selectionComplete) {
            // If drawing, cancel current annotation
            if (m_isDrawing) {
                m_isDrawing = false;
                m_currentPath.clear();
                m_currentPencil.reset();
                m_currentMarker.reset();
                m_currentArrow.reset();
                m_currentRectangle.reset();
                m_currentMosaicStroke.reset();
                update();
                return;
            }
            // If resizing/moving, cancel
            if (m_isResizing || m_isMoving) {
                m_selectionRect = m_originalRect;
                m_isResizing = false;
                m_isMoving = false;
                m_activeHandle = ResizeHandle::None;
                update();
                return;
            }
            // Cancel selection, go back to selection mode
            m_selectionComplete = false;
            m_selectionRect = QRect();
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
        } else {
            setCursor(Qt::ArrowCursor);
        }
        update();
        return;
    }

    // Handle text annotation transformation (rotation/scale)
    if (m_isTransformingAnnotation && m_annotationLayer->selectedIndex() >= 0) {
        updateTextTransformation(event->pos());
        update();
        return;
    }

    // Handle dragging selected text annotation
    if (m_isDraggingAnnotation && m_annotationLayer->selectedIndex() >= 0) {
        QPoint delta = event->pos() - m_annotationDragStart;
        auto* textItem = dynamic_cast<TextAnnotation*>(m_annotationLayer->selectedItem());
        if (textItem) {
            textItem->moveBy(delta);
        }
        m_annotationDragStart = event->pos();
        update();
        return;
    }

    // Window detection during hover (before any selection or dragging)
    // Throttle by distance to avoid expensive window enumeration on every mouse move
    if (!m_isSelecting && !m_selectionComplete && m_windowDetector) {
        int dx = event->pos().x() - m_lastWindowDetectionPos.x();
        int dy = event->pos().y() - m_lastWindowDetectionPos.y();
        if (dx * dx + dy * dy >= WINDOW_DETECTION_MIN_DISTANCE_SQ) {
            updateWindowDetection(event->pos());
            m_lastWindowDetectionPos = event->pos();
        }
    }

    if (m_isSelecting) {
        // Clear window detection when dragging
        m_highlightedWindowRect = QRect();
        m_detectedWindow.reset();
        m_selectionRect = QRect(m_startPoint, m_currentPoint).normalized();
    }
    else if (m_isResizing) {
        // Update selection rect based on resize handle
        updateResize(event->pos());
    }
    else if (m_isMoving) {
        // Move selection rect
        QPoint delta = event->pos() - m_resizeStartPoint;
        m_selectionRect = m_originalRect.translated(delta);

        // Clamp to screen bounds
        if (m_selectionRect.left() < 0)
            m_selectionRect.moveLeft(0);
        if (m_selectionRect.top() < 0)
            m_selectionRect.moveTop(0);
        if (m_selectionRect.right() >= width())
            m_selectionRect.moveRight(width() - 1);
        if (m_selectionRect.bottom() >= height())
            m_selectionRect.moveBottom(height() - 1);
    }
    else if (m_isDrawing) {
        // Update current annotation
        updateAnnotation(event->pos());
    }
    else if (m_selectionComplete) {
        bool colorPaletteHovered = false;
        bool lineWidthHovered = false;
        bool unifiedWidgetHovered = false;
        bool textAnnotationHovered = false;
        bool gizmoHandleHovered = false;

        // Check if hovering over gizmo handles of selected text annotation
        if (m_annotationLayer->selectedIndex() >= 0) {
            if (auto* textItem = dynamic_cast<TextAnnotation*>(m_annotationLayer->selectedItem())) {
                GizmoHandle handle = TransformationGizmo::hitTest(textItem, event->pos());
                switch (handle) {
                case GizmoHandle::Rotation:
                    setCursor(Qt::CrossCursor);
                    gizmoHandleHovered = true;
                    break;
                case GizmoHandle::TopLeft:
                case GizmoHandle::BottomRight:
                    setCursor(Qt::SizeFDiagCursor);
                    gizmoHandleHovered = true;
                    break;
                case GizmoHandle::TopRight:
                case GizmoHandle::BottomLeft:
                    setCursor(Qt::SizeBDiagCursor);
                    gizmoHandleHovered = true;
                    break;
                case GizmoHandle::Body:
                    setCursor(Qt::SizeAllCursor);
                    gizmoHandleHovered = true;
                    break;
                default:
                    break;
                }
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
                } else if (m_colorPalette->contains(event->pos())) {
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
            else if (m_currentTool == ToolbarButton::Text && !m_textEditor->isEditing()) {
                setCursor(Qt::IBeamCursor);
            }
            else if (m_currentTool == ToolbarButton::Mosaic) {
                static QCursor mosaicCursor = createMosaicCursor(15);
                setCursor(mosaicCursor);
            }
            else if (isAnnotationTool(m_currentTool) && m_currentTool != ToolbarButton::Selection) {
                setCursor(Qt::CrossCursor);
            }
            else {
                // Selection tool: update cursor based on handle position
                ResizeHandle handle = getHandleAtPosition(event->pos());
                updateCursorForHandle(handle);
            }
        }
        else if (hoveredButton < 0 && !colorPaletteHovered && !lineWidthHovered && !unifiedWidgetHovered && !textAnnotationHovered && !gizmoHandleHovered && m_currentTool == ToolbarButton::Selection) {
            // Update cursor for resize handles when not hovering button or color swatch
            ResizeHandle handle = getHandleAtPosition(event->pos());
            updateCursorForHandle(handle);
        }
    }

    // Per-operation throttling for smooth 60fps experience on high-resolution displays
    // Different operations have different update frequency requirements
    if (m_isSelecting || m_isResizing || m_isMoving) {
        // Selection/resize/move: high frequency (120fps) for responsiveness
        // Note: Full update required because overlay affects entire screen outside selection
        if (m_selectionUpdateTimer.elapsed() >= SELECTION_UPDATE_MS) {
            m_selectionUpdateTimer.restart();
            update();
        }
    } else if (m_isDrawing) {
        // Annotation drawing: medium frequency (80fps) to balance smoothness and performance
        if (m_annotationUpdateTimer.elapsed() >= ANNOTATION_UPDATE_MS) {
            m_annotationUpdateTimer.restart();
            update();
        }
    } else if (m_selectionComplete) {
        // Hover effects in selection complete mode: lower frequency (30fps)
        if (m_hoverUpdateTimer.elapsed() >= HOVER_UPDATE_MS) {
            m_hoverUpdateTimer.restart();
            update();
        }
    } else {
        // Magnifier-only mode (pre-selection): use dirty region for magnifier
        if (m_magnifierUpdateTimer.elapsed() >= MAGNIFIER_MIN_UPDATE_MS) {
            m_magnifierUpdateTimer.restart();
            // Calculate magnifier panel rect and update only that region
            const int panelWidth = 180;
            const int totalHeight = MAGNIFIER_SIZE + 75;
            int panelX = m_currentPoint.x() - panelWidth / 2;
            int panelY = m_currentPoint.y() + 25;
            panelX = qMax(10, qMin(panelX, width() - panelWidth - 10));
            if (panelY + totalHeight > height()) {
                panelY = m_currentPoint.y() - totalHeight - 25;
            }
            QRect currentMagRect(panelX - 5, panelY - 5, panelWidth + 10, totalHeight + 10);
            QRect dirtyRect = m_lastMagnifierRect.united(currentMagRect);
            // Also include crosshair area
            dirtyRect = dirtyRect.united(QRect(0, m_currentPoint.y() - 1, width(), 3));
            dirtyRect = dirtyRect.united(QRect(m_currentPoint.x() - 1, 0, 3, height()));
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
        if (m_isTransformingAnnotation) {
            finishTextTransformation();
            return;
        }

        // Handle text annotation drag release
        if (m_isDraggingAnnotation) {
            m_isDraggingAnnotation = false;
            return;
        }

        if (m_isSelecting) {
            m_isSelecting = false;

            QRect sel = m_selectionRect.normalized();
            if (sel.width() > 5 && sel.height() > 5) {
                // 有拖曳 - 使用拖曳選取的區域
                m_selectionComplete = true;
                setCursor(Qt::ArrowCursor);
                qDebug() << "RegionSelector: Selection complete via drag";
            }
            else if (m_detectedWindow.has_value() && m_highlightedWindowRect.isValid()) {
                // Click on detected window - use its bounds
                m_selectionRect = m_highlightedWindowRect;
                m_selectionComplete = true;
                setCursor(Qt::ArrowCursor);
                qDebug() << "RegionSelector: Selection complete via detected window:" << m_detectedWindow->windowTitle;

                // Clear detection state
                m_highlightedWindowRect = QRect();
                m_detectedWindow.reset();
            }
            else {
                // 點擊無拖曳且無偵測到視窗 - 選取整個螢幕（包含 menu bar）
                QRect screenGeom = m_currentScreen->geometry();
                m_selectionRect = QRect(0, 0, screenGeom.width(), screenGeom.height());
                m_selectionComplete = true;
                setCursor(Qt::ArrowCursor);
                qDebug() << "RegionSelector: Click without drag - selecting full screen (including menu bar)";
            }

            update();
        }
        else if (m_isResizing || m_isMoving) {
            // Finish resize/move
            m_isResizing = false;
            m_isMoving = false;
            m_activeHandle = ResizeHandle::None;
            m_selectionRect = m_selectionRect.normalized();
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

void RegionSelector::wheelEvent(QWheelEvent *event)
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
        } else {
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
        if (m_selectionComplete) {
            finishSelection();
        }
    }
    else if (event->matches(QKeySequence::Copy)) {
        if (m_selectionComplete) {
            copyToClipboard();
        }
    }
    else if (event->matches(QKeySequence::Save)) {
        if (m_selectionComplete) {
            saveToFile();
        }
    }
    else if (event->key() == Qt::Key_R && !event->modifiers()) {
        if (m_selectionComplete) {
            handleToolbarClick(ToolbarButton::Record);
        }
    }
    else if (event->key() == Qt::Key_Shift) {
        // 切換 RGB/HEX 顯示格式
        m_showHexColor = !m_showHexColor;
        update();
    }
    else if (event->key() == Qt::Key_C && !m_selectionComplete) {
        // 複製顏色到剪貼板 (僅在未選取完成時有效)
        int deviceX = static_cast<int>(m_currentPoint.x() * m_devicePixelRatio);
        int deviceY = static_cast<int>(m_currentPoint.y() * m_devicePixelRatio);
        const QImage &img = m_backgroundImageCache;
        if (deviceX >= 0 && deviceX < img.width() && deviceY >= 0 && deviceY < img.height()) {
            QColor pixelColor = QColor(img.pixel(deviceX, deviceY));
            QString colorText;
            if (m_showHexColor) {
                colorText = QString("#%1%2%3")
                    .arg(pixelColor.red(), 2, 16, QChar('0'))
                    .arg(pixelColor.green(), 2, 16, QChar('0'))
                    .arg(pixelColor.blue(), 2, 16, QChar('0'));
            }
            else {
                colorText = QString("rgb(%1, %2, %3)")
                    .arg(pixelColor.red())
                    .arg(pixelColor.green())
                    .arg(pixelColor.blue());
            }
            QGuiApplication::clipboard()->setText(colorText);
            qDebug() << "RegionSelector: Copied color to clipboard:" << colorText;
        }
    }
    else if (event->matches(QKeySequence::Undo)) {
        if (m_selectionComplete && m_annotationLayer->canUndo()) {
            m_annotationLayer->undo();
            update();
        }
    }
    else if (event->matches(QKeySequence::Redo)) {
        if (m_selectionComplete && m_annotationLayer->canRedo()) {
            m_annotationLayer->redo();
            update();
        }
    }
    else if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        // Delete selected text annotation
        if (m_selectionComplete && m_annotationLayer->selectedIndex() >= 0) {
            m_annotationLayer->removeSelectedItem();
            update();
        }
    }
    // Arrow keys for precise selection adjustment
    else if (m_selectionComplete && !m_selectionRect.isEmpty()) {
        bool handled = true;

        if (event->modifiers() & Qt::ShiftModifier) {
            // Shift + Arrow: Resize selection (adjust corresponding edge)
            QRect newRect = m_selectionRect;
            switch (event->key()) {
            case Qt::Key_Left:  newRect.setRight(newRect.right() - 1); break;
            case Qt::Key_Right: newRect.setRight(newRect.right() + 1); break;
            case Qt::Key_Up:    newRect.setBottom(newRect.bottom() - 1); break;
            case Qt::Key_Down:  newRect.setBottom(newRect.bottom() + 1); break;
            default: handled = false; break;
            }
            if (handled && newRect.width() >= 10 && newRect.height() >= 10) {
                m_selectionRect = newRect;
                update();
            }
        } else {
            // Arrow only: Move selection
            switch (event->key()) {
            case Qt::Key_Left:  m_selectionRect.translate(-1, 0); break;
            case Qt::Key_Right: m_selectionRect.translate(1, 0);  break;
            case Qt::Key_Up:    m_selectionRect.translate(0, -1); break;
            case Qt::Key_Down:  m_selectionRect.translate(0, 1);  break;
            default: handled = false; break;
            }
            if (handled) update();
        }
    }
}

void RegionSelector::closeEvent(QCloseEvent *event)
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
    } else if (event->type() == QEvent::ApplicationDeactivate) {
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

void RegionSelector::drawAnnotations(QPainter &painter)
{
    m_annotationLayer->draw(painter);

    // Draw transformation gizmo for selected text annotation
    int selIdx = m_annotationLayer->selectedIndex();
    if (selIdx >= 0) {
        if (auto* textItem = dynamic_cast<TextAnnotation*>(m_annotationLayer->selectedItem())) {
            TransformationGizmo::draw(painter, textItem);
        }
    }
}

void RegionSelector::drawCurrentAnnotation(QPainter &painter)
{
    if (!m_isDrawing) return;

    if (m_currentPencil) {
        m_currentPencil->draw(painter);
    }
    else if (m_currentMarker) {
        m_currentMarker->draw(painter);
    }
    else if (m_currentArrow) {
        // Only draw arrow if mouse has moved sufficiently from start point
        QPoint delta = m_currentArrow->end() - m_currentArrow->start();
        if (delta.manhattanLength() >= 3) {
            m_currentArrow->draw(painter);
        }
    }
    else if (m_currentRectangle) {
        m_currentRectangle->draw(painter);
    }
    else if (m_currentEllipse) {
        m_currentEllipse->draw(painter);
    }
    else if (m_currentMosaicStroke) {
        m_currentMosaicStroke->draw(painter);
    }
    else if (m_currentTool == ToolbarButton::Eraser && !m_eraserPath.isEmpty()) {
        // Draw eraser cursor at the last position
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);

        QPoint lastPos = m_eraserPath.last();
        int radius = ERASER_WIDTH / 2;

        // Draw semi-transparent circle
        painter.setPen(QPen(Qt::white, 2));
        painter.setBrush(QColor(255, 255, 255, 50));
        painter.drawEllipse(lastPos, radius, radius);

        painter.restore();
    }
}

void RegionSelector::startAnnotation(const QPoint &pos)
{
    m_isDrawing = true;
    m_drawStartPoint = pos;
    m_currentPath.clear();
    m_currentPath.append(QPointF(pos));

    switch (m_currentTool) {
    case ToolbarButton::Pencil:
        m_currentPencil = std::make_unique<PencilStroke>(m_currentPath, m_annotationColor, m_annotationWidth);
        break;

    case ToolbarButton::Marker:
        m_currentMarker = std::make_unique<MarkerStroke>(m_currentPath, m_annotationColor, 20);
        break;

    case ToolbarButton::Arrow:
        m_currentArrow = std::make_unique<ArrowAnnotation>(pos, pos, m_annotationColor, m_annotationWidth, m_arrowStyle);
        break;

    case ToolbarButton::Rectangle:
        m_currentRectangle = std::make_unique<RectangleAnnotation>(QRect(pos, pos), m_annotationColor, m_annotationWidth);
        break;

    case ToolbarButton::Ellipse:
        m_currentEllipse = std::make_unique<EllipseAnnotation>(QRect(pos, pos), m_annotationColor, m_annotationWidth);
        break;

    case ToolbarButton::Mosaic: {
        QVector<QPoint> intPath;
        for (const QPointF &p : m_currentPath) {
            intPath.append(p.toPoint());
        }
        MosaicStrength strength = m_colorAndWidthWidget->mosaicStrength();
        m_currentMosaicStroke = std::make_unique<MosaicStroke>(intPath, m_backgroundPixmap, 15, 5, strength);
        break;
    }

    case ToolbarButton::Eraser:
        m_eraserPath.clear();
        m_eraserPath.append(pos);
        m_erasedItems.clear();
        // Immediately check for items to erase at start position
        {
            auto removed = m_annotationLayer->removeItemsIntersecting(pos, ERASER_WIDTH);
            for (auto &item : removed) {
                m_erasedItems.push_back(std::move(item));
            }
        }
        break;

    default:
        m_isDrawing = false;
        break;
    }

    update();
}

void RegionSelector::updateAnnotation(const QPoint &pos)
{
    if (!m_isDrawing) return;

    switch (m_currentTool) {
    case ToolbarButton::Pencil:
        if (m_currentPencil) {
            m_currentPath.append(QPointF(pos));
            m_currentPencil->addPoint(QPointF(pos));
        }
        break;

    case ToolbarButton::Marker:
        if (m_currentMarker) {
            m_currentPath.append(QPointF(pos));
            m_currentMarker->addPoint(QPointF(pos));
        }
        break;

    case ToolbarButton::Arrow:
        if (m_currentArrow) {
            m_currentArrow->setEnd(pos);
        }
        break;

    case ToolbarButton::Rectangle:
        if (m_currentRectangle) {
            m_currentRectangle->setRect(QRect(m_drawStartPoint, pos));
        }
        break;

    case ToolbarButton::Ellipse:
        if (m_currentEllipse) {
            m_currentEllipse->setRect(QRect(m_drawStartPoint, pos));
        }
        break;

    case ToolbarButton::Mosaic:
        if (m_currentMosaicStroke) {
            m_currentPath.append(QPointF(pos));
            m_currentMosaicStroke->addPoint(pos);  // MosaicStroke still uses QPoint
        }
        break;

    case ToolbarButton::Eraser:
        m_eraserPath.append(pos);
        // Check for items to erase at this position
        {
            auto removed = m_annotationLayer->removeItemsIntersecting(pos, ERASER_WIDTH);
            for (auto &item : removed) {
                m_erasedItems.push_back(std::move(item));
            }
        }
        break;

    default:
        break;
    }

    update();
}

void RegionSelector::finishAnnotation()
{
    if (!m_isDrawing) return;

    m_isDrawing = false;

    // Add the annotation to the layer
    switch (m_currentTool) {
    case ToolbarButton::Pencil:
        if (m_currentPencil && m_currentPath.size() > 1) {
            m_annotationLayer->addItem(std::move(m_currentPencil));
        }
        m_currentPencil.reset();
        break;

    case ToolbarButton::Marker:
        if (m_currentMarker && m_currentPath.size() > 1) {
            m_annotationLayer->addItem(std::move(m_currentMarker));
        }
        m_currentMarker.reset();
        break;

    case ToolbarButton::Arrow:
        // Only save arrow if mouse has moved sufficiently from start point
        if (m_currentArrow) {
            QPoint delta = m_currentArrow->end() - m_currentArrow->start();
            if (delta.manhattanLength() >= 3) {
                m_annotationLayer->addItem(std::move(m_currentArrow));
            }
        }
        m_currentArrow.reset();
        break;

    case ToolbarButton::Rectangle:
        if (m_currentRectangle) {
            m_annotationLayer->addItem(std::move(m_currentRectangle));
        }
        m_currentRectangle.reset();
        break;

    case ToolbarButton::Ellipse:
        if (m_currentEllipse) {
            m_annotationLayer->addItem(std::move(m_currentEllipse));
        }
        m_currentEllipse.reset();
        break;

    case ToolbarButton::Mosaic:
        if (m_currentMosaicStroke && m_currentPath.size() > 1) {
            m_annotationLayer->addItem(std::move(m_currentMosaicStroke));
        }
        m_currentMosaicStroke.reset();
        break;

    case ToolbarButton::Eraser:
        // If items were erased, add an ErasedItemsGroup for undo support
        if (!m_erasedItems.empty()) {
            auto erasedGroup = std::make_unique<ErasedItemsGroup>(std::move(m_erasedItems));
            m_annotationLayer->addItem(std::move(erasedGroup));
        }
        m_eraserPath.clear();
        m_erasedItems.clear();
        break;

    default:
        break;
    }

    m_currentPath.clear();
}

bool RegionSelector::isAnnotationTool(ToolbarButton tool) const
{
    switch (tool) {
    case ToolbarButton::Pencil:
    case ToolbarButton::Marker:
    case ToolbarButton::Arrow:
    case ToolbarButton::Rectangle:
    case ToolbarButton::Ellipse:
    case ToolbarButton::Text:
    case ToolbarButton::Mosaic:
    case ToolbarButton::StepBadge:
    case ToolbarButton::Eraser:
        return true;
    default:
        return false;
    }
}

void RegionSelector::showTextInputDialog(const QPoint &pos)
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

void RegionSelector::placeStepBadge(const QPoint &pos)
{
    int nextNumber = m_annotationLayer->countStepBadges() + 1;
    auto badge = std::make_unique<StepBadgeAnnotation>(pos, m_annotationColor, nextNumber);
    m_annotationLayer->addItem(std::move(badge));
    qDebug() << "Placed step badge" << nextNumber << "at" << pos;
    update();
}

void RegionSelector::onTextEditingFinished(const QString &text, const QPoint &position)
{
    QFont font = m_textFormatting.toQFont();

    if (m_editingTextIndex >= 0) {
        // Re-editing: restore visibility first
        auto* textItem = dynamic_cast<TextAnnotation*>(m_annotationLayer->itemAt(m_editingTextIndex));
        if (textItem) {
            textItem->setVisible(true);  // Restore visibility
            if (!text.isEmpty()) {
                textItem->setText(text);
                textItem->setFont(font);
                textItem->setColor(m_annotationColor);
                // Position may have changed during confirm mode drag
                textItem->setPosition(position);
            }
        }
        m_annotationLayer->setSelectedIndex(m_editingTextIndex);
        m_editingTextIndex = -1;
    } else if (!text.isEmpty()) {
        // Create new annotation
        auto textAnnotation = std::make_unique<TextAnnotation>(position, text, font, m_annotationColor);
        m_annotationLayer->addItem(std::move(textAnnotation));

        // Auto-select the newly created text annotation to show the gizmo
        int newIndex = static_cast<int>(m_annotationLayer->itemCount()) - 1;
        m_annotationLayer->setSelectedIndex(newIndex);
    }

    update();
}

void RegionSelector::startTextReEditing(int annotationIndex)
{
    auto* textItem = dynamic_cast<TextAnnotation*>(m_annotationLayer->itemAt(annotationIndex));
    if (!textItem) return;

    m_editingTextIndex = annotationIndex;

    // Extract formatting from existing annotation
    m_textFormatting = TextFormattingState::fromQFont(textItem->font());
    m_annotationColor = textItem->color();

    // Update UI to reflect current formatting
    m_colorAndWidthWidget->setCurrentColor(m_annotationColor);
    m_colorAndWidthWidget->setBold(m_textFormatting.bold);
    m_colorAndWidthWidget->setItalic(m_textFormatting.italic);
    m_colorAndWidthWidget->setUnderline(m_textFormatting.underline);
    m_colorAndWidthWidget->setFontSize(m_textFormatting.fontSize);
    m_colorAndWidthWidget->setFontFamily(m_textFormatting.fontFamily);

    // Start editor with existing text
    m_textEditor->setColor(m_annotationColor);
    m_textEditor->setFont(m_textFormatting.toQFont());
    m_textEditor->startEditingExisting(textItem->position(), m_selectionRect.normalized(), textItem->text());

    // Hide the original annotation while editing (prevent duplicate display)
    textItem->setVisible(false);

    // Clear selection to hide gizmo while editing
    m_annotationLayer->clearSelection();
    update();
}

// ============================================================================
// Text Annotation Transformation Helper Functions
// ============================================================================

void RegionSelector::startTextTransformation(const QPoint &pos, GizmoHandle handle)
{
    auto* textItem = dynamic_cast<TextAnnotation*>(m_annotationLayer->selectedItem());
    if (!textItem) return;

    m_isTransformingAnnotation = true;
    m_activeGizmoHandle = handle;
    m_transformStartCenter = textItem->center();
    m_transformStartRotation = textItem->rotation();
    m_transformStartScale = textItem->scale();

    // Calculate initial angle and distance from center to mouse
    QPointF delta = QPointF(pos) - m_transformStartCenter;
    m_transformStartAngle = qRadiansToDegrees(qAtan2(delta.y(), delta.x()));
    m_transformStartDistance = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());

    m_annotationDragStart = pos;
}

void RegionSelector::updateTextTransformation(const QPoint &pos)
{
    auto* textItem = dynamic_cast<TextAnnotation*>(m_annotationLayer->selectedItem());
    if (!textItem) return;

    QPointF center = m_transformStartCenter;
    QPointF delta = QPointF(pos) - center;

    switch (m_activeGizmoHandle) {
    case GizmoHandle::Rotation: {
        // Calculate new angle from center to mouse
        qreal currentAngle = qRadiansToDegrees(qAtan2(delta.y(), delta.x()));
        qreal angleDelta = currentAngle - m_transformStartAngle;

        // Apply rotation
        textItem->setRotation(m_transformStartRotation + angleDelta);
        break;
    }

    case GizmoHandle::TopLeft:
    case GizmoHandle::TopRight:
    case GizmoHandle::BottomLeft:
    case GizmoHandle::BottomRight: {
        // Calculate scale based on distance from center
        qreal currentDistance = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());

        if (m_transformStartDistance > 0) {
            qreal scaleFactor = currentDistance / m_transformStartDistance;
            // Clamp scale to reasonable range (10% to 1000%)
            scaleFactor = qBound(0.1, m_transformStartScale * scaleFactor, 10.0);
            textItem->setScale(scaleFactor);
        }
        break;
    }

    case GizmoHandle::Body: {
        // Move the annotation (this case is normally handled by m_isDraggingAnnotation)
        QPoint moveD = pos - m_annotationDragStart;
        textItem->moveBy(moveD);
        m_annotationDragStart = pos;
        break;
    }

    default:
        break;
    }
}

void RegionSelector::finishTextTransformation()
{
    m_isTransformingAnnotation = false;
    m_activeGizmoHandle = GizmoHandle::None;
}

// ============================================================================
// Selection Resize/Move Helper Functions
// ============================================================================

ResizeHandle RegionSelector::getHandleAtPosition(const QPoint &pos)
{
    if (!m_selectionComplete) return ResizeHandle::None;

    QRect sel = m_selectionRect.normalized();
    const int handleSize = 16;  // Hit area for handles

    // Define handle hit areas
    QRect topLeft(sel.left() - handleSize/2, sel.top() - handleSize/2, handleSize, handleSize);
    QRect top(sel.center().x() - handleSize/2, sel.top() - handleSize/2, handleSize, handleSize);
    QRect topRight(sel.right() - handleSize/2, sel.top() - handleSize/2, handleSize, handleSize);
    QRect left(sel.left() - handleSize/2, sel.center().y() - handleSize/2, handleSize, handleSize);
    QRect right(sel.right() - handleSize/2, sel.center().y() - handleSize/2, handleSize, handleSize);
    QRect bottomLeft(sel.left() - handleSize/2, sel.bottom() - handleSize/2, handleSize, handleSize);
    QRect bottom(sel.center().x() - handleSize/2, sel.bottom() - handleSize/2, handleSize, handleSize);
    QRect bottomRight(sel.right() - handleSize/2, sel.bottom() - handleSize/2, handleSize, handleSize);

    // Check corners first (higher priority)
    if (topLeft.contains(pos)) return ResizeHandle::TopLeft;
    if (topRight.contains(pos)) return ResizeHandle::TopRight;
    if (bottomLeft.contains(pos)) return ResizeHandle::BottomLeft;
    if (bottomRight.contains(pos)) return ResizeHandle::BottomRight;

    // Check edges
    if (top.contains(pos)) return ResizeHandle::Top;
    if (bottom.contains(pos)) return ResizeHandle::Bottom;
    if (left.contains(pos)) return ResizeHandle::Left;
    if (right.contains(pos)) return ResizeHandle::Right;

    // Check if inside selection (for move)
    if (sel.contains(pos)) return ResizeHandle::Center;

    return ResizeHandle::None;
}

void RegionSelector::updateResize(const QPoint &pos)
{
    QPoint delta = pos - m_resizeStartPoint;
    QRect newRect = m_originalRect;

    switch (m_activeHandle) {
    case ResizeHandle::TopLeft:
        newRect.setTopLeft(m_originalRect.topLeft() + delta);
        break;
    case ResizeHandle::Top:
        newRect.setTop(m_originalRect.top() + delta.y());
        break;
    case ResizeHandle::TopRight:
        newRect.setTopRight(m_originalRect.topRight() + delta);
        break;
    case ResizeHandle::Left:
        newRect.setLeft(m_originalRect.left() + delta.x());
        break;
    case ResizeHandle::Right:
        newRect.setRight(m_originalRect.right() + delta.x());
        break;
    case ResizeHandle::BottomLeft:
        newRect.setBottomLeft(m_originalRect.bottomLeft() + delta);
        break;
    case ResizeHandle::Bottom:
        newRect.setBottom(m_originalRect.bottom() + delta.y());
        break;
    case ResizeHandle::BottomRight:
        newRect.setBottomRight(m_originalRect.bottomRight() + delta);
        break;
    default:
        break;
    }

    // Ensure minimum size
    if (newRect.width() >= 10 && newRect.height() >= 10) {
        m_selectionRect = newRect;
    }
}

void RegionSelector::updateCursorForHandle(ResizeHandle handle)
{
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
    case ResizeHandle::Center:
        setCursor(Qt::SizeAllCursor);
        break;
    default:
        setCursor(Qt::ArrowCursor);
        break;
    }
}

void RegionSelector::performOCR()
{
    if (!m_ocrManager || m_ocrInProgress || !m_selectionComplete) {
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
    QRect sel = m_selectionRect.normalized();
    QPixmap selectedRegion = m_backgroundPixmap.copy(
        static_cast<int>(sel.x() * m_devicePixelRatio),
        static_cast<int>(sel.y() * m_devicePixelRatio),
        static_cast<int>(sel.width() * m_devicePixelRatio),
        static_cast<int>(sel.height() * m_devicePixelRatio)
    );
    selectedRegion.setDevicePixelRatio(m_devicePixelRatio);

    QPointer<RegionSelector> safeThis = this;
    m_ocrManager->recognizeText(selectedRegion,
        [safeThis](bool success, const QString &text, const QString &error) {
            if (safeThis) {
                safeThis->onOCRComplete(success, text, error);
            } else {
                qDebug() << "RegionSelector: OCR completed but widget was destroyed, ignoring result";
            }
        });
}

void RegionSelector::onOCRComplete(bool success, const QString &text, const QString &error)
{
    m_ocrInProgress = false;
    m_loadingSpinner->stop();

    QString msg;
    if (success && !text.isEmpty()) {
        QGuiApplication::clipboard()->setText(text);
        qDebug() << "RegionSelector: OCR complete, copied" << text.length() << "characters to clipboard";
        msg = tr("Copied %1 characters").arg(text.length());

        // Show success toast with green background
        m_ocrToastLabel->setStyleSheet(
            "QLabel {"
            "  background-color: rgba(34, 139, 34, 220);"
            "  color: white;"
            "  padding: 8px 16px;"
            "  border-radius: 6px;"
            "  font-size: 13px;"
            "  font-weight: bold;"
            "}"
        );
    } else {
        msg = error.isEmpty() ? tr("No text found") : error;
        qDebug() << "RegionSelector: OCR failed:" << msg;

        // Show failure toast with red background
        m_ocrToastLabel->setStyleSheet(
            "QLabel {"
            "  background-color: rgba(200, 60, 60, 220);"
            "  color: white;"
            "  padding: 8px 16px;"
            "  border-radius: 6px;"
            "  font-size: 13px;"
            "  font-weight: bold;"
            "}"
        );
    }

    // Display the toast centered at top of selection area
    m_ocrToastLabel->setText(msg);
    m_ocrToastLabel->adjustSize();
    int x = m_selectionRect.center().x() - m_ocrToastLabel->width() / 2;
    int y = m_selectionRect.top() + 12;
    m_ocrToastLabel->move(x, y);
    m_ocrToastLabel->show();
    m_ocrToastLabel->raise();
    m_ocrToastTimer->start(2500);

    update();
}

QColor RegionSelector::loadAnnotationColor() const
{
    QSettings settings("Victor Fu", "SnapTray");
    return settings.value(SETTINGS_KEY_ANNOTATION_COLOR, QColor(Qt::red)).value<QColor>();
}

void RegionSelector::saveAnnotationColor(const QColor &color)
{
    QSettings settings("Victor Fu", "SnapTray");
    settings.setValue(SETTINGS_KEY_ANNOTATION_COLOR, color);
}

int RegionSelector::loadAnnotationWidth() const
{
    QSettings settings("Victor Fu", "SnapTray");
    return settings.value(SETTINGS_KEY_ANNOTATION_WIDTH, 3).toInt();
}

void RegionSelector::saveAnnotationWidth(int width)
{
    QSettings settings("Victor Fu", "SnapTray");
    settings.setValue(SETTINGS_KEY_ANNOTATION_WIDTH, width);
}

LineEndStyle RegionSelector::loadArrowStyle() const
{
    QSettings settings("Victor Fu", "SnapTray");
    int style = settings.value("annotation/arrowStyle", static_cast<int>(LineEndStyle::EndArrow)).toInt();
    return static_cast<LineEndStyle>(style);
}

void RegionSelector::saveArrowStyle(LineEndStyle style)
{
    QSettings settings("Victor Fu", "SnapTray");
    settings.setValue("annotation/arrowStyle", static_cast<int>(style));
}

void RegionSelector::onArrowStyleChanged(LineEndStyle style)
{
    m_arrowStyle = style;
    saveArrowStyle(style);
    update();
}

TextFormattingState RegionSelector::loadTextFormatting() const
{
    QSettings settings("Victor Fu", "SnapTray");
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
    QSettings settings("Victor Fu", "SnapTray");
    settings.setValue(SETTINGS_KEY_TEXT_BOLD, m_textFormatting.bold);
    settings.setValue(SETTINGS_KEY_TEXT_ITALIC, m_textFormatting.italic);
    settings.setValue(SETTINGS_KEY_TEXT_UNDERLINE, m_textFormatting.underline);
    settings.setValue(SETTINGS_KEY_TEXT_SIZE, m_textFormatting.fontSize);
    settings.setValue(SETTINGS_KEY_TEXT_FAMILY, m_textFormatting.fontFamily);
}

void RegionSelector::onBoldToggled(bool enabled)
{
    m_textFormatting.bold = enabled;
    saveTextFormatting();
    if (m_textEditor->isEditing()) {
        m_textEditor->setBold(enabled);
    }
    update();
}

void RegionSelector::onItalicToggled(bool enabled)
{
    m_textFormatting.italic = enabled;
    saveTextFormatting();
    if (m_textEditor->isEditing()) {
        m_textEditor->setItalic(enabled);
    }
    update();
}

void RegionSelector::onUnderlineToggled(bool enabled)
{
    m_textFormatting.underline = enabled;
    saveTextFormatting();
    if (m_textEditor->isEditing()) {
        m_textEditor->setUnderline(enabled);
    }
    update();
}

void RegionSelector::onFontSizeDropdownRequested(const QPoint& pos)
{
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background: #2d2d2d; color: white; border: 1px solid #3d3d3d; } "
        "QMenu::item { padding: 4px 20px; } "
        "QMenu::item:selected { background: #0078d4; }"
    );

    static const int sizes[] = {8, 10, 12, 14, 16, 18, 20, 24, 28, 32, 36, 48, 72};
    for (int size : sizes) {
        QAction* action = menu.addAction(QString::number(size));
        action->setCheckable(true);
        action->setChecked(size == m_textFormatting.fontSize);
        connect(action, &QAction::triggered, this, [this, size]() {
            m_textFormatting.fontSize = size;
            m_colorAndWidthWidget->setFontSize(size);
            saveTextFormatting();
            if (m_textEditor->isEditing()) {
                m_textEditor->setFontSize(size);
            }
            update();
        });
    }
    menu.exec(mapToGlobal(pos));
}

void RegionSelector::onFontFamilyDropdownRequested(const QPoint& pos)
{
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background: #2d2d2d; color: white; border: 1px solid #3d3d3d; } "
        "QMenu::item { padding: 4px 20px; } "
        "QMenu::item:selected { background: #0078d4; }"
    );

    // Add "Default" option
    QAction* defaultAction = menu.addAction(tr("Default"));
    defaultAction->setCheckable(true);
    defaultAction->setChecked(m_textFormatting.fontFamily.isEmpty());
    connect(defaultAction, &QAction::triggered, this, [this]() {
        m_textFormatting.fontFamily = QString();
        m_colorAndWidthWidget->setFontFamily(QString());
        saveTextFormatting();
        if (m_textEditor->isEditing()) {
            m_textEditor->setFontFamily(QString());
        }
        update();
    });

    menu.addSeparator();

    // Add common font families
    QStringList families = QFontDatabase::families();
    // Limit to common fonts to avoid overwhelming menu
    QStringList commonFonts = {"Arial", "Helvetica", "Times New Roman", "Courier New",
                               "Verdana", "Georgia", "Trebuchet MS", "Impact"};
    for (const QString& family : commonFonts) {
        if (families.contains(family)) {
            QAction* action = menu.addAction(family);
            action->setCheckable(true);
            action->setChecked(family == m_textFormatting.fontFamily);
            connect(action, &QAction::triggered, this, [this, family]() {
                m_textFormatting.fontFamily = family;
                m_colorAndWidthWidget->setFontFamily(family);
                saveTextFormatting();
                if (m_textEditor->isEditing()) {
                    m_textEditor->setFontFamily(family);
                }
                update();
            });
        }
    }

    menu.exec(mapToGlobal(pos));
}
