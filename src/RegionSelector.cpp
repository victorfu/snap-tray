#include "RegionSelector.h"
#include "annotation/AnnotationContext.h"
#include "platform/WindowLevel.h"
#include "region/RegionPainter.h"
#include "region/RegionInputHandler.h"
#include "region/RegionToolbarHandler.h"
#include "region/RegionSettingsHelper.h"
#include "region/RegionExportManager.h"
#include "region/MultiRegionListPanel.h"
#include "annotations/AllAnnotations.h"
#include "cursor/CursorManager.h"
#include "GlassRenderer.h"
#include "ToolbarStyle.h"
#include "IconRenderer.h"
#include "toolbar/ToolOptionsPanel.h"
#include "colorwidgets/ColorPickerDialogCompat.h"

using snaptray::colorwidgets::ColorPickerDialogCompat;
#include "EmojiPicker.h"
#include "OCRManager.h"
#include "QRCodeManager.h"
#include "PlatformFeatures.h"
#include "detection/AutoBlurManager.h"
#include "detection/CredentialDetector.h"
#include "settings/AnnotationSettingsManager.h"
#include "settings/AutoBlurSettingsManager.h"
#include "settings/FileSettingsManager.h"
#include "settings/OCRSettingsManager.h"
#include "OCRResultDialog.h"
#include "QRCodeResultDialog.h"
#include "ui/GlobalToast.h"
#include "tools/handlers/MosaicToolHandler.h"
#include "tools/handlers/EmojiStickerToolHandler.h"
#include "tools/ToolSectionConfig.h"
#include "tools/ToolTraits.h"
#include "utils/CoordinateHelper.h"
#include <QTextEdit>

#include <cstring>
#include <algorithm>
#include <map>
#include <memory>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QEnterEvent>
#include <QFocusEvent>
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
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include "tools/ToolRegistry.h"

RegionSelector::RegionSelector(QWidget* parent)
    : QWidget(parent)
    , m_currentScreen(nullptr)
    , m_selectionManager(nullptr)
    , m_devicePixelRatio(1.0)
    , m_toolbar(nullptr)
    , m_annotationLayer(nullptr)
    , m_isClosing(false)
    , m_isDialogOpen(false)
    , m_windowDetector(nullptr)
    , m_ocrManager(nullptr)
    , m_ocrInProgress(false)
    , m_qrCodeManager(nullptr)
    , m_qrCodeInProgress(false)
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
            if (m_inputState.multiRegionMode && m_multiRegionManager &&
                m_inputState.replaceTargetIndex < 0) {
                int activeIndex = m_multiRegionManager->activeIndex();
                if (activeIndex >= 0) {
                    m_multiRegionManager->updateRegion(activeIndex, rect);
                }
            }
            // Removed update() - repaints now handled by RegionInputHandler's throttled path
        });
    connect(m_selectionManager, &SelectionStateManager::stateChanged,
        this, [this](SelectionStateManager::State newState) {
            if (m_inputState.multiRegionMode &&
                newState == SelectionStateManager::State::Complete &&
                m_multiRegionListRefreshPending) {
                m_multiRegionListRefreshPending = false;
                refreshMultiRegionListPanel();
            }

            auto& cm = CursorManager::instance();

            // Multi-region mode owns its cursor updates via hover/input-state handling.
            // Only keep explicit selection cursor overrides for active drag operations.
            if (m_inputState.multiRegionMode) {
                if (newState == SelectionStateManager::State::Moving) {
                    cm.pushCursorForWidget(this, CursorContext::Selection, Qt::SizeAllCursor);
                }
                else if (newState == SelectionStateManager::State::ResizingHandle) {
                    SelectionStateManager::ResizeHandle handle =
                        m_selectionManager ? m_selectionManager->activeResizeHandle()
                                           : SelectionStateManager::ResizeHandle::None;
                    const Qt::CursorShape shape = handle != SelectionStateManager::ResizeHandle::None
                        ? CursorManager::cursorForHandle(handle)
                        : Qt::SizeAllCursor;
                    cm.pushCursorForWidget(this, CursorContext::Selection, shape);
                }
                else {
                    cm.popCursorForWidget(this, CursorContext::Selection);
                }
                syncMultiRegionListPanelCursor();
                return;
            }

            // Single-region mode: keep selection cursor fully aligned with state.
            if (newState == SelectionStateManager::State::None ||
                newState == SelectionStateManager::State::Selecting) {
                cm.pushCursorForWidget(this, CursorContext::Selection, Qt::CrossCursor);
            }
            else if (newState == SelectionStateManager::State::Moving) {
                cm.pushCursorForWidget(this, CursorContext::Selection, Qt::SizeAllCursor);
            }
            else if (newState == SelectionStateManager::State::ResizingHandle) {
                SelectionStateManager::ResizeHandle handle =
                    m_selectionManager ? m_selectionManager->activeResizeHandle()
                                       : SelectionStateManager::ResizeHandle::None;
                const Qt::CursorShape shape = handle != SelectionStateManager::ResizeHandle::None
                    ? CursorManager::cursorForHandle(handle)
                    : Qt::SizeAllCursor;
                cm.pushCursorForWidget(this, CursorContext::Selection, shape);
            }
            else {
                cm.popCursorForWidget(this, CursorContext::Selection);
            }
        });

    // Initialize multi-region manager
    m_multiRegionManager = new MultiRegionManager(this);
    // Note: regionAdded/Removed/Updated connections moved after m_painter initialization
    connect(m_multiRegionManager, &MultiRegionManager::activeIndexChanged,
        this, [this](int index) {
            if (!m_inputState.multiRegionMode) return;
            if (m_multiRegionListPanel) {
                m_multiRegionListPanel->setActiveIndex(index);
            }
            if (index >= 0) {
                m_selectionManager->setSelectionRect(m_multiRegionManager->regionRect(index));
            }
            else if (!m_selectionManager->isSelecting()) {
                m_selectionManager->clearSelection();
            }
            // Removed update() - setSelectionRect triggers selectionChanged which flows through throttled path
        });

    // Initialize annotation layer
    m_annotationLayer = new AnnotationLayer(this);

    // Load saved annotation settings (or defaults)
    auto& settings = AnnotationSettingsManager::instance();
    m_inputState.annotationColor = settings.loadColor();
    m_inputState.annotationWidth = settings.loadWidth();
    m_inputState.arrowStyle = settings.loadArrowStyle();
    m_inputState.lineStyle = settings.loadLineStyle();
    m_stepBadgeSize = settings.loadStepBadgeSize();
    m_mosaicBlurType = settings.loadMosaicBlurType();

    // Initialize tool manager
    m_toolManager = new ToolManager(this);
    m_toolManager->registerDefaultHandlers();
    m_toolManager->setAnnotationLayer(m_annotationLayer);
    m_toolManager->setColor(m_inputState.annotationColor);
    m_toolManager->setWidth(m_inputState.annotationWidth);
    m_toolManager->setArrowStyle(m_inputState.arrowStyle);
    m_toolManager->setLineStyle(m_inputState.lineStyle);
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
        // Re-edit initialization should update runtime/UI state only.
        // Persisting here would rewrite default color without explicit user change.
        syncColorToRuntimeState(color);
    });
    m_toolManager->setHostFocusCallback([this]() {
        setFocus(Qt::OtherFocusReason);
    });

    // Initialize unified color and width widget
    m_colorAndWidthWidget = new ToolOptionsPanel(this);
    m_colorAndWidthWidget->setCurrentColor(m_inputState.annotationColor);
    m_colorAndWidthWidget->setCurrentWidth(m_inputState.annotationWidth);
    m_colorAndWidthWidget->setWidthRange(1, 20);
    // Set persisted annotation style controls
    m_colorAndWidthWidget->setArrowStyle(m_inputState.arrowStyle);
    m_colorAndWidthWidget->setLineStyle(m_inputState.lineStyle);

    // Centralized annotation/text setup and common signal wiring
    m_annotationContext = std::make_unique<AnnotationContext>(*this);
    m_annotationContext->setupTextAnnotationEditor(true, true);
    m_annotationContext->connectTextEditorSignals();
    m_annotationContext->connectToolOptionsSignals();
    m_annotationContext->connectTextFormattingSignals();
    m_annotationContext->syncTextFormattingControls();

    // Connect shape section signals
    connect(m_colorAndWidthWidget, &ToolOptionsPanel::shapeTypeChanged,
        this, [this](ShapeType type) {
            m_inputState.shapeType = type;
            m_toolManager->setShapeType(static_cast<int>(type));
        });
    connect(m_colorAndWidthWidget, &ToolOptionsPanel::shapeFillModeChanged,
        this, [this](ShapeFillMode mode) {
            m_inputState.shapeFillMode = mode;
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

    m_screenSwitchTimer = new QTimer(this);
    m_screenSwitchTimer->setInterval(50);
    connect(m_screenSwitchTimer, &QTimer::timeout,
            this, &RegionSelector::handleCursorScreenSwitch);

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

    // Multi-region list panel (left dock)
    m_multiRegionListPanel = new MultiRegionListPanel(this);
    m_multiRegionListPanel->hide();
    connect(m_multiRegionListPanel, &MultiRegionListPanel::regionActivated,
        this, [this](int index) {
            if (!m_inputState.multiRegionMode || !m_multiRegionManager) {
                return;
            }
            if (m_inputState.replaceTargetIndex >= 0 &&
                m_inputState.replaceTargetIndex != index) {
                beginMultiRegionReplace(index);
                return;
            }
            m_multiRegionManager->setActiveIndex(index);
            update();
        });
    connect(m_multiRegionListPanel, &MultiRegionListPanel::regionDeleteRequested,
        this, [this](int index) {
            if (!m_inputState.multiRegionMode || !m_multiRegionManager) {
                return;
            }
            if (index < 0 || index >= m_multiRegionManager->count()) {
                return;
            }
            if (m_inputState.replaceTargetIndex == index) {
                cancelMultiRegionReplace(false);
            }
            m_multiRegionManager->removeRegion(index);
            update();
        });
    connect(m_multiRegionListPanel, &MultiRegionListPanel::regionReplaceRequested,
        this, &RegionSelector::beginMultiRegionReplace);
    connect(m_multiRegionListPanel, &MultiRegionListPanel::regionMoveRequested,
        this, [this](int fromIndex, int toIndex) {
            if (!m_inputState.multiRegionMode || !m_multiRegionManager) {
                return;
            }
            if (!m_multiRegionManager->moveRegion(fromIndex, toIndex)) {
                return;
            }

            if (m_inputState.replaceTargetIndex == fromIndex) {
                m_inputState.replaceTargetIndex = toIndex;
            } else if (fromIndex < toIndex &&
                       m_inputState.replaceTargetIndex > fromIndex &&
                       m_inputState.replaceTargetIndex <= toIndex) {
                m_inputState.replaceTargetIndex -= 1;
            } else if (toIndex < fromIndex &&
                       m_inputState.replaceTargetIndex >= toIndex &&
                       m_inputState.replaceTargetIndex < fromIndex) {
                m_inputState.replaceTargetIndex += 1;
            }
            update();
        });

    // Initialize painting component
    m_painter = new RegionPainter(this);
    m_painter->setSelectionManager(m_selectionManager);
    m_painter->setAnnotationLayer(m_annotationLayer);
    m_painter->setToolManager(m_toolManager);
    m_painter->setToolbar(m_toolbar);
    m_painter->setRegionControlWidget(m_regionControlWidget);
    m_painter->setMultiRegionManager(m_multiRegionManager);
    m_painter->setParentWidget(this);

    // Connect multi-region signals that depend on m_painter (must be after m_painter init)
    connect(m_multiRegionManager, &MultiRegionManager::regionAdded,
        this, [this](int) {
            m_painter->invalidateOverlayCache();
            refreshMultiRegionListPanel();
        });
    connect(m_multiRegionManager, &MultiRegionManager::regionRemoved,
        this, [this](int removedIndex) {
            m_painter->invalidateOverlayCache();
            if (m_inputState.replaceTargetIndex == removedIndex) {
                cancelMultiRegionReplace(false);
            } else if (m_inputState.replaceTargetIndex > removedIndex) {
                m_inputState.replaceTargetIndex -= 1;
            }
            refreshMultiRegionListPanel();
        });
    connect(m_multiRegionManager, &MultiRegionManager::regionUpdated,
        this, [this](int) {
            m_painter->invalidateOverlayCache();
            const bool isSelectionInteracting = m_selectionManager &&
                (m_selectionManager->isSelecting() ||
                 m_selectionManager->isResizing() ||
                 m_selectionManager->isMoving());
            if (isSelectionInteracting) {
                m_multiRegionListRefreshPending = true;
            } else {
                refreshMultiRegionListPanel();
            }
        });
    connect(m_multiRegionManager, &MultiRegionManager::regionsReordered,
        this, [this]() {
            m_painter->invalidateOverlayCache();
            refreshMultiRegionListPanel();
        });
    connect(m_multiRegionManager, &MultiRegionManager::regionsCleared,
        this, [this]() {
            m_painter->invalidateOverlayCache();
            refreshMultiRegionListPanel();
        });

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
    m_inputHandler->setSharedState(&m_inputState);

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
    connect(m_inputHandler, &RegionInputHandler::selectionFinished,
        this, [this]() {
            m_selectionManager->finishSelection();
            if (!m_inputState.multiRegionMode) {
                CursorManager::instance().popCursorForWidget(this, CursorContext::Selection);
            }
            if (m_inputState.multiRegionMode && m_multiRegionManager) {
                QRect sel = m_selectionManager->selectionRect();
                if (m_inputState.replaceTargetIndex >= 0) {
                    const int replaceIndex = m_inputState.replaceTargetIndex;
                    if (replaceIndex >= 0 && replaceIndex < m_multiRegionManager->count() &&
                        sel.isValid() && sel.width() > 5 && sel.height() > 5) {
                        m_multiRegionManager->updateRegion(replaceIndex, sel);
                        m_multiRegionManager->setActiveIndex(replaceIndex);
                    }
                    m_inputState.replaceTargetIndex = -1;
                    m_replaceOriginalRect = QRect();
                }
                else if (sel.isValid() && sel.width() > 5 && sel.height() > 5 &&
                    m_multiRegionManager->activeIndex() < 0) {
                    m_multiRegionManager->addRegion(sel);
                }
            }
            // Quick Pin mode: directly pin without showing toolbar
            if (m_quickPinMode && !m_inputState.multiRegionMode) {
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
            if (!m_inputState.multiRegionMode) {
                CursorManager::instance().popCursorForWidget(this, CursorContext::Selection);
            }

            // Add full-screen region in multi-region mode
            if (m_inputState.multiRegionMode && m_multiRegionManager &&
                m_multiRegionManager->activeIndex() < 0) {
                m_multiRegionManager->addRegion(fullScreenRect);
            }

            // Quick Pin mode: directly pin without showing toolbar
            if (m_quickPinMode && !m_inputState.multiRegionMode) {
                finishSelection();
            }
        });
    connect(m_inputHandler, &RegionInputHandler::detectionCleared,
        this, [this](const QRect& previousHighlightRect) {
            QRect oldVisualRect;
            if (!previousHighlightRect.isNull()) {
                const QString oldTitle = QString("%1x%2")
                    .arg(previousHighlightRect.width())
                    .arg(previousHighlightRect.height());
                oldVisualRect = m_painter->getWindowHighlightVisualRect(previousHighlightRect, oldTitle);
            }

            m_inputState.highlightedWindowRect = QRect();
            m_inputState.hasDetectedWindow = false;
            m_detectedWindow.reset();

            if (!oldVisualRect.isNull()) {
                update(oldVisualRect);
            }
        });
    connect(m_inputHandler, &RegionInputHandler::selectionCancelledByRightClick,
        this, [this]() {
            emit selectionCancelled();
            close();
        });
    connect(m_inputHandler, &RegionInputHandler::replaceSelectionCancelled,
        this, [this]() {
            cancelMultiRegionReplace(true);
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
            m_inputState.currentTool = tool;
            m_inputState.showSubToolbar = showSubToolbar;
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
    connect(m_toolbarHandler, &RegionToolbarHandler::qrCodeRequested,
        this, &RegionSelector::performQRCodeScan);
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
    if (m_autoBlurWatcher) {
        m_autoBlurWatcher->waitForFinished();
    }

    // Clean up cursor state before destruction
    CursorManager::instance().clearAllForWidget(this);

    if (m_colorPickerDialog) {
        m_colorPickerDialog->close();
    }

    delete m_shapeAnnotationEditor;
    m_shapeAnnotationEditor = nullptr;

    // Remove event filter
    qApp->removeEventFilter(this);
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
    if (m_inputState.multiRegionMode) return false;
    if (!m_selectionManager->isComplete()) return false;
    if (!m_inputState.showSubToolbar) return false;
    return ToolRegistry::instance().showColorPalette(m_inputState.currentTool);
}

QWidget* RegionSelector::annotationHostWidget() const
{
    return const_cast<RegionSelector*>(this);
}

AnnotationLayer* RegionSelector::annotationLayerForContext() const
{
    return m_annotationLayer;
}

ToolOptionsPanel* RegionSelector::toolOptionsPanelForContext() const
{
    return m_colorAndWidthWidget;
}

InlineTextEditor* RegionSelector::inlineTextEditorForContext() const
{
    return m_textEditor;
}

TextAnnotationEditor* RegionSelector::textAnnotationEditorForContext() const
{
    return m_textAnnotationEditor;
}

void RegionSelector::onContextColorSelected(const QColor& color)
{
    onColorSelected(color);
}

void RegionSelector::onContextMoreColorsRequested()
{
    onMoreColorsRequested();
}

void RegionSelector::onContextLineWidthChanged(int width)
{
    onLineWidthChanged(width);
}

void RegionSelector::onContextArrowStyleChanged(LineEndStyle style)
{
    onArrowStyleChanged(style);
}

void RegionSelector::onContextLineStyleChanged(LineStyle style)
{
    onLineStyleChanged(style);
}

void RegionSelector::onContextFontSizeDropdownRequested(const QPoint& pos)
{
    onFontSizeDropdownRequested(pos);
}

void RegionSelector::onContextFontFamilyDropdownRequested(const QPoint& pos)
{
    onFontFamilyDropdownRequested(pos);
}

void RegionSelector::onContextTextEditingFinished(const QString& text, const QPoint& position)
{
    onTextEditingFinished(text, position);
}

void RegionSelector::onContextTextEditingCancelled()
{
    if (m_textAnnotationEditor) {
        m_textAnnotationEditor->cancelEditing();
    }
}

void RegionSelector::syncColorToAllWidgets(const QColor& color)
{
    syncColorToRuntimeState(color);
    AnnotationSettingsManager::instance().saveColor(color);
}

void RegionSelector::syncColorToRuntimeState(const QColor& color)
{
    m_inputState.annotationColor = color;
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
    m_colorAndWidthWidget->setCurrentColor(m_inputState.annotationColor);

    AnnotationContext::showColorPickerDialog(
        this,
        m_colorPickerDialog,
        m_inputState.annotationColor,
        geometry().center(),
        [this](const QColor& color) {
            syncColorToAllWidgets(color);
        });
}

void RegionSelector::onLineWidthChanged(int width)
{
    if (m_inputState.currentTool == ToolId::Mosaic) {
        m_inputState.annotationWidth = width;
        // Update cursor to reflect new width
        setToolCursor();
        AnnotationSettingsManager::instance().saveWidth(width);
    }
    else {
        m_inputState.annotationWidth = width;
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

QRCodeManager* RegionSelector::ensureQRCodeManager()
{
    if (!m_qrCodeManager) {
        m_qrCodeManager = new QRCodeManager(this);
    }
    return m_qrCodeManager;
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
    if (m_inputState.multiRegionMode) return false;
    if (!m_selectionManager->isComplete()) return false;
    if (!m_inputState.showSubToolbar) return false;
    return ToolRegistry::instance().showColorWidthWidget(m_inputState.currentTool);
}

bool RegionSelector::shouldShowWidthControl() const
{
    if (m_inputState.multiRegionMode) return false;
    return ToolRegistry::instance().showWidthControl(m_inputState.currentTool);
}

int RegionSelector::toolWidthForCurrentTool() const
{
    if (m_inputState.currentTool == ToolId::StepBadge) {
        return StepBadgeAnnotation::radiusForSize(m_stepBadgeSize);
    }
    // Mosaic now uses m_inputState.annotationWidth (synced with other tools)
    return m_inputState.annotationWidth;
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
    if (m_multiRegionListPanel) {
        m_multiRegionListPanel->updatePanelGeometry(size());
    }
    m_selectionManager->setBounds(QRect(0, 0, screenGeom.width(), screenGeom.height()));
    m_toolManager->setTextEditingBounds(rect());

    if (m_exportManager) {
        const int monitorIndex = QGuiApplication::screens().indexOf(m_currentScreen.data());
        m_exportManager->setMonitorIdentifier(
            monitorIndex >= 0 ? QString::number(monitorIndex) : QStringLiteral("unknown"));
    }
}

void RegionSelector::initializeForScreen(QScreen* screen, const QPixmap& preCapture)
{
    clearPreservedSelection();

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
    }
    else {
        m_backgroundPixmap = m_currentScreen->grabWindow(0);
    }

    // Create shared pixmap for mosaic tool (explicit sharing to avoid memory duplication)
    m_sharedSourcePixmap = std::make_shared<const QPixmap>(m_backgroundPixmap);

    // Update tool manager with shared pixmap for mosaic tool
    m_toolManager->setSourcePixmap(m_sharedSourcePixmap);
    m_toolManager->setDevicePixelRatio(m_devicePixelRatio);

    // Update export manager with background pixmap and DPR
    m_exportManager->setBackgroundPixmap(m_backgroundPixmap);
    m_exportManager->setDevicePixelRatio(m_devicePixelRatio);
    if (m_multiRegionListPanel) {
        m_multiRegionListPanel->setCaptureContext(m_backgroundPixmap, m_devicePixelRatio);
    }

    // 不預設選取整個螢幕，等待用戶操作
    m_selectionManager->clearSelection();

    // 將 cursor 全域座標轉換為 widget 本地座標，用於 crosshair/magnifier 初始位置
    QRect screenGeom = m_currentScreen->geometry();
    QPoint globalCursor = QCursor::pos();
    m_inputState.currentPoint = globalCursor - screenGeom.topLeft();

    // 鎖定視窗大小，防止 macOS 原生 resize 行為
    setFixedSize(screenGeom.size());

    // Set bounds for selection manager
    m_selectionManager->setBounds(QRect(0, 0, screenGeom.width(), screenGeom.height()));

    // Configure magnifier panel with device pixel ratio
    m_magnifierPanel->setDevicePixelRatio(m_devicePixelRatio);
    m_magnifierPanel->invalidateCache();

    // Reset dirty tracking for optimized QRegion-based updates
    m_inputHandler->resetDirtyTracking();

    // Pre-compose dimmed background cache for fast painting on high-DPI screens
    m_painter->buildDimmedCache(m_backgroundPixmap);

    // Pre-warm magnifier cache to eliminate first-frame delay
    m_magnifierPanel->preWarmCache(m_inputState.currentPoint, m_backgroundPixmap);

    // Note: Cursor initialization is handled by ensureCrossCursor() called from
    // CaptureManager::initializeRegionSelector() AFTER show/activate. This avoids
    // a race condition where Selection context set here would be popped by
    // updateCursorFromStateForWidget() when mouse moves during initialization.

    // Initial window detection at cursor position
    if (m_windowDetector && m_windowDetector->isEnabled()) {
        updateWindowDetection(m_inputState.currentPoint);
    }

    if (m_screenSwitchTimer && !m_screenSwitchTimer->isActive()) {
        m_screenSwitchTimer->start();
    }
}

void RegionSelector::initializeWithRegion(QScreen* screen, const QRect& region)
{
    clearPreservedSelection();

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
    if (m_multiRegionListPanel) {
        m_multiRegionListPanel->setCaptureContext(m_backgroundPixmap, m_devicePixelRatio);
    }

    // Pre-compose dimmed background cache for fast painting on high-DPI screens
    m_painter->buildDimmedCache(m_backgroundPixmap);

    // Convert global region to local coordinates
    QRect screenGeom = m_currentScreen->geometry();
    QRect localRegion = region.translated(-screenGeom.topLeft());

    // Set selection from the region (this marks selection as complete)
    m_selectionManager->setSelectionRect(localRegion);
    m_selectionManager->setFromDetectedWindow(localRegion);

    // Set cursor position
    QPoint globalCursor = QCursor::pos();
    m_inputState.currentPoint = globalCursor - screenGeom.topLeft();

    // Trigger repaint to show toolbar (toolbar is drawn in paintEvent when m_selectionComplete is true)
    QTimer::singleShot(0, this, [this]() {
        update();
        });

    if (m_screenSwitchTimer && !m_screenSwitchTimer->isActive()) {
        m_screenSwitchTimer->start();
    }
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

void RegionSelector::switchToScreen(QScreen* screen, bool preserveCompletedSelection)
{
    QScreen* targetScreen = screen ? screen : QGuiApplication::primaryScreen();
    if (!targetScreen) {
        return;
    }
    if (m_currentScreen == targetScreen) {
        return;
    }

    if (!m_inputState.multiRegionMode &&
        preserveCompletedSelection &&
        m_selectionManager->isComplete()) {
        preserveCompletedSelectionSnapshot();
    }

    // Grab the target screen before moving this top-level overlay window.
    QPixmap newBackground = targetScreen->grabWindow(0);
    if (newBackground.isNull()) {
        qWarning() << "RegionSelector: Failed to capture target screen";
        return;
    }

    // --- Phase 1: Pre-compute expensive caches BEFORE moving the window ---
    const QRect targetGeometry = targetScreen->geometry();
    const qreal targetDpr = targetScreen->devicePixelRatio();

    m_painter->buildDimmedCache(newBackground);

    QPoint localCursor = QCursor::pos() - targetGeometry.topLeft();
    localCursor.setX(qBound(0, localCursor.x(), qMax(0, targetGeometry.width() - 1)));
    localCursor.setY(qBound(0, localCursor.y(), qMax(0, targetGeometry.height() - 1)));

    m_magnifierPanel->setDevicePixelRatio(targetDpr);
    m_magnifierPanel->invalidateCache();
    m_magnifierPanel->preWarmCache(localCursor, newBackground);

    // --- Phase 2: Hide window from DWM, reconfigure geometry + state ---
    // setUpdatesEnabled(false) only suppresses Qt paint events — DWM still composites
    // the old backing-store at the new position when setGeometry() moves the window.
    // Making the window transparent prevents DWM from showing stale content.
    setWindowOpacity(0.0);
    setUpdatesEnabled(false);

    setupScreenGeometry(targetScreen);
    if (!isScreenValid()) {
        qWarning() << "RegionSelector: Screen switch failed, no valid screen";
        setUpdatesEnabled(true);
        setWindowOpacity(1.0);
        return;
    }

    setGeometry(targetGeometry);

    m_backgroundPixmap = newBackground;
    m_sharedSourcePixmap = std::make_shared<const QPixmap>(m_backgroundPixmap);
    m_toolManager->setSourcePixmap(m_sharedSourcePixmap);
    m_toolManager->setDevicePixelRatio(m_devicePixelRatio);

    m_exportManager->setBackgroundPixmap(m_backgroundPixmap);
    m_exportManager->setDevicePixelRatio(m_devicePixelRatio);

    if (m_multiRegionManager) {
        m_multiRegionManager->clear();
    }
    if (m_inputState.multiRegionMode) {
        clearPreservedSelection();
        cancelMultiRegionReplace(false);
        refreshMultiRegionListPanel();
    }
    m_annotationLayer->clear();
    m_selectionManager->clearSelection();

    m_inputState.highlightedWindowRect = QRect();
    m_inputState.hasDetectedWindow = false;
    m_detectedWindow.reset();
    m_inputState.currentPoint = localCursor;
    m_inputHandler->resetDirtyTracking();

    // --- Phase 3: Paint correct content into backing store, then reveal ---
    setUpdatesEnabled(true);
    repaint();
    setWindowOpacity(1.0);

    refreshWindowDetectorForCurrentScreen();
    raiseWindowAboveMenuBar(this);
    activateWindow();
    raise();
    ensureCrossCursor();
}

void RegionSelector::handleCursorScreenSwitch()
{
    if (m_isClosing || !isVisible() || !isScreenValid()) {
        return;
    }
    if (m_isDialogOpen) {
        return;
    }
    if (m_textEditor->isEditing() ||
        (m_textAnnotationEditor && m_textAnnotationEditor->isEditing())) {
        return;
    }
    if (m_selectionManager->isSelecting() ||
        m_selectionManager->isResizing() ||
        m_selectionManager->isMoving() ||
        m_inputState.isDrawing) {
        return;
    }

    QScreen* cursorScreen = QGuiApplication::screenAt(QCursor::pos());
    if (!cursorScreen) {
        cursorScreen = QGuiApplication::primaryScreen();
    }
    if (!cursorScreen || m_currentScreen == cursorScreen) {
        return;
    }

    const bool preserveSingleSelection =
        !m_inputState.multiRegionMode && m_selectionManager->isComplete();
    switchToScreen(cursorScreen, preserveSingleSelection);
}

void RegionSelector::preserveCompletedSelectionSnapshot()
{
    if (!m_selectionManager->isComplete()) {
        return;
    }

    const QRect selectionRect = m_selectionManager->selectionRect();
    if (!selectionRect.isValid() || selectionRect.isEmpty()) {
        return;
    }

    const QPixmap selectedRegion = m_exportManager->getSelectedRegion(
        selectionRect, effectiveCornerRadius());
    if (selectedRegion.isNull()) {
        return;
    }

    m_preservedGlobalSelectionRect = QRect(
        localToGlobal(selectionRect.topLeft()),
        selectionRect.size());
    m_preservedSelectionPixmap = selectedRegion;
    m_hasPreservedSelection = m_preservedGlobalSelectionRect.isValid() &&
                              !m_preservedGlobalSelectionRect.isEmpty();
}

void RegionSelector::clearPreservedSelection()
{
    m_hasPreservedSelection = false;
    m_preservedGlobalSelectionRect = QRect();
    m_preservedSelectionPixmap = QPixmap();
}

void RegionSelector::finishPreservedSelection()
{
    if (!m_hasPreservedSelection ||
        m_preservedSelectionPixmap.isNull() ||
        !m_preservedGlobalSelectionRect.isValid() ||
        m_preservedGlobalSelectionRect.isEmpty()) {
        return;
    }

    emit regionSelected(m_preservedSelectionPixmap,
                        m_preservedGlobalSelectionRect.topLeft(),
                        m_preservedGlobalSelectionRect);
    close();
}

void RegionSelector::refreshWindowDetectorForCurrentScreen()
{
    if (!m_windowDetector || !m_windowDetector->isEnabled() || m_currentScreen.isNull()) {
        return;
    }

    m_windowDetector->setScreen(m_currentScreen.data());
    m_windowDetector->refreshWindowListAsync();

    if (m_windowDetector->isRefreshComplete()) {
        refreshWindowDetectionAtCursor();
    } else {
        connect(m_windowDetector, &WindowDetector::windowListReady,
                this, [this]() {
                    refreshWindowDetectionAtCursor();
                }, Qt::SingleShotConnection);
    }
}

void RegionSelector::refreshMultiRegionListPanel()
{
    if (!m_multiRegionListPanel || !m_multiRegionManager) {
        return;
    }

    m_multiRegionListRefreshPending = false;
    m_multiRegionListPanel->setCaptureContext(m_backgroundPixmap, m_devicePixelRatio);
    m_multiRegionListPanel->setRegions(m_multiRegionManager->regions());
    m_multiRegionListPanel->setActiveIndex(m_multiRegionManager->activeIndex());
}

void RegionSelector::beginMultiRegionReplace(int index)
{
    if (!m_inputState.multiRegionMode || !m_multiRegionManager) {
        return;
    }
    if (index < 0 || index >= m_multiRegionManager->count()) {
        return;
    }
    if (m_inputState.replaceTargetIndex >= 0 && m_inputState.replaceTargetIndex != index) {
        cancelMultiRegionReplace(false);
    }

    m_inputState.currentTool = ToolId::Selection;
    m_inputState.showSubToolbar = false;
    m_inputState.replaceTargetIndex = index;
    m_replaceOriginalRect = m_multiRegionManager->regionRect(index);

    m_multiRegionManager->setActiveIndex(index);
    m_selectionManager->clearSelection();
    m_selectionManager->setSelectionRect(m_replaceOriginalRect);
    update();
}

void RegionSelector::cancelMultiRegionReplace(bool restoreSelection)
{
    const int replaceIndex = m_inputState.replaceTargetIndex;
    if (replaceIndex < 0) {
        return;
    }

    if (restoreSelection && m_multiRegionManager &&
        replaceIndex < m_multiRegionManager->count()) {
        const QRect restoreRect = m_replaceOriginalRect.isValid()
            ? m_replaceOriginalRect
            : m_multiRegionManager->regionRect(replaceIndex);
        m_multiRegionManager->setActiveIndex(replaceIndex);
        m_selectionManager->clearSelection();
        if (restoreRect.isValid() && !restoreRect.isEmpty()) {
            m_selectionManager->setSelectionRect(restoreRect);
        }
    }

    m_inputState.replaceTargetIndex = -1;
    m_replaceOriginalRect = QRect();
    update();
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
        m_inputState.hasDetectedWindow = true;
        QRect localBounds = globalToLocal(detected->bounds).intersected(rect());

        if (localBounds != m_inputState.highlightedWindowRect) {
            // Calculate old visual rect for partial update
            QString oldTitle;
            if (!m_inputState.highlightedWindowRect.isNull()) {
                oldTitle = QString("%1x%2").arg(m_inputState.highlightedWindowRect.width()).arg(m_inputState.highlightedWindowRect.height());
            }
            QRect oldVisualRect = m_painter->getWindowHighlightVisualRect(m_inputState.highlightedWindowRect, oldTitle);

            m_inputState.highlightedWindowRect = localBounds;
            m_detectedWindow = detected;

            // Calculate new visual rect (use clipped local bounds, not global bounds)
            QString newTitle = QString("%1x%2").arg(localBounds.width()).arg(localBounds.height());
            QRect newVisualRect = m_painter->getWindowHighlightVisualRect(m_inputState.highlightedWindowRect, newTitle);

            // Update only changed regions
            if (!oldVisualRect.isNull()) update(oldVisualRect);
            if (!newVisualRect.isNull()) update(newVisualRect);
        }
    }
    else {
        m_inputState.hasDetectedWindow = false;
        if (!m_inputState.highlightedWindowRect.isNull()) {
            // Calculate old visual rect for partial update (use clipped local bounds)
            QString oldTitle;
            if (!m_inputState.highlightedWindowRect.isNull()) {
                oldTitle = QString("%1x%2").arg(m_inputState.highlightedWindowRect.width()).arg(m_inputState.highlightedWindowRect.height());
            }
            QRect oldVisualRect = m_painter->getWindowHighlightVisualRect(m_inputState.highlightedWindowRect, oldTitle);

            m_inputState.highlightedWindowRect = QRect();
            m_detectedWindow.reset();
            
            if (!oldVisualRect.isNull()) update(oldVisualRect);
        }
    }
}

void RegionSelector::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);

    // Use QRegion-based clipping for efficient partial repainting.
    // When update(QRegion) is used, QPaintEvent::region() contains the individual rects
    // rather than inflating to a bounding box. This dramatically reduces repaint area
    // for crosshair strips on high-DPI screens.
    const QRegion dirtyRegion = event->region();
    const QRect dirtyRect = dirtyRegion.boundingRect();
    painter.setClipRegion(dirtyRegion);

    // Disable SmoothPixmapTransform for background blit — the background pixmap
    // is at native device pixel ratio, so smooth scaling adds CPU cost with no visual benefit.
    // Antialiasing is enabled for selection handles, borders, and UI elements drawn after.
    painter.setRenderHint(QPainter::Antialiasing);

    // Update painter state before painting
    m_painter->setHighlightedWindowRect(m_inputState.highlightedWindowRect);
    m_painter->setDetectedWindowTitle(
        !m_inputState.highlightedWindowRect.isNull()
        ? QString("%1x%2").arg(m_inputState.highlightedWindowRect.width()).arg(m_inputState.highlightedWindowRect.height())
        : QString());
    m_painter->setCornerRadius(m_cornerRadius);
    m_painter->setShowSubToolbar(m_inputState.showSubToolbar);
    m_painter->setCurrentTool(static_cast<int>(m_inputState.currentTool));
    m_painter->setDevicePixelRatio(m_devicePixelRatio);
    m_painter->setMultiRegionMode(m_inputState.multiRegionMode);
    const bool showReplacePreview = m_inputState.multiRegionMode &&
        m_inputState.replaceTargetIndex >= 0 &&
        m_selectionManager->isSelecting();
    m_painter->setReplacePreview(
        showReplacePreview ? m_inputState.replaceTargetIndex : -1,
        showReplacePreview ? m_selectionManager->selectionRect().normalized() : QRect());

    // Delegate core painting (background, overlay, selection, annotations)
    m_painter->paint(painter, m_backgroundPixmap, dirtyRect);

    // Draw toolbar and widgets (UI management stays in RegionSelector)
    QRect selectionRect = m_selectionManager->selectionRect();
    if (m_selectionManager->hasActiveSelection() && selectionRect.isValid()) {
        if (m_selectionManager->hasSelection()) {
            // Update toolbar position and draw
            // Only show active indicator when sub-toolbar is visible
            m_toolbar->setActiveButton(m_inputState.showSubToolbar ? static_cast<int>(m_inputState.currentTool) : -1);
            m_toolbar->setViewportWidth(width());
            m_toolbar->setPositionForSelection(selectionRect, height());
            m_toolbar->draw(painter);

            if (m_inputState.multiRegionMode && m_multiRegionManager && m_multiRegionManager->count() > 0) {
                const int regionCount = m_multiRegionManager->count();
                QString countText = (regionCount == 1)
                    ? tr("%1 region").arg(regionCount)
                    : tr("%1 regions").arg(regionCount);
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
                ToolSectionConfig::forTool(m_inputState.currentTool).applyTo(m_colorAndWidthWidget);
                m_colorAndWidthWidget->setWidthSectionHidden(false);
                // Runtime-dependent setting for Mosaic auto blur
                if (m_inputState.currentTool == ToolId::Mosaic) {
                    m_colorAndWidthWidget->setAutoBlurEnabled(m_autoBlurManager != nullptr);
                }
                m_colorAndWidthWidget->updatePosition(m_toolbar->boundingRect(), false, width());
                m_colorAndWidthWidget->draw(painter);
            }
            else {
                m_colorAndWidthWidget->setVisible(false);
            }

            // Draw emoji picker when EmojiSticker tool is selected (lazy creation)
            if (m_inputState.currentTool == ToolId::EmojiSticker) {
                EmojiPicker* picker = ensureEmojiPicker();
                picker->setVisible(true);
                picker->updatePosition(m_toolbar->boundingRect(), false);
                picker->draw(painter);
            }
            else if (m_emojiPicker) {
                m_emojiPicker->setVisible(false);
            }

            m_toolbar->drawTooltip(painter);

            // Draw loading spinner when OCR, QR Code scan, or auto-blur is in progress
            if ((m_ocrInProgress || m_qrCodeInProgress || m_autoBlurInProgress) && m_loadingSpinner) {
                QPoint center = selectionRect.center();
                m_loadingSpinner->draw(painter, center);
            }
        }
    }

    // Draw crosshair at cursor - show during selection process and inside selection when complete
    // Hide magnifier when any annotation tool is selected or when drawing
    bool shouldShowCrosshair = !m_inputState.isDrawing && !isAnnotationTool(m_inputState.currentTool);
    if (m_selectionManager->isComplete()) {
        // Only show crosshair inside selection area, not on toolbar
        shouldShowCrosshair = shouldShowCrosshair &&
            selectionRect.contains(m_inputState.currentPoint) &&
            !m_toolbar->contains(m_inputState.currentPoint);
    }

    if (shouldShowCrosshair) {
        drawMagnifier(painter);
    }

}

void RegionSelector::drawMagnifier(QPainter& painter)
{
    // Delegate to MagnifierPanel component
    m_magnifierPanel->draw(painter, m_inputState.currentPoint, size(), m_backgroundPixmap);
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
    if (m_inputState.currentTool == ToolId::Text && m_textEditor->isEditing()) {
        return;
    }

    // Use CursorManager's unified tool cursor update
    // This delegates to IToolHandler::cursor() for proper cursor behavior
    cursorManager.updateToolCursorForWidget(this);
}

void RegionSelector::syncMultiRegionListPanelCursor()
{
    if (!m_multiRegionListPanel || !m_inputState.multiRegionMode || !m_multiRegionListPanel->isVisible()) {
        return;
    }

    if (!m_selectionManager) {
        return;
    }

    const bool isDraggingRegion = m_selectionManager->isSelecting() ||
                                  m_selectionManager->isResizing() ||
                                  m_selectionManager->isMoving();

    if (isDraggingRegion) {
        Qt::CursorShape interactionCursor = Qt::CrossCursor;
        if (m_selectionManager->isMoving()) {
            interactionCursor = Qt::SizeAllCursor;
        } else if (m_selectionManager->isResizing()) {
            const SelectionStateManager::ResizeHandle handle = m_selectionManager->activeResizeHandle();
            interactionCursor = handle != SelectionStateManager::ResizeHandle::None
                ? CursorManager::cursorForHandle(handle)
                : Qt::SizeAllCursor;
        }
        m_multiRegionListPanel->setInteractionCursor(interactionCursor);
    } else {
        m_multiRegionListPanel->clearInteractionCursor();
    }
}

void RegionSelector::handleToolbarClick(ToolId tool)
{
    // Sync current state to handler
    m_toolbarHandler->setCurrentTool(m_inputState.currentTool);
    m_toolbarHandler->setShowSubToolbar(m_inputState.showSubToolbar);
    m_toolbarHandler->setAnnotationWidth(m_inputState.annotationWidth);
    m_toolbarHandler->setAnnotationColor(m_inputState.annotationColor);
    m_toolbarHandler->setStepBadgeSize(m_stepBadgeSize);
    m_toolbarHandler->setOCRInProgress(m_ocrInProgress);
    m_toolbarHandler->setMultiRegionMode(m_inputState.multiRegionMode);

    // Delegate to handler
    m_toolbarHandler->handleToolbarClick(tool);
}

void RegionSelector::setMultiRegionMode(bool enabled)
{
    if (m_inputState.multiRegionMode == enabled) {
        return;
    }

    cancelMultiRegionReplace(false);
    m_inputState.multiRegionMode = enabled;
    m_painter->setMultiRegionMode(enabled);
    m_toolbarHandler->setMultiRegionMode(enabled);
    m_toolbarHandler->setupToolbarButtons();

    if (enabled) {
        clearPreservedSelection();

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
        m_inputState.currentTool = ToolId::Selection;
        m_inputState.showSubToolbar = false;
        if (m_multiRegionListPanel) {
            m_multiRegionListPanel->setCaptureContext(m_backgroundPixmap, m_devicePixelRatio);
            m_multiRegionListPanel->updatePanelGeometry(size());
            m_multiRegionListPanel->show();
            m_multiRegionListPanel->raise();
            m_multiRegionListPanel->clearInteractionCursor();
            refreshMultiRegionListPanel();
        }
    }
    else {
        m_multiRegionManager->clear();
        m_selectionManager->clearSelection();
        m_inputState.showSubToolbar = true;
        if (m_multiRegionListPanel) {
            m_multiRegionListPanel->clearInteractionCursor();
            m_multiRegionListPanel->hide();
            m_multiRegionListPanel->setRegions(QVector<MultiRegionManager::Region>());
        }
    }

    update();
}

void RegionSelector::completeMultiRegionCapture()
{
    if (!m_inputState.multiRegionMode || !m_multiRegionManager || m_multiRegionManager->count() == 0) {
        return;
    }

    // Merge regions from current screen only.
    QImage merged = m_multiRegionManager->mergeToSingleImage(m_backgroundPixmap, m_devicePixelRatio);
    if (merged.isNull()) {
        qWarning() << "RegionSelector: Failed to merge multi-region capture";
        QMessageBox::warning(this, tr("Error"), tr("Failed to merge regions. Please try again."));
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

bool RegionSelector::isMultiRegionCapture() const
{
    return m_inputState.multiRegionMode && m_multiRegionManager && m_multiRegionManager->count() > 1;
}

void RegionSelector::copyToClipboard()
{
    m_exportManager->copyToClipboard(
        m_selectionManager->selectionRect(), effectiveCornerRadius());
    // Note: close() will be called via copyCompleted signal connection
}

void RegionSelector::saveToFile()
{
    if (m_exportManager) {
        const int regionIndex = (m_inputState.multiRegionMode && m_multiRegionManager)
            ? m_multiRegionManager->activeIndex()
            : -1;
        m_exportManager->setRegionIndex(regionIndex);
        if (m_detectedWindow.has_value()) {
            m_exportManager->setWindowMetadata(m_detectedWindow->windowTitle, m_detectedWindow->ownerApp);
        } else {
            m_exportManager->setWindowMetadata(QString(), QString());
        }
    }

    // Delegate to export manager - it will emit signals for UI coordination
    m_exportManager->saveToFile(
        m_selectionManager->selectionRect(), effectiveCornerRadius(), nullptr);
    // Note: UI handling (hide/show/close) is done via signal connections
}

void RegionSelector::finishSelection()
{
    QRect sel = m_selectionManager->selectionRect();
    QPixmap selectedRegion = m_exportManager->getSelectedRegion(sel, effectiveCornerRadius());

    QRect globalRect(localToGlobal(sel.topLeft()), sel.size());
    emit regionSelected(selectedRegion, localToGlobal(sel.topLeft()), globalRect);
    close();
}

void RegionSelector::mousePressEvent(QMouseEvent* event)
{
    if (!m_inputState.multiRegionMode &&
        m_hasPreservedSelection &&
        event->button() == Qt::LeftButton) {
        clearPreservedSelection();
        m_selectionManager->clearSelection();
        m_annotationLayer->clear();
        if (m_multiRegionManager) {
            m_multiRegionManager->clear();
        }
    }

    m_inputHandler->handleMousePress(event);
    m_lastMagnifierRect = m_inputHandler->lastMagnifierRect();
    syncMultiRegionListPanelCursor();
}

void RegionSelector::mouseMoveEvent(QMouseEvent* event)
{
    m_inputHandler->handleMouseMove(event);
    m_lastMagnifierRect = m_inputHandler->lastMagnifierRect();
    syncMultiRegionListPanelCursor();
}

void RegionSelector::mouseReleaseEvent(QMouseEvent* event)
{
    m_inputHandler->handleMouseRelease(event);
    syncMultiRegionListPanelCursor();
}

void RegionSelector::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        return;
    }

    if (!m_selectionManager->isComplete()) {
        return;
    }

    if (m_toolManager &&
        m_toolManager->handleTextInteractionDoubleClick(event->pos())) {
        update();
        return;
    }

    // Forward other double-clicks to tool manager
    m_toolManager->handleDoubleClick(event->pos());
    update();
}

void RegionSelector::wheelEvent(QWheelEvent* event)
{
    // Handle scroll wheel for StepBadge size adjustment
    if (m_inputState.currentTool == ToolId::StepBadge) {
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
        if (m_inputState.multiRegionMode) {
            if (m_inputState.replaceTargetIndex >= 0) {
                cancelMultiRegionReplace(true);
            } else {
                cancelMultiRegionCapture();
            }
        }
        else {
            emit selectionCancelled();
            close();
        }
    }
    else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (m_inputState.multiRegionMode) {
            completeMultiRegionCapture();
        }
        else if (m_selectionManager->isComplete()) {
            finishSelection();
        }
        else if (!m_inputState.multiRegionMode && m_hasPreservedSelection) {
            finishPreservedSelection();
        }
    }
    else if (event->matches(QKeySequence::Copy)) {
        if (!m_inputState.multiRegionMode && m_selectionManager->isComplete()) {
            copyToClipboard();
        }
    }
    else if (event->matches(QKeySequence::Save)) {
        if (!m_inputState.multiRegionMode && m_selectionManager->isComplete()) {
            saveToFile();
        }
    }
    else if (event->key() == Qt::Key_M) {
        setMultiRegionMode(!m_inputState.multiRegionMode);
    }
    else if (event->key() == Qt::Key_R && !event->modifiers()) {
        if (m_selectionManager->isComplete()) {
            handleToolbarClick(ToolId::Record);
        }
    }
    else if (event->key() == Qt::Key_Shift) {
        // Switch RGB/HEX color format display (only when magnifier is shown)
        bool magnifierVisible = !m_inputState.isDrawing && !isAnnotationTool(m_inputState.currentTool);
        if (m_selectionManager->isComplete()) {
            QRect selectionRect = m_selectionManager->selectionRect();
            magnifierVisible = magnifierVisible &&
                selectionRect.contains(m_inputState.currentPoint) &&
                !m_toolbar->contains(m_inputState.currentPoint);
        }
        if (magnifierVisible) {
            m_magnifierPanel->toggleColorFormat();
            update();
        }
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
    }
    else if (event->matches(QKeySequence::Undo)) {
        if (m_inputState.multiRegionMode && m_multiRegionManager) {
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
        if (m_inputState.multiRegionMode && m_multiRegionManager) {
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
    if (m_screenSwitchTimer) {
        m_screenSwitchTimer->stop();
    }
    QWidget::closeEvent(event);
}

void RegionSelector::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    // Set cursor immediately on show (replaces unreliable 100ms timer)
    ensureCrossCursor();
}

void RegionSelector::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_multiRegionListPanel) {
        m_multiRegionListPanel->updatePanelGeometry(size());
    }
}

void RegionSelector::enterEvent(QEnterEvent* event)
{
    QWidget::enterEvent(event);

    // macOS resets cursor when mouse enters a window, so we need to set it here
    // Only set if not in selection/drawing mode (those have their own cursor logic)
    if (!m_selectionManager->hasSelection() && !m_inputState.isDrawing) {
        ensureCrossCursor();
    }
}

void RegionSelector::focusInEvent(QFocusEvent* event)
{
    QWidget::focusInEvent(event);

    // Ensure cursor is correct when window regains focus
    if (!m_selectionManager->hasSelection() && !m_inputState.isDrawing) {
        ensureCrossCursor();
    }
}

void RegionSelector::ensureCrossCursor()
{
    // Use Override context (highest priority 500) to guarantee cross cursor
    // during initialization. This prevents race conditions where:
    // 1. Mouse movement triggers updateCursorFromStateForWidget()
    // 2. inputState == Idle causes Selection context to be popped
    // 3. Hover context (150) overrides Tool context (100) with ArrowCursor
    // Override context beats all other contexts, ensuring cross cursor is shown.
    CursorManager::instance().pushCursorForWidget(
        this, CursorContext::Override, Qt::CrossCursor);
    forceNativeCrosshairCursor(this);

    // After initialization stabilizes, transition to normal Tool context.
    // This allows normal state-driven cursor updates to work properly.
    QTimer::singleShot(100, this, [this]() {
        if (!m_isClosing) {
            CursorManager::instance().popCursorForWidget(this, CursorContext::Override);
            CursorManager::instance().pushCursorForWidget(
                this, CursorContext::Tool, Qt::CrossCursor);
        }
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

            if (m_inputState.multiRegionMode) {
                if (m_inputState.replaceTargetIndex >= 0) {
                    cancelMultiRegionReplace(true);
                } else {
                    cancelMultiRegionCapture();
                }
                return true;
            }

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
            // Restore focus to re-enable mouse event delivery
            // Without this, Qt stops delivering mouse events to deactivated windows
            QTimer::singleShot(0, this, [this]() {
                activateWindow();
                raise();
                });

            return false;
        }
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
    return ToolTraits::isAnnotationTool(tool);
}

void RegionSelector::onTextEditingFinished(const QString& text, const QPoint& position)
{
    m_textAnnotationEditor->finishEditing(text, position, m_inputState.annotationColor);
}

void RegionSelector::performOCR()
{
    OCRManager* ocrMgr = ensureOCRManager();
    if (!ocrMgr || m_ocrInProgress || !m_selectionManager->isComplete()) {
        return;
    }

    m_ocrInProgress = true;
    ensureLoadingSpinner()->start();
    update();

    // Get the selected region (without annotations for OCR)
    const QRect sel = m_selectionManager->selectionRect();
    const QRect physicalRect = CoordinateHelper::toPhysicalCoveringRect(sel, m_devicePixelRatio);
    const QRect clampedPhysicalRect = physicalRect.intersected(m_backgroundPixmap.rect());
    if (clampedPhysicalRect.isEmpty()) {
        m_ocrInProgress = false;
        if (m_loadingSpinner) {
            m_loadingSpinner->stop();
        }
        showSelectionToast(tr("Failed to process selected region"), "rgba(200, 60, 60, 220)");
        update();
        return;
    }
    QPixmap selectedRegion = m_backgroundPixmap.copy(clampedPhysicalRect);
    selectedRegion.setDevicePixelRatio(m_devicePixelRatio);

    QPointer<RegionSelector> safeThis = this;
    ocrMgr->recognizeText(selectedRegion,
        [safeThis](const OCRResult& result) {
            if (safeThis) {
                safeThis->onOCRComplete(result);
            }
        });
}

void RegionSelector::onOCRComplete(const OCRResult& result)
{
    m_ocrInProgress = false;
    if (m_loadingSpinner) {
        m_loadingSpinner->stop();
    }

    if (result.success && !result.text.isEmpty()) {
        if (OCRSettingsManager::instance().behavior() == OCRBehavior::ShowEditor) {
            showOCRResultDialog(result);
            return;  // Dialog will handle the rest
        }

        QGuiApplication::clipboard()->setText(result.text);
        showSelectionToast(tr("Copied %1 characters").arg(result.text.length()),
                           "rgba(34, 139, 34, 220)");
    }
    else {
        QString msg = result.error.isEmpty() ? tr("No text found") : result.error;
        showSelectionToast(msg, "rgba(200, 60, 60, 220)");
    }

    update();
}

void RegionSelector::showOCRResultDialog(const OCRResult& result)
{
    // Create non-modal dialog
    auto *dialog = new OCRResultDialog(this);
    dialog->setOCRResult(result);

    // Connect signals
    connect(dialog, &OCRResultDialog::textCopied, this, [this](const QString &copiedText) {
        qDebug() << "OCR text copied:" << copiedText.length() << "characters";

        // Show success toast using GlobalToast
        GlobalToast::instance().showToast(
            GlobalToast::Success,
            tr("OCR"),
            tr("Copied %1 characters").arg(copiedText.length())
        );
    });

    connect(dialog, &OCRResultDialog::dialogClosed, this, [this]() {
        // Close the region selector after dialog closes
        close();
    });

    // Show dialog centered on screen
    dialog->showAt();
}

void RegionSelector::performQRCodeScan()
{
    QRCodeManager* qrMgr = ensureQRCodeManager();
    if (!qrMgr || m_qrCodeInProgress || !m_selectionManager->isComplete()) {
        return;
    }

    m_qrCodeInProgress = true;
    ensureLoadingSpinner()->start();
    update();

    // Get the selected region (without annotations)
    const QRect sel = m_selectionManager->selectionRect();
    const QRect physicalRect = CoordinateHelper::toPhysicalCoveringRect(sel, m_devicePixelRatio);
    const QRect clampedPhysicalRect = physicalRect.intersected(m_backgroundPixmap.rect());
    if (clampedPhysicalRect.isEmpty()) {
        m_qrCodeInProgress = false;
        if (m_loadingSpinner) {
            m_loadingSpinner->stop();
        }
        showSelectionToast(tr("Failed to process selected region"), "rgba(200, 60, 60, 220)");
        update();
        return;
    }
    QPixmap selectedRegion = m_backgroundPixmap.copy(clampedPhysicalRect);
    selectedRegion.setDevicePixelRatio(m_devicePixelRatio);

    QPointer<RegionSelector> safeThis = this;
    qrMgr->decode(selectedRegion,
        [safeThis, selectedRegion](const QRDecodeResult& result) {
            if (safeThis) {
                safeThis->onQRCodeComplete(result.success, result.text, result.format, result.error, selectedRegion);
            }
        });
}

void RegionSelector::onQRCodeComplete(bool success, const QString& text, const QString& format, const QString& error, const QPixmap &sourceImage)
{
    m_qrCodeInProgress = false;
    if (m_loadingSpinner) {
        m_loadingSpinner->stop();
    }

    if (success && !text.isEmpty()) {
        // Show result dialog
        auto *dialog = new QRCodeResultDialog(this);
        dialog->setPinActionAvailable(true);
        dialog->setResult(text, format, sourceImage);

        // Connect signals
        connect(dialog, &QRCodeResultDialog::textCopied, this, [this](const QString &copiedText) {
            qDebug() << "QR Code text copied:" << copiedText.length() << "characters";
        });

        connect(dialog, &QRCodeResultDialog::urlOpened, this, [](const QString &url) {
            qDebug() << "URL opened:" << url;
        });

        connect(dialog, &QRCodeResultDialog::qrCodeGenerated, this,
            [](const QImage &image, const QString &encodedText) {
                qDebug() << "QR Code generated:" << image.size() << "for" << encodedText.length() << "characters";
            });

        connect(dialog, &QRCodeResultDialog::pinGeneratedRequested, this,
            [this, dialog](const QPixmap &pixmap) {
                const QPoint globalTopLeft = dialog->mapToGlobal(QPoint(0, 0));
                // Generated QR content is synthetic and has no stable screen source region.
                // Emit an empty source rect so live-update is not mapped to unrelated screen content.
                const QRect globalRect;
                emit regionSelected(pixmap, globalTopLeft, globalRect);
                close();
            });

        connect(dialog, &QRCodeResultDialog::dialogClosed, this, [this]() {
            // Close the region selector after dialog closes
            close();
        });

        // Show dialog centered on screen
        dialog->showAt();
    }
    else {
        QString msg = error.isEmpty() ? tr("No QR code found") : error;
        showSelectionToast(msg, "rgba(200, 60, 60, 220)");
    }

    update();
}

void RegionSelector::performAutoBlur()
{
    if (!m_autoBlurManager || m_autoBlurInProgress || !m_selectionManager->isComplete()) {
        return;
    }

    m_autoBlurInProgress = true;
    m_colorAndWidthWidget->setAutoBlurProcessing(true);
    ensureLoadingSpinner()->start();
    update();

    // Reload latest settings before detection
    m_autoBlurManager->setOptions(AutoBlurSettingsManager::instance().load());

    // Get the selected region as QImage
    const QRect sel = m_selectionManager->selectionRect();
    const QRect physicalRect = CoordinateHelper::toPhysicalCoveringRect(sel, m_devicePixelRatio);
    const QRect clampedPhysicalRect = physicalRect.intersected(m_backgroundPixmap.rect());
    if (clampedPhysicalRect.isEmpty()) {
        m_autoBlurInProgress = false;
        m_colorAndWidthWidget->setAutoBlurProcessing(false);
        if (m_loadingSpinner) {
            m_loadingSpinner->stop();
        }
        showSelectionToast(tr("Failed to process selected region"), "rgba(200, 60, 60, 220)");
        update();
        return;
    }
    QPixmap selectedPixmap = m_backgroundPixmap.copy(clampedPhysicalRect);
    QImage selectedImage = selectedPixmap.toImage();
    const QSize selectedImageSize = selectedImage.size();

    const auto opts = m_autoBlurManager->options();
    const bool wantsFaceDetection = opts.detectFaces;
    bool runFaceDetection = false;
    if (wantsFaceDetection) {
        runFaceDetection = m_autoBlurManager->isInitialized() || m_autoBlurManager->initialize();
    }
    const QString faceUnavailableError =
        (wantsFaceDetection && !runFaceDetection) ? tr("Face detection unavailable") : QString();

    OCRManager* ocrManager = ensureOCRManager();
    const bool runCredentialDetection = (ocrManager != nullptr);

    if (!runFaceDetection && !runCredentialDetection) {
        m_autoBlurInProgress = false;
        m_colorAndWidthWidget->setAutoBlurProcessing(false);
        if (m_loadingSpinner) {
            m_loadingSpinner->stop();
        }
        showSelectionToast(
            faceUnavailableError.isEmpty() ? tr("Detection unavailable") : faceUnavailableError,
            "rgba(200, 60, 60, 220)");
        update();
        return;
    }

    struct AggregatedResult {
        QRect selectionRect;
        QRect clampedPhysicalRect;
        QVector<QRect> faceRegions;
        QVector<QRect> credentialRegions;
        bool facePending = false;
        bool ocrPending = false;
        bool faceAttempted = false;
        bool ocrAttempted = false;
        QString faceError;
        QString ocrError;
    };

    auto aggregated = std::make_shared<AggregatedResult>();
    aggregated->selectionRect = sel;
    aggregated->clampedPhysicalRect = clampedPhysicalRect;
    aggregated->facePending = runFaceDetection;
    aggregated->ocrPending = runCredentialDetection;
    aggregated->faceAttempted = wantsFaceDetection;
    aggregated->ocrAttempted = runCredentialDetection;
    aggregated->faceError = faceUnavailableError;

    QPointer<RegionSelector> safeThis = this;
    auto finishIfReady = [safeThis, aggregated]() {
        if (!safeThis || aggregated->facePending || aggregated->ocrPending) {
            return;
        }

        const int faceCount = aggregated->faceRegions.size();
        const int credentialCount = aggregated->credentialRegions.size();

        if (faceCount > 0 || credentialCount > 0) {
            QString warningMessage;
            if (!aggregated->faceError.isEmpty() && !aggregated->ocrError.isEmpty()) {
                warningMessage = QStringLiteral("%1; %2").arg(aggregated->faceError, aggregated->ocrError);
            } else if (!aggregated->faceError.isEmpty()) {
                warningMessage = aggregated->faceError;
            } else if (!aggregated->ocrError.isEmpty()) {
                warningMessage = aggregated->ocrError;
            }

            const auto options = safeThis->m_autoBlurManager->options();
            const int blockSize = AutoBlurManager::intensityToBlockSize(options.blurIntensity);
            const auto blurType = (options.blurType == AutoBlurSettingsManager::BlurType::Gaussian)
                ? MosaicRectAnnotation::BlurType::Gaussian
                : MosaicRectAnnotation::BlurType::Pixelate;

            auto addMosaicRegions = [&](const QVector<QRect>& regions) {
                for (const QRect& region : regions) {
                    const QRect absolutePhysicalRect =
                        region.translated(aggregated->clampedPhysicalRect.topLeft());
                    const QRect logicalRect =
                        CoordinateHelper::toLogical(absolutePhysicalRect, safeThis->m_devicePixelRatio)
                            .intersected(aggregated->selectionRect);
                    if (logicalRect.isEmpty()) {
                        continue;
                    }

                    auto mosaic = std::make_unique<MosaicRectAnnotation>(
                        logicalRect, safeThis->m_sharedSourcePixmap, blockSize, blurType
                    );
                    safeThis->m_annotationLayer->addItem(std::move(mosaic));
                }
            };

            addMosaicRegions(aggregated->faceRegions);
            addMosaicRegions(aggregated->credentialRegions);
            if (warningMessage.isEmpty()) {
                safeThis->onAutoBlurComplete(true, faceCount, credentialCount, QString());
                return;
            }

            QString partialMessage;
            if (faceCount > 0 && credentialCount > 0) {
                partialMessage = safeThis->tr("Blurred %1 face(s), %2 credential(s). %3")
                    .arg(faceCount)
                    .arg(credentialCount)
                    .arg(warningMessage);
            } else if (faceCount > 0) {
                partialMessage = safeThis->tr("Blurred %1 face(s). %2")
                    .arg(faceCount)
                    .arg(warningMessage);
            } else {
                partialMessage = safeThis->tr("Blurred %1 credential(s). %2")
                    .arg(credentialCount)
                    .arg(warningMessage);
            }
            safeThis->onAutoBlurComplete(false, faceCount, credentialCount, partialMessage);
            return;
        }

        QString errorMessage;
        if (!aggregated->faceError.isEmpty() && !aggregated->ocrError.isEmpty()) {
            errorMessage = QStringLiteral("%1; %2").arg(aggregated->faceError, aggregated->ocrError);
        } else if (!aggregated->faceError.isEmpty()) {
            errorMessage = aggregated->faceError;
        } else if (!aggregated->ocrError.isEmpty()) {
            errorMessage = aggregated->ocrError;
        }

        if (errorMessage.isEmpty()) {
            safeThis->onAutoBlurComplete(true, 0, 0, QString());
        } else {
            safeThis->onAutoBlurComplete(false, 0, 0, errorMessage);
        }
    };

    m_autoBlurWatcher = nullptr;

    if (runFaceDetection) {
        auto* watcher = new QFutureWatcher<AutoBlurManager::DetectionResult>(this);
        m_autoBlurWatcher = watcher;
        connect(watcher, &QFutureWatcher<AutoBlurManager::DetectionResult>::finished,
                this, [this, watcher, aggregated, finishIfReady]() {
            const auto result = watcher->result();
            m_autoBlurWatcher = nullptr;
            watcher->deleteLater();

            if (result.success) {
                aggregated->faceRegions = result.faceRegions;
            } else {
                aggregated->faceError = result.errorMessage;
            }

            aggregated->facePending = false;
            finishIfReady();
        });

        AutoBlurManager* manager = m_autoBlurManager;
        watcher->setFuture(QtConcurrent::run([manager, selectedImage]() {
            return manager->detect(selectedImage);
        }));
    }

    if (runCredentialDetection) {
        selectedPixmap.setDevicePixelRatio(1.0);
        ocrManager->recognizeText(selectedPixmap,
            [safeThis, aggregated, finishIfReady, selectedImageSize](const OCRResult& result) {
                if (!safeThis) {
                    return;
                }

                if (result.success && !result.blocks.isEmpty()) {
                    aggregated->credentialRegions =
                        CredentialDetector::detect(result.blocks, selectedImageSize);
                } else if (!result.success && !result.error.isEmpty()
                           && result.error != QStringLiteral("No text detected")) {
                    aggregated->ocrError = result.error;
                }

                aggregated->ocrPending = false;
                finishIfReady();
            });
    } else {
        aggregated->ocrPending = false;
        if (!runFaceDetection) {
            finishIfReady();
        }
    }
}

void RegionSelector::onAutoBlurComplete(bool success, int faceCount, int credentialCount, const QString& error)
{
    m_autoBlurInProgress = false;
    m_colorAndWidthWidget->setAutoBlurProcessing(false);
    if (m_loadingSpinner) {
        m_loadingSpinner->stop();
    }

    QString msg;
    QString bgColor;
    if (success && faceCount > 0 && credentialCount > 0) {
        msg = tr("Blurred %1 face(s), %2 credential(s)").arg(faceCount).arg(credentialCount);
        bgColor = "rgba(34, 139, 34, 220)";
    }
    else if (success && faceCount > 0) {
        msg = tr("Blurred %1 face(s)").arg(faceCount);
        bgColor = "rgba(34, 139, 34, 220)";  // Green for success
    }
    else if (success && credentialCount > 0) {
        msg = tr("Blurred %1 credential(s)").arg(credentialCount);
        bgColor = "rgba(34, 139, 34, 220)";
    }
    else if (success) {
        msg = tr("No faces or credentials detected");
        bgColor = "rgba(100, 100, 100, 220)";  // Gray for nothing found
    }
    else {
        msg = error.isEmpty() ? tr("Detection failed") : error;
        bgColor = "rgba(200, 60, 60, 220)";  // Red for failure
    }

    showSelectionToast(msg, bgColor);

    update();
}

void RegionSelector::showSelectionToast(const QString &message, const QString &bgColor)
{
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
    m_ocrToastLabel->setText(message);
    m_ocrToastLabel->adjustSize();
    QRect sel = m_selectionManager->selectionRect();
    int x = sel.center().x() - m_ocrToastLabel->width() / 2;
    int y = sel.top() + 12;
    m_ocrToastLabel->move(x, y);
    m_ocrToastLabel->show();
    m_ocrToastLabel->raise();
    m_ocrToastTimer->start(2500);
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
    m_inputState.arrowStyle = style;
    m_toolManager->setArrowStyle(style);
    AnnotationSettingsManager::instance().saveArrowStyle(style);
    update();
}

void RegionSelector::onLineStyleChanged(LineStyle style)
{
    m_inputState.lineStyle = style;
    m_toolManager->setLineStyle(style);
    AnnotationSettingsManager::instance().saveLineStyle(style);
    update();
}

void RegionSelector::onFontSizeDropdownRequested(const QPoint& pos)
{
    if (m_annotationContext && m_settingsHelper) {
        m_annotationContext->showTextFontSizeDropdown(*m_settingsHelper, pos);
    }
}

void RegionSelector::onFontFamilyDropdownRequested(const QPoint& pos)
{
    if (m_annotationContext && m_settingsHelper) {
        m_annotationContext->showTextFontFamilyDropdown(*m_settingsHelper, pos);
    }
}

void RegionSelector::onFontSizeSelected(int size)
{
    if (m_annotationContext) {
        m_annotationContext->applyTextFontSize(size);
    }
}

void RegionSelector::onFontFamilySelected(const QString& family)
{
    if (m_annotationContext) {
        m_annotationContext->applyTextFontFamily(family);
    }
}
