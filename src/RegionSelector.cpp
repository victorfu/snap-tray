#include "RegionSelector.h"
#include "platform/WindowLevel.h"
#include "region/RegionPainter.h"
#include "region/RegionInputHandler.h"
#include "region/RegionToolbarHandler.h"
#include "region/RegionSettingsHelper.h"
#include "region/RegionExportManager.h"
#include "annotations/AllAnnotations.h"
#include "cursor/CursorManager.h"
#include "GlassRenderer.h"
#include "ToolbarStyle.h"
#include "IconRenderer.h"
#include "toolbar/ToolOptionsPanel.h"
#include "ColorPickerDialog.h"
#include "EmojiPicker.h"
#include "OCRManager.h"
#include "PlatformFeatures.h"
#include "detection/AutoBlurManager.h"
#include "settings/AnnotationSettingsManager.h"
#include "settings/FileSettingsManager.h"
#include "settings/OCRSettingsManager.h"
#include "tools/handlers/MosaicToolHandler.h"
#include "tools/handlers/EmojiStickerToolHandler.h"
#include "tools/ToolSectionConfig.h"
#include <QTextEdit>

#include <cstring>
#include <map>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QShowEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QDebug>
#include <QCursor>
#include <QClipboard>
#include <QFileDialog>
#include <QMessageBox>
#include <QAbstractButton>
#include <QPushButton>
#include <QStandardPaths>
#include <QDateTime>
#include <numeric>
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
#include "tools/ToolRegistry.h"

RegionSelector::RegionSelector(QWidget* parent)
    : QWidget(parent)
    , m_currentScreen(nullptr)
    , m_selectionManager(nullptr)
    , m_devicePixelRatio(1.0)
    , m_toolbar(nullptr)
    , m_annotationLayer(nullptr)
    , m_currentTool(ToolId::Selection)
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
    , m_emojiPicker(nullptr)
    , m_textEditor(nullptr)
    , m_colorPickerDialog(nullptr)
    , m_magnifierPanel(nullptr)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);

    // Initialize selection state manager
    m_selectionManager = new SelectionStateManager(this);
    connect(m_selectionManager, &SelectionStateManager::selectionChanged,
        this, [this](const QRect& rect) {
            if (m_multiRegionMode && m_multiRegionManager) {
                int activeIndex = m_multiRegionManager->activeIndex();
                if (activeIndex >= 0) {
                    m_multiRegionManager->updateRegion(activeIndex, rect);
                }
            }
            update();
        });
    connect(m_selectionManager, &SelectionStateManager::stateChanged,
        this, [this](SelectionStateManager::State newState) {
            // Skip automatic cursor updates in multi-region mode
            // (multi-region mode handles its own cursor logic)
            if (m_multiRegionMode) {
                return;
            }
            // Update cursor based on state (single-region mode only)
            auto& cm = CursorManager::instance();
            if (newState == SelectionStateManager::State::None ||
                newState == SelectionStateManager::State::Selecting) {
                cm.pushCursorForWidget(this, CursorContext::Selection, Qt::CrossCursor);
            }
            else if (newState == SelectionStateManager::State::Moving) {
                cm.pushCursorForWidget(this, CursorContext::Selection, Qt::SizeAllCursor);
            }
            else {
                cm.popCursorForWidget(this, CursorContext::Selection);
            }
        });

    // Initialize multi-region manager
    m_multiRegionManager = new MultiRegionManager(this);
    connect(m_multiRegionManager, &MultiRegionManager::regionAdded,
        this, [this](int) { update(); });
    connect(m_multiRegionManager, &MultiRegionManager::regionRemoved,
        this, [this](int) { update(); });
    connect(m_multiRegionManager, &MultiRegionManager::regionUpdated,
        this, [this](int) { update(); });
    connect(m_multiRegionManager, &MultiRegionManager::activeIndexChanged,
        this, [this](int index) {
            if (!m_multiRegionMode) return;
            if (index >= 0) {
                m_selectionManager->setSelectionRect(m_multiRegionManager->regionRect(index));
            }
            else if (!m_selectionManager->isSelecting()) {
                m_selectionManager->clearSelection();
            }
            update();
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

    // Initialize cursor manager for centralized cursor handling
    auto& cursorManager = CursorManager::instance();
    cursorManager.registerWidget(this, m_toolManager);

    // OCR manager is lazy-initialized via ensureOCRManager() when first needed

    // Initialize auto-blur manager (lazy initialization - cascade loaded on first use)
    m_autoBlurManager = new AutoBlurManager(this);

    // Initialize toolbar widget
    m_toolbar = new ToolbarCore(this);
    connect(m_toolbar, &ToolbarCore::buttonClicked, this, [this](int buttonId) {
        handleToolbarClick(static_cast<ToolId>(buttonId));
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

    // Initialize unified color and width widget
    m_colorAndWidthWidget = new ToolOptionsPanel(this);
    m_colorAndWidthWidget->setCurrentColor(m_annotationColor);
    m_colorAndWidthWidget->setCurrentWidth(m_annotationWidth);
    m_colorAndWidthWidget->setWidthRange(1, 20);
    connect(m_colorAndWidthWidget, &ToolOptionsPanel::colorSelected,
        this, &RegionSelector::onColorSelected);
    connect(m_colorAndWidthWidget, &ToolOptionsPanel::customColorPickerRequested,
        this, &RegionSelector::onMoreColorsRequested);
    connect(m_colorAndWidthWidget, &ToolOptionsPanel::widthChanged,
        this, &RegionSelector::onLineWidthChanged);
    // Mosaic now uses widthChanged signal like other tools (no separate mosaicWidthChanged)

    // Configure text annotation editor with ToolOptionsPanel
    m_textAnnotationEditor->setColorAndWidthWidget(m_colorAndWidthWidget);

    // Load text formatting settings from TextAnnotationEditor
    TextFormattingState textFormatting = m_textAnnotationEditor->formatting();
    m_colorAndWidthWidget->setBold(textFormatting.bold);
    m_colorAndWidthWidget->setItalic(textFormatting.italic);
    m_colorAndWidthWidget->setUnderline(textFormatting.underline);
    m_colorAndWidthWidget->setFontSize(textFormatting.fontSize);
    m_colorAndWidthWidget->setFontFamily(textFormatting.fontFamily);

    // Connect text formatting signals to TextAnnotationEditor
    connect(m_colorAndWidthWidget, &ToolOptionsPanel::boldToggled,
        m_textAnnotationEditor, &TextAnnotationEditor::setBold);
    connect(m_colorAndWidthWidget, &ToolOptionsPanel::italicToggled,
        m_textAnnotationEditor, &TextAnnotationEditor::setItalic);
    connect(m_colorAndWidthWidget, &ToolOptionsPanel::underlineToggled,
        m_textAnnotationEditor, &TextAnnotationEditor::setUnderline);
    connect(m_colorAndWidthWidget, &ToolOptionsPanel::fontSizeDropdownRequested,
        this, &RegionSelector::onFontSizeDropdownRequested);
    connect(m_colorAndWidthWidget, &ToolOptionsPanel::fontFamilyDropdownRequested,
        this, &RegionSelector::onFontFamilyDropdownRequested);

    // Connect arrow style signal
    m_colorAndWidthWidget->setArrowStyle(m_arrowStyle);
    connect(m_colorAndWidthWidget, &ToolOptionsPanel::arrowStyleChanged,
        this, &RegionSelector::onArrowStyleChanged);

    // Connect line style signal
    m_colorAndWidthWidget->setLineStyle(m_lineStyle);
    connect(m_colorAndWidthWidget, &ToolOptionsPanel::lineStyleChanged,
        this, &RegionSelector::onLineStyleChanged);

    // Connect shape section signals
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
        this, &RegionSelector::onStepBadgeSizeChanged);
    connect(m_colorAndWidthWidget, &ToolOptionsPanel::autoBlurRequested,
        this, &RegionSelector::performAutoBlur);

    // EmojiPicker is lazy-initialized via ensureEmojiPicker() when first needed
    // LoadingSpinner is lazy-initialized via ensureLoadingSpinner() when first needed

    // OCR toast label (shows success/failure message)
    m_ocrToastLabel = new QLabel(this);
    m_ocrToastLabel->hide();

    m_ocrToastTimer = new QTimer(this);
    m_ocrToastTimer->setSingleShot(true);
    connect(m_ocrToastTimer, &QTimer::timeout, m_ocrToastLabel, &QLabel::hide);

    // Install application-level event filter to capture ESC even when window loses focus
    qApp->installEventFilter(this);

    // Connect to screen removal signal for graceful handling
    connect(qApp, &QGuiApplication::screenRemoved,
        this, &RegionSelector::onScreenRemoved);

    // Initialize magnifier panel component
    m_magnifierPanel = new MagnifierPanel(this);

    // Initialize region control widget (combines radius toggle + aspect ratio lock)
    m_regionControlWidget = new RegionControlWidget(this);

    // Load saved aspect ratio
    int savedRatioWidth = settings.loadAspectRatioWidth();
    int savedRatioHeight = settings.loadAspectRatioHeight();
    m_regionControlWidget->setLockedRatio(savedRatioWidth, savedRatioHeight);

    // Load saved corner radius and enabled state
    m_cornerRadius = settings.loadCornerRadius();
    m_regionControlWidget->setCurrentRadius(m_cornerRadius);
    m_regionControlWidget->setRadiusEnabled(settings.loadCornerRadiusEnabled());

    // Connect radius enabled state signal
    connect(m_regionControlWidget, &RegionControlWidget::radiusEnabledChanged,
        this, [](bool enabled) {
            AnnotationSettingsManager::instance().saveCornerRadiusEnabled(enabled);
        });

    // Connect aspect ratio lock signal
    connect(m_regionControlWidget, &RegionControlWidget::lockChanged,
        this, [this](bool locked) {
            auto& settings = AnnotationSettingsManager::instance();
            if (!locked) {
                m_selectionManager->setAspectRatio(0.0);
                update();
                return;
            }

            QRect sel = m_selectionManager->selectionRect();
            int ratioWidth = sel.width();
            int ratioHeight = sel.height();
            if (ratioWidth <= 0 || ratioHeight <= 0) {
                ratioWidth = settings.loadAspectRatioWidth();
                ratioHeight = settings.loadAspectRatioHeight();
            }

            if (ratioWidth <= 0 || ratioHeight <= 0) {
                ratioWidth = 1;
                ratioHeight = 1;
            }

            int gcd = std::gcd(ratioWidth, ratioHeight);
            ratioWidth /= gcd;
            ratioHeight /= gcd;
            m_regionControlWidget->setLockedRatio(ratioWidth, ratioHeight);
            settings.saveAspectRatio(ratioWidth, ratioHeight);
            m_selectionManager->setAspectRatio(static_cast<qreal>(ratioWidth) /
                static_cast<qreal>(ratioHeight));
            update();
        });

    // Connect corner radius signal
    connect(m_regionControlWidget, &RegionControlWidget::radiusChanged,
        this, &RegionSelector::onCornerRadiusChanged);

    // Initialize painting component
    m_painter = new RegionPainter(this);
    m_painter->setSelectionManager(m_selectionManager);
    m_painter->setAnnotationLayer(m_annotationLayer);
    m_painter->setToolManager(m_toolManager);
    m_painter->setToolbar(m_toolbar);
    m_painter->setRegionControlWidget(m_regionControlWidget);
    m_painter->setMultiRegionManager(m_multiRegionManager);
    m_painter->setParentWidget(this);

    // Initialize export manager component
    m_exportManager = new RegionExportManager(this);
    m_exportManager->setAnnotationLayer(m_annotationLayer);
    connect(m_exportManager, &RegionExportManager::copyCompleted,
        this, [this](const QPixmap& pixmap) {
            emit copyRequested(pixmap);
            close();
        });
    connect(m_exportManager, &RegionExportManager::saveDialogOpening,
        this, [this]() {
            m_isDialogOpen = true;
            hide();
        });
    connect(m_exportManager, &RegionExportManager::saveDialogClosed,
        this, [this](bool saved) {
            m_isDialogOpen = false;
            if (saved) {
                // Get the selected region again for the signal
                QPixmap selectedRegion = m_exportManager->getSelectedRegion(
                    m_selectionManager->selectionRect(), effectiveCornerRadius());
                emit saveRequested(selectedRegion);
                close();
            }
            else {
                show();
                activateWindow();
                raise();
            }
        });
    connect(m_exportManager, &RegionExportManager::saveCompleted,
        this, [this](const QPixmap& pixmap, const QString& filePath) {
            emit saveCompleted(pixmap, filePath);
            close();
        });
    connect(m_exportManager, &RegionExportManager::saveFailed,
        this, [this](const QString& filePath, const QString& error) {
            emit saveFailed(filePath, error);
            close();
        });

    // Initialize input handling component
    m_inputHandler = new RegionInputHandler(this);
    m_inputHandler->setSelectionManager(m_selectionManager);
    m_inputHandler->setAnnotationLayer(m_annotationLayer);
    m_inputHandler->setToolManager(m_toolManager);
    m_inputHandler->setToolbar(m_toolbar);
    m_inputHandler->setTextEditor(m_textEditor);
    m_inputHandler->setTextAnnotationEditor(m_textAnnotationEditor);
    m_inputHandler->setColorAndWidthWidget(m_colorAndWidthWidget);
    // EmojiPicker is set lazily via ensureEmojiPicker() when first needed
    m_inputHandler->setRegionControlWidget(m_regionControlWidget);
    m_inputHandler->setMultiRegionManager(m_multiRegionManager);
    m_inputHandler->setUpdateThrottler(&m_updateThrottler);
    m_inputHandler->setParentWidget(this);

    // Connect input handler signals
    connect(m_inputHandler, &RegionInputHandler::toolCursorRequested,
        this, &RegionSelector::setToolCursor);
    connect(m_inputHandler, qOverload<>(&RegionInputHandler::updateRequested),
        this, qOverload<>(&QWidget::update));
    connect(m_inputHandler, qOverload<const QRect&>(&RegionInputHandler::updateRequested),
        this, qOverload<const QRect&>(&QWidget::update));
    connect(m_inputHandler, &RegionInputHandler::toolbarClickRequested,
        this, [this](int buttonId) { handleToolbarClick(static_cast<ToolId>(buttonId)); });
    connect(m_inputHandler, &RegionInputHandler::windowDetectionRequested,
        this, &RegionSelector::updateWindowDetection);
    connect(m_inputHandler, &RegionInputHandler::textReEditingRequested,
        this, &RegionSelector::startTextReEditing);
    connect(m_inputHandler, &RegionInputHandler::selectionFinished,
        this, [this]() {
            m_selectionManager->finishSelection();
            if (!m_multiRegionMode) {
                CursorManager::instance().popCursorForWidget(this, CursorContext::Selection);
            }
            if (m_multiRegionMode && m_multiRegionManager) {
                QRect sel = m_selectionManager->selectionRect();
                if (sel.isValid() && sel.width() > 5 && sel.height() > 5 &&
                    m_multiRegionManager->activeIndex() < 0) {
                    m_multiRegionManager->addRegion(sel);
                }
            }
            // Quick Pin mode: directly pin without showing toolbar
            if (m_quickPinMode && !m_multiRegionMode) {
                finishSelection();
            }
        });
    connect(m_inputHandler, &RegionInputHandler::fullScreenSelectionRequested,
        this, [this]() {
            if (!isScreenValid()) {
                return;
            }
            QRect screenGeom = m_currentScreen->geometry();
            QRect fullScreenRect = QRect(0, 0, screenGeom.width(), screenGeom.height());
            m_selectionManager->setFromDetectedWindow(fullScreenRect);
            if (!m_multiRegionMode) {
                CursorManager::instance().popCursorForWidget(this, CursorContext::Selection);
            }

            // Add full-screen region in multi-region mode
            if (m_multiRegionMode && m_multiRegionManager &&
                m_multiRegionManager->activeIndex() < 0) {
                m_multiRegionManager->addRegion(fullScreenRect);
            }

            // Quick Pin mode: directly pin without showing toolbar
            if (m_quickPinMode && !m_multiRegionMode) {
                finishSelection();
            }
        });
    connect(m_inputHandler, &RegionInputHandler::drawingStateChanged,
        this, [this](bool isDrawing) { m_isDrawing = isDrawing; });
    connect(m_inputHandler, &RegionInputHandler::detectionCleared,
        this, [this]() {
            m_highlightedWindowRect = QRect();
            m_detectedWindow.reset();
        });
    connect(m_inputHandler, &RegionInputHandler::selectionCancelledByRightClick,
        this, [this]() {
            qDebug() << "RegionSelector: Cancelled via right-click";
            emit selectionCancelled();
            close();
        });

    // Initialize toolbar handler component
    m_toolbarHandler = new RegionToolbarHandler(this);
    m_toolbarHandler->setToolbar(m_toolbar);
    m_toolbarHandler->setToolManager(m_toolManager);
    m_toolbarHandler->setAnnotationLayer(m_annotationLayer);
    m_toolbarHandler->setColorAndWidthWidget(m_colorAndWidthWidget);
    m_toolbarHandler->setSelectionManager(m_selectionManager);
    // OCRManager is set lazily via ensureOCRManager() when first needed
    m_toolbarHandler->setParentWidget(this);

    // Connect toolbar handler signals
    connect(m_toolbarHandler, &RegionToolbarHandler::toolChanged,
        this, [this](ToolId tool, bool showSubToolbar) {
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
            if (!isScreenValid()) {
                qWarning() << "RegionSelector: Screen invalid, cannot start recording";
                emit selectionCancelled();
                close();
                return;
            }
            emit recordingRequested(localToGlobal(m_selectionManager->selectionRect()), m_currentScreen.data());
            close();
        });
    connect(m_toolbarHandler, &RegionToolbarHandler::saveRequested,
        this, &RegionSelector::saveToFile);
    connect(m_toolbarHandler, &RegionToolbarHandler::copyRequested,
        this, &RegionSelector::copyToClipboard);
    connect(m_toolbarHandler, &RegionToolbarHandler::ocrRequested,
        this, &RegionSelector::performOCR);
    connect(m_toolbarHandler, &RegionToolbarHandler::multiRegionToggled,
        this, &RegionSelector::setMultiRegionMode);
    connect(m_toolbarHandler, &RegionToolbarHandler::multiRegionDoneRequested,
        this, &RegionSelector::completeMultiRegionCapture);
    connect(m_toolbarHandler, &RegionToolbarHandler::multiRegionCancelRequested,
        this, &RegionSelector::cancelMultiRegionCapture);

    // Connect ToolOptionsPanel configuration signals
    connect(m_toolbarHandler, &RegionToolbarHandler::showSizeSectionRequested,
        m_colorAndWidthWidget, &ToolOptionsPanel::setShowSizeSection);
    connect(m_toolbarHandler, &RegionToolbarHandler::showWidthSectionRequested,
        m_colorAndWidthWidget, &ToolOptionsPanel::setShowWidthSection);
    connect(m_toolbarHandler, &RegionToolbarHandler::widthSectionHiddenRequested,
        m_colorAndWidthWidget, &ToolOptionsPanel::setWidthSectionHidden);
    connect(m_toolbarHandler, &RegionToolbarHandler::showColorSectionRequested,
        m_colorAndWidthWidget, &ToolOptionsPanel::setShowColorSection);
    connect(m_toolbarHandler, &RegionToolbarHandler::widthRangeRequested,
        m_colorAndWidthWidget, &ToolOptionsPanel::setWidthRange);
    connect(m_toolbarHandler, &RegionToolbarHandler::currentWidthRequested,
        m_colorAndWidthWidget, &ToolOptionsPanel::setCurrentWidth);
    connect(m_toolbarHandler, &RegionToolbarHandler::stepBadgeSizeRequested,
        m_colorAndWidthWidget, &ToolOptionsPanel::setStepBadgeSize);

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
    // Clean up cursor state before destruction
    CursorManager::instance().clearAllForWidget(this);

    // Clean up color picker dialog
    if (m_colorPickerDialog) {
        delete m_colorPickerDialog;
        m_colorPickerDialog = nullptr;
    }

    // Remove event filter
    qApp->removeEventFilter(this);

    qDebug() << "RegionSelector: Destroyed";
}

void RegionSelector::onScreenRemoved(QScreen* screen)
{
    // Check if our current screen was removed
    if (m_currentScreen == screen || m_currentScreen.isNull()) {
        qWarning() << "RegionSelector: Current screen disconnected, closing gracefully";
        emit selectionCancelled();
        close();
    }
}

bool RegionSelector::isScreenValid() const
{
    return !m_currentScreen.isNull();
}

bool RegionSelector::shouldShowColorPalette() const
{
    if (m_multiRegionMode) return false;
    if (!m_selectionManager->isComplete()) return false;
    if (!m_showSubToolbar) return false;
    return ToolRegistry::instance().showColorPalette(m_currentTool);
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
    if (m_currentTool == ToolId::Mosaic) {
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
    // Save to settings - StepBadgeToolHandler reads from AnnotationSettingsManager
    AnnotationSettingsManager::instance().saveStepBadgeSize(size);
    update();
}

void RegionSelector::onCornerRadiusChanged(int radius)
{
    m_cornerRadius = radius;
    AnnotationSettingsManager::instance().saveCornerRadius(radius);
    update();
}

EmojiPicker* RegionSelector::ensureEmojiPicker()
{
    if (!m_emojiPicker) {
        m_emojiPicker = new EmojiPicker(this);
        connect(m_emojiPicker, &EmojiPicker::emojiSelected,
            this, [this](const QString& emoji) {
                if (auto* handler = dynamic_cast<EmojiStickerToolHandler*>(
                    m_toolManager->handler(ToolId::EmojiSticker))) {
                    handler->setCurrentEmoji(emoji);
                }
                update();
            });
        // Update input handler reference
        m_inputHandler->setEmojiPicker(m_emojiPicker);
    }
    return m_emojiPicker;
}

OCRManager* RegionSelector::ensureOCRManager()
{
    if (!m_ocrManager) {
        m_ocrManager = PlatformFeatures::instance().createOCRManager(this);
        if (m_ocrManager) {
            m_ocrManager->setRecognitionLanguages(OCRSettingsManager::instance().languages());
        }
        // Update toolbar handler reference
        m_toolbarHandler->setOCRManager(m_ocrManager);
    }
    return m_ocrManager;
}

LoadingSpinnerRenderer* RegionSelector::ensureLoadingSpinner()
{
    if (!m_loadingSpinner) {
        m_loadingSpinner = new LoadingSpinnerRenderer(this);
        connect(m_loadingSpinner, &LoadingSpinnerRenderer::needsRepaint,
            this, QOverload<>::of(&QWidget::update));
    }
    return m_loadingSpinner;
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
    if (m_multiRegionMode) return false;
    if (!m_selectionManager->isComplete()) return false;
    if (!m_showSubToolbar) return false;
    return ToolRegistry::instance().showColorWidthWidget(m_currentTool);
}

bool RegionSelector::shouldShowWidthControl() const
{
    if (m_multiRegionMode) return false;
    return ToolRegistry::instance().showWidthControl(m_currentTool);
}

int RegionSelector::toolWidthForCurrentTool() const
{
    if (m_currentTool == ToolId::StepBadge) {
        return StepBadgeAnnotation::radiusForSize(m_stepBadgeSize);
    }
    // Mosaic now uses m_annotationWidth (synced with other tools)
    return m_annotationWidth;
}


void RegionSelector::setupScreenGeometry(QScreen* screen)
{
    m_currentScreen = screen ? screen : QGuiApplication::primaryScreen();
    if (m_currentScreen.isNull()) {
        qCritical() << "RegionSelector: No valid screen available";
        return;
    }
    m_devicePixelRatio = m_currentScreen->devicePixelRatio();

    QRect screenGeom = m_currentScreen->geometry();
    setFixedSize(screenGeom.size());
    m_selectionManager->setBounds(QRect(0, 0, screenGeom.width(), screenGeom.height()));
}

void RegionSelector::initializeForScreen(QScreen* screen, const QPixmap& preCapture)
{
    setupScreenGeometry(screen);
    if (!isScreenValid()) {
        qWarning() << "RegionSelector: Cannot initialize, no valid screen";
        emit selectionCancelled();
        QTimer::singleShot(0, this, &QWidget::close);
        return;
    }

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

    // Create shared pixmap for mosaic tool (explicit sharing to avoid memory duplication)
    m_sharedSourcePixmap = std::make_shared<const QPixmap>(m_backgroundPixmap);

    // Update tool manager with shared pixmap for mosaic tool
    m_toolManager->setSourcePixmap(m_sharedSourcePixmap);
    m_toolManager->setDevicePixelRatio(m_devicePixelRatio);

    // Update export manager with background pixmap and DPR
    m_exportManager->setBackgroundPixmap(m_backgroundPixmap);
    m_exportManager->setDevicePixelRatio(m_devicePixelRatio);

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

    // Pre-warm magnifier cache to eliminate first-frame delay
    m_magnifierPanel->preWarmCache(m_currentPoint, m_backgroundPixmap);

    // Initialize cursor via CursorManager
    CursorManager::instance().pushCursorForWidget(this, CursorContext::Selection, Qt::CrossCursor);

    // Initial window detection at cursor position
    if (m_windowDetector && m_windowDetector->isEnabled()) {
        updateWindowDetection(m_currentPoint);
    }
}

void RegionSelector::initializeWithRegion(QScreen* screen, const QRect& region)
{
    setupScreenGeometry(screen);
    if (!isScreenValid()) {
        qWarning() << "RegionSelector: Cannot initialize with region, no valid screen";
        emit selectionCancelled();
        QTimer::singleShot(0, this, &QWidget::close);
        return;
    }

    // Capture the screen first
    m_backgroundPixmap = m_currentScreen->grabWindow(0);

    // Create shared pixmap for mosaic tool (explicit sharing to avoid memory duplication)
    m_sharedSourcePixmap = std::make_shared<const QPixmap>(m_backgroundPixmap);

    // Update tool manager with shared pixmap for mosaic tool
    m_toolManager->setSourcePixmap(m_sharedSourcePixmap);
    m_toolManager->setDevicePixelRatio(m_devicePixelRatio);

    // Update export manager with background pixmap and DPR
    m_exportManager->setBackgroundPixmap(m_backgroundPixmap);
    m_exportManager->setDevicePixelRatio(m_devicePixelRatio);

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

void RegionSelector::setQuickPinMode(bool enabled)
{
    m_quickPinMode = enabled;
}

bool RegionSelector::isSelectionComplete() const
{
    return m_selectionManager && m_selectionManager->isComplete();
}

void RegionSelector::setWindowDetector(WindowDetector* detector)
{
    m_windowDetector = detector;
}

void RegionSelector::refreshWindowDetectionAtCursor()
{
    if (!m_windowDetector || !m_windowDetector->isEnabled() || !m_currentScreen) {
        return;
    }

    const auto globalPos = QCursor::pos();
    const auto localPos = globalToLocal(globalPos);
    updateWindowDetection(localPos);
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

void RegionSelector::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);

    // Use the dirty rect for clipping to avoid full repaint
    const QRect dirtyRect = event->rect();
    painter.setClipRect(dirtyRect);

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
    m_painter->setMultiRegionMode(m_multiRegionMode);

    // Delegate core painting (background, overlay, selection, annotations)
    m_painter->paint(painter, m_backgroundPixmap, dirtyRect);

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

            if (m_multiRegionMode && m_multiRegionManager && m_multiRegionManager->count() > 0) {
                QString countText = QString("%1 regions").arg(m_multiRegionManager->count());
                QFont countFont = painter.font();
                countFont.setPointSize(11);
                painter.setFont(countFont);

                QFontMetrics fm(countFont);
                QRect countRect = fm.boundingRect(countText);
                countRect.adjust(-8, -4, 8, 4);

                QRect toolbarRect = m_toolbar->boundingRect();
                int countX = toolbarRect.right() - countRect.width();
                int countY = toolbarRect.top() - countRect.height() - 6;
                if (countY < 5) {
                    countY = toolbarRect.bottom() + 6;
                }
                countRect.moveTo(countX, countY);

                auto styleConfig = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
                GlassRenderer::drawGlassPanel(painter, countRect, styleConfig, 6);
                painter.setPen(styleConfig.textColor);
                painter.drawText(countRect, Qt::AlignCenter, countText);
            }

            // Use unified color and width widget with data-driven configuration
            if (shouldShowColorAndWidthWidget()) {
                m_colorAndWidthWidget->setVisible(true);
                // Apply tool-specific section configuration
                ToolSectionConfig::forTool(m_currentTool).applyTo(m_colorAndWidthWidget);
                m_colorAndWidthWidget->setWidthSectionHidden(false);
                // Runtime-dependent setting for Mosaic auto blur
                if (m_currentTool == ToolId::Mosaic) {
                    m_colorAndWidthWidget->setAutoBlurEnabled(m_autoBlurManager != nullptr);
                }
                m_colorAndWidthWidget->updatePosition(m_toolbar->boundingRect(), false, width());
                m_colorAndWidthWidget->draw(painter);
            }
            else {
                m_colorAndWidthWidget->setVisible(false);
            }

            // Draw emoji picker when EmojiSticker tool is selected (lazy creation)
            if (m_currentTool == ToolId::EmojiSticker) {
                EmojiPicker* picker = ensureEmojiPicker();
                picker->setVisible(true);
                picker->updatePosition(m_toolbar->boundingRect(), false);
                picker->draw(painter);
            }
            else if (m_emojiPicker) {
                m_emojiPicker->setVisible(false);
            }

            m_toolbar->drawTooltip(painter);

            // Draw loading spinner when OCR or auto-blur is in progress
            if ((m_ocrInProgress || m_autoBlurInProgress) && m_loadingSpinner) {
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
    m_magnifierPanel->draw(painter, m_currentPoint, size(), m_backgroundPixmap);
}

QCursor RegionSelector::getMosaicCursor(int width)
{
    // Use centralized CursorManager for mosaic cursor
    // Use 2x width for mosaic cursor (UI shows half the actual drawing size)
    int effectiveWidth = width * 2;
    if (effectiveWidth != m_mosaicCursorCacheWidth) {
        m_mosaicCursorCache = CursorManager::createMosaicCursor(effectiveWidth);
        m_mosaicCursorCacheWidth = effectiveWidth;
    }
    return m_mosaicCursorCache;
}

void RegionSelector::setToolCursor()
{
    auto& cursorManager = CursorManager::instance();

    // Special handling for text tool during editing
    if (m_currentTool == ToolId::Text && m_textEditor->isEditing()) {
        return;
    }

    // Use CursorManager's unified tool cursor update
    // This delegates to IToolHandler::cursor() for proper cursor behavior
    cursorManager.updateToolCursorForWidget(this);
}

void RegionSelector::handleToolbarClick(ToolId tool)
{
    // Sync current state to handler
    m_toolbarHandler->setCurrentTool(m_currentTool);
    m_toolbarHandler->setShowSubToolbar(m_showSubToolbar);
    m_toolbarHandler->setAnnotationWidth(m_annotationWidth);
    m_toolbarHandler->setAnnotationColor(m_annotationColor);
    m_toolbarHandler->setStepBadgeSize(m_stepBadgeSize);
    m_toolbarHandler->setOCRInProgress(m_ocrInProgress);
    m_toolbarHandler->setMultiRegionMode(m_multiRegionMode);

    // Delegate to handler
    m_toolbarHandler->handleToolbarClick(tool);
}

void RegionSelector::setMultiRegionMode(bool enabled)
{
    if (m_multiRegionMode == enabled) {
        return;
    }

    m_multiRegionMode = enabled;
    m_inputHandler->setMultiRegionMode(enabled);
    m_painter->setMultiRegionMode(enabled);
    m_toolbarHandler->setMultiRegionMode(enabled);
    m_toolbarHandler->setupToolbarButtons();

    if (enabled) {
        m_multiRegionManager->clear();
        QRect existingSelection = m_selectionManager->selectionRect();
        if (m_selectionManager->hasSelection() && existingSelection.isValid()) {
            m_multiRegionManager->addRegion(existingSelection);
        }
        else {
            m_multiRegionManager->setActiveIndex(-1);
            m_selectionManager->clearSelection();
        }
        m_annotationLayer->clear();
        m_currentTool = ToolId::Selection;
        m_showSubToolbar = false;
    }
    else {
        m_multiRegionManager->clear();
        m_selectionManager->clearSelection();
        m_showSubToolbar = true;
    }

    update();
}

void RegionSelector::completeMultiRegionCapture()
{
    if (!m_multiRegionMode || !m_multiRegionManager || m_multiRegionManager->count() == 0) {
        return;
    }

    // Directly merge regions without showing modal
    QImage merged = m_multiRegionManager->mergeToSingleImage(m_backgroundPixmap, m_devicePixelRatio);
    if (merged.isNull()) {
        qWarning() << "RegionSelector: Failed to merge multi-region capture";
        QMessageBox::warning(this, "Error", "Failed to merge regions. Please try again.");
        return;
    }
    QPixmap pixmap = QPixmap::fromImage(merged, Qt::NoOpaqueDetection);
    pixmap.setDevicePixelRatio(m_devicePixelRatio);
    QRect bounds = m_multiRegionManager->boundingBox();
    QRect globalBounds(localToGlobal(bounds.topLeft()), bounds.size());
    emit regionSelected(pixmap, localToGlobal(bounds.topLeft()), globalBounds);
    close();
}

void RegionSelector::cancelMultiRegionCapture()
{
    setMultiRegionMode(false);
}

void RegionSelector::copyToClipboard()
{
    m_exportManager->copyToClipboard(
        m_selectionManager->selectionRect(), effectiveCornerRadius());
    // Note: close() will be called via copyCompleted signal connection
}

void RegionSelector::saveToFile()
{
    // Delegate to export manager - it will emit signals for UI coordination
    m_exportManager->saveToFile(
        m_selectionManager->selectionRect(), effectiveCornerRadius(), nullptr);
    // Note: UI handling (hide/show/close) is done via signal connections
}

void RegionSelector::finishSelection()
{
    QRect sel = m_selectionManager->selectionRect();
    QPixmap selectedRegion = m_exportManager->getSelectedRegion(sel, effectiveCornerRadius());

    qDebug() << "finishSelection: selectionRect=" << sel
        << "globalPos=" << localToGlobal(sel.topLeft());

    QRect globalRect(localToGlobal(sel.topLeft()), sel.size());
    emit regionSelected(selectedRegion, localToGlobal(sel.topLeft()), globalRect);
    close();
}

void RegionSelector::mousePressEvent(QMouseEvent* event)
{
    // Sync state to input handler
    m_inputHandler->setCurrentTool(m_currentTool);
    m_inputHandler->setShowSubToolbar(m_showSubToolbar);
    m_inputHandler->setHighlightedWindowRect(m_highlightedWindowRect);
    m_inputHandler->setDetectedWindow(m_detectedWindow.has_value());
    m_inputHandler->setAnnotationColor(m_annotationColor);
    m_inputHandler->setAnnotationWidth(m_annotationWidth);
    m_inputHandler->setArrowStyle(static_cast<int>(m_arrowStyle));
    m_inputHandler->setLineStyle(static_cast<int>(m_lineStyle));
    m_inputHandler->setShapeType(static_cast<int>(m_shapeType));
    m_inputHandler->setShapeFillMode(static_cast<int>(m_shapeFillMode));
    m_inputHandler->setMultiRegionMode(m_multiRegionMode);

    // Delegate to input handler
    m_inputHandler->handleMousePress(event);

    // Sync state back from handler
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
    m_inputHandler->setDetectedWindow(m_detectedWindow.has_value());
    m_inputHandler->setMultiRegionMode(m_multiRegionMode);

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
    m_inputHandler->setDetectedWindow(m_detectedWindow.has_value());
    m_inputHandler->setMultiRegionMode(m_multiRegionMode);

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

    if (!m_selectionManager->isComplete()) {
        return;
    }

    // Check if double-click is on a text annotation (for re-editing)
    if (m_annotationLayer) {
        int hitIndex = m_annotationLayer->hitTestText(event->pos());
        if (hitIndex >= 0) {
            m_annotationLayer->setSelectedIndex(hitIndex);
            m_textAnnotationEditor->startReEditing(hitIndex, m_annotationColor);
            update();
            return;
        }
    }

    // Forward other double-clicks to tool manager
    m_toolManager->handleDoubleClick(event->pos());
    update();
}

void RegionSelector::wheelEvent(QWheelEvent* event)
{
    // Handle scroll wheel for StepBadge size adjustment
    if (m_currentTool == ToolId::StepBadge) {
        int delta = event->angleDelta().y();
        if (delta != 0) {
            // Cycle through sizes: Small -> Medium -> Large -> Small
            int current = static_cast<int>(m_stepBadgeSize);
            if (delta > 0) {
                current = (current + 1) % 3;  // Scroll up = increase size
            }
            else {
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
        // Try to handle escape in tool first (e.g. finish polyline)
        if (m_toolManager && m_toolManager->handleEscape()) {
            event->accept();
            return;
        }

        // ESC 直接離開 capture mode
        qDebug() << "RegionSelector: Cancelled via Escape";
        if (m_multiRegionMode) {
            cancelMultiRegionCapture();
        }
        else {
            emit selectionCancelled();
            close();
        }
    }
    else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (m_multiRegionMode) {
            completeMultiRegionCapture();
        }
        else if (m_selectionManager->isComplete()) {
            finishSelection();
        }
    }
    else if (event->matches(QKeySequence::Copy)) {
        if (!m_multiRegionMode && m_selectionManager->isComplete()) {
            copyToClipboard();
        }
    }
    else if (event->matches(QKeySequence::Save)) {
        if (!m_multiRegionMode && m_selectionManager->isComplete()) {
            saveToFile();
        }
    }
    else if (event->key() == Qt::Key_M) {
        setMultiRegionMode(!m_multiRegionMode);
    }
    else if (event->key() == Qt::Key_R && !event->modifiers()) {
        if (m_selectionManager->isComplete()) {
            handleToolbarClick(ToolId::Record);
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
        if (m_multiRegionMode && m_multiRegionManager) {
            if (m_multiRegionManager->count() > 0) {
                m_multiRegionManager->removeRegion(m_multiRegionManager->count() - 1);
            }
        }
        else if (m_selectionManager->isComplete() && m_annotationLayer->canUndo()) {
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
        if (m_multiRegionMode && m_multiRegionManager) {
            int activeIndex = m_multiRegionManager->activeIndex();
            if (activeIndex >= 0) {
                m_multiRegionManager->removeRegion(activeIndex);
            }
        }
        // Delete selected text annotation
        else if (m_selectionManager->isComplete() && m_annotationLayer->selectedIndex() >= 0) {
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

void RegionSelector::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    // Delay cursor setting to ensure macOS has finished window activation.
    QTimer::singleShot(100, this, [this]() {
        forceNativeCrosshairCursor(this);
    });
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

            // Try to handle escape in tool first (e.g. finish polyline)
            // Note: eventFilter sees keys before keyPressEvent
            if (m_toolManager && m_toolManager->handleEscape()) {
                return true; // Event handled, don't close
            }

            qDebug() << "RegionSelector: Cancelled via Escape (event filter)";
            emit selectionCancelled();
            close();
            return true;  // Event handled
        }
    }
    else if (event->type() == QEvent::WindowActivate) {
        m_activationCount++;
    }
    else if (event->type() == QEvent::ApplicationDeactivate) {
        // Don't cancel if a dialog is open (e.g., save dialog)
        if (m_isDialogOpen) {
            return false;
        }
        // Startup protection: ignore deactivation if window hasn't been activated yet
        // This prevents false cancellation when popup menus close during startup or
        // when window manager focus changes are still settling
        if (m_activationCount == 0) {
            qDebug() << "RegionSelector: Ignoring premature deactivation (activation count: 0)";

            // Restore focus to re-enable mouse event delivery
            // Without this, Qt stops delivering mouse events to deactivated windows
            QTimer::singleShot(0, this, [this]() {
                activateWindow();
                raise();
                });

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

bool RegionSelector::isAnnotationTool(ToolId tool) const
{
    switch (tool) {
    case ToolId::Pencil:
    case ToolId::Marker:
    case ToolId::Arrow:
    case ToolId::Shape:
    case ToolId::Text:
    case ToolId::Mosaic:
    case ToolId::Eraser:
    case ToolId::StepBadge:
    case ToolId::EmojiSticker:
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

        auto textAnnotation = std::make_unique<TextBoxAnnotation>(QPointF(pos), text, font, m_annotationColor);
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
    auto* textItem = dynamic_cast<TextBoxAnnotation*>(m_annotationLayer->itemAt(annotationIndex));
    if (textItem) {
        m_annotationColor = textItem->color();
        m_colorAndWidthWidget->setCurrentColor(m_annotationColor);
    }
}

void RegionSelector::performOCR()
{
    OCRManager* ocrMgr = ensureOCRManager();
    if (!ocrMgr || m_ocrInProgress || !m_selectionManager->isComplete()) {
        if (!ocrMgr) {
            qDebug() << "RegionSelector: OCR not available on this platform";
        }
        return;
    }

    m_ocrInProgress = true;
    ensureLoadingSpinner()->start();
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
    ocrMgr->recognizeText(selectedRegion,
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
    if (m_loadingSpinner) {
        m_loadingSpinner->stop();
    }

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
    if (!m_autoBlurManager || m_autoBlurInProgress || !m_selectionManager->isComplete()) {
        return;
    }

    // Lazy initialization: load cascade classifier on first use
    if (!m_autoBlurManager->isInitialized()) {
        qDebug() << "RegionSelector: Lazy-initializing AutoBlurManager...";
        if (!m_autoBlurManager->initialize()) {
            qDebug() << "RegionSelector: Auto-blur initialization failed";
            return;
        }
    }

    m_autoBlurInProgress = true;
    m_colorAndWidthWidget->setAutoBlurProcessing(true);
    ensureLoadingSpinner()->start();
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
                // Use blur type from settings
                auto blurType = static_cast<MosaicRectAnnotation::BlurType>(
                    static_cast<int>(m_mosaicBlurType)
                    );
                auto mosaic = std::make_unique<MosaicRectAnnotation>(
                    logicalRect, m_sharedSourcePixmap, 12, blurType
                );
                m_annotationLayer->addItem(std::move(mosaic));
            }
        }

        onAutoBlurComplete(true, faceCount, 0, QString());
    }
    else {
        onAutoBlurComplete(false, 0, 0, result.errorMessage);
    }
}

void RegionSelector::onAutoBlurComplete(bool success, int faceCount, int /*textCount*/, const QString& error)
{
    m_autoBlurInProgress = false;
    m_colorAndWidthWidget->setAutoBlurProcessing(false);
    if (m_loadingSpinner) {
        m_loadingSpinner->stop();
    }

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
    if (m_currentScreen.isNull()) {
        qWarning() << "RegionSelector: Screen null in localToGlobal";
        return localPos;
    }
    return m_currentScreen->geometry().topLeft() + localPos;
}

QPoint RegionSelector::globalToLocal(const QPoint& globalPos) const
{
    if (m_currentScreen.isNull()) {
        qWarning() << "RegionSelector: Screen null in globalToLocal";
        return globalPos;
    }
    return globalPos - m_currentScreen->geometry().topLeft();
}

QRect RegionSelector::localToGlobal(const QRect& localRect) const
{
    if (m_currentScreen.isNull()) {
        qWarning() << "RegionSelector: Screen null in localToGlobal(QRect)";
        return localRect;
    }
    return localRect.translated(m_currentScreen->geometry().topLeft());
}

QRect RegionSelector::globalToLocal(const QRect& globalRect) const
{
    if (m_currentScreen.isNull()) {
        qWarning() << "RegionSelector: Screen null in globalToLocal(QRect)";
        return globalRect;
    }
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
