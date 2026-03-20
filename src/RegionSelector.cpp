#include "RegionSelector.h"
#include "annotation/AnnotationContext.h"
#include "history/AnnotationSerializer.h"
#include "history/HistoryRecorder.h"
#include "platform/WindowLevel.h"
#include "region/RegionPainter.h"
#include "region/RegionInputHandler.h"
#include "region/RegionToolbarHandler.h"
#include "region/RegionSettingsHelper.h"
#include "region/RegionExportManager.h"
#include "region/MagnifierOverlay.h"
#include "region/CaptureShortcutHintsOverlay.h"
#include "qml/QmlOverlayPanel.h"
#include "qml/RegionControlViewModel.h"
#include "qml/MultiRegionListViewModel.h"
#include "share/ShareUploadClient.h"
#include "annotations/AllAnnotations.h"
#include "cursor/CursorAuthority.h"
#include "cursor/CursorManager.h"
#include "GlassRenderer.h"
#include "ToolbarStyle.h"
#include "IconRenderer.h"
#include "qml/RegionToolbarViewModel.h"
#include "qml/PinToolOptionsViewModel.h"
#include "qml/QmlFloatingToolbar.h"
#include "qml/QmlFloatingSubToolbar.h"
#include "colorwidgets/ColorPickerDialogCompat.h"

using snaptray::colorwidgets::ColorPickerDialogCompat;
#include "qml/QmlEmojiPickerPopup.h"
#include "OCRManager.h"
#include "QRCodeManager.h"
#include "PlatformFeatures.h"
#include "detection/AutoBlurManager.h"
#include "detection/CredentialDetector.h"
#include "settings/AnnotationSettingsManager.h"
#include "settings/AutoBlurSettingsManager.h"
#include "settings/FileSettingsManager.h"
#include "settings/OCRSettingsManager.h"
#include "settings/PinWindowSettingsManager.h"
#include "settings/RegionCaptureSettingsManager.h"
#include "qml/OCRResultViewModel.h"
#include "qml/QRCodeResultViewModel.h"
#include "qml/QmlDialog.h"
#include "qml/QmlToast.h"
#include "qml/SharePasswordViewModel.h"
#include "qml/ShareResultViewModel.h"
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
#include <QApplication>
#include <QScreen>
#include <QDebug>
#include <QCursor>
#include <QQuickView>
#include <QMessageBox>
#include <QFile>
#include <QAbstractButton>
#include <QPushButton>
#include <QStandardPaths>
#include <QDateTime>
#include <numeric>
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

namespace {

QRect rectFromGlobal(QWidget* widget, const QRect& globalRect)
{
    if (!widget || !globalRect.isValid()) {
        return {};
    }

    return QRect(widget->mapFromGlobal(globalRect.topLeft()), globalRect.size());
}

bool containsGlobalPoint(const QWidget* widget, const QPoint& globalPos)
{
    return widget && widget->isVisible() && widget->frameGeometry().contains(globalPos);
}

CursorStyleSpec cursorSpecForWidget(const QWidget* widget)
{
    return widget ? CursorStyleSpec::fromCursor(widget->cursor())
                  : CursorStyleSpec::fromShape(Qt::ArrowCursor);
}

bool shouldSuppressCompletedSelectionDragUi(const SelectionStateManager* selectionManager)
{
    return selectionManager &&
           selectionManager->hasSelection() &&
           (selectionManager->isMoving() || selectionManager->isResizing());
}

QString physicalPixelSizeLabel(const QRect& logicalRect, qreal dpr)
{
    if (!logicalRect.isValid()) {
        return {};
    }

    const qreal effectiveDpr = dpr > 0.0 ? dpr : 1.0;
    const QSize physicalSize =
        CoordinateHelper::toPhysicalCoveringRect(logicalRect.normalized(), effectiveDpr).size();
    return QCoreApplication::translate(
        "RegionSelector", "%1 x %2 px").arg(physicalSize.width()).arg(physicalSize.height());
}

} // namespace

RegionSelector::RegionSelector(QWidget* parent)
    : QWidget(parent)
    , m_currentScreen(nullptr)
    , m_selectionManager(nullptr)
    , m_devicePixelRatio(1.0)
    , m_annotationLayer(nullptr)
    , m_isClosing(false)
    , m_saveDialogOpen(false)
    , m_dropdownOpen(false)
    , m_windowDetector(nullptr)
    , m_ocrManager(nullptr)
    , m_ocrInProgress(false)
    , m_qrCodeManager(nullptr)
    , m_qrCodeInProgress(false)
    , m_autoBlurManager(nullptr)
    , m_autoBlurInProgress(false)
    , m_textEditor(nullptr)
    , m_colorPickerDialog(nullptr)
    , m_magnifierPanel(nullptr)
    , m_shortcutHintsOverlay(nullptr)
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
            if (m_inputState.completedSelectionDragUiSuppressed && m_toolbarUserDragged) {
                m_toolbarUserDragged = false;
            }
            // Removed update() - repaints now handled by RegionInputHandler's throttled path
        });
    connect(m_selectionManager, &SelectionStateManager::stateChanged,
        this, [this](SelectionStateManager::State newState) {
            const bool suppressionBefore = m_inputState.completedSelectionDragUiSuppressed;
            const bool suppressionAfter = shouldSuppressCompletedSelectionDragUi(m_selectionManager);

            if (suppressionBefore != suppressionAfter) {
                m_inputState.completedSelectionDragUiSuppressed = suppressionAfter;
                updateCompletedSelectionDragUiSuppression();
            }

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
                // QML multi-region panel handles cursor internally
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
            if (m_multiRegionListViewModel) {
                m_multiRegionListViewModel->setActiveIndex(index);
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
    const MosaicBlurType mosaicBlurType = settings.loadMosaicBlurType();

    // Initialize tool manager
    m_toolManager = new ToolManager(this);
    m_toolManager->registerDefaultHandlers();
    m_toolManager->setAnnotationLayer(m_annotationLayer);
    m_toolManager->setColor(m_inputState.annotationColor);
    m_toolManager->setWidth(m_inputState.annotationWidth);
    m_toolManager->setArrowStyle(m_inputState.arrowStyle);
    m_toolManager->setLineStyle(m_inputState.lineStyle);
    m_toolManager->setMosaicBlurType(mosaicBlurType);
    connect(m_toolManager, &ToolManager::needsRepaint, this, QOverload<>::of(&QWidget::update));

    // Initialize cursor manager for centralized cursor handling
    auto& cursorManager = CursorManager::instance();
    cursorManager.registerWidget(this, m_toolManager);

    // OCR manager is lazy-initialized via ensureOCRManager() when first needed

    // Initialize auto-blur manager (lazy initialization - cascade loaded on first use)
    m_autoBlurManager = new AutoBlurManager(this);

    // Initialize QML toolbar ViewModels
    m_toolbarViewModel = new RegionToolbarViewModel(this);
    m_toolbarViewModel->setAutoBlurProcessing(m_autoBlurInProgress);
    m_toolOptionsViewModel = new PinToolOptionsViewModel(this);

    // Create QML floating toolbar windows
    m_qmlToolbar = std::make_unique<SnapTray::QmlFloatingToolbar>(
        m_toolbarViewModel, nullptr);
    m_qmlToolbar->setParentWidget(this);
    m_qmlSubToolbar = std::make_unique<SnapTray::QmlFloatingSubToolbar>(m_toolOptionsViewModel, nullptr);
    m_qmlSubToolbar->setParentWidget(this);

    // Connect toolbar ViewModel signals
    connect(m_toolbarViewModel, &RegionToolbarViewModel::toolSelected,
        this, [this](int toolId) { handleToolbarClick(static_cast<ToolId>(toolId)); });
    connect(m_toolbarViewModel, &RegionToolbarViewModel::undoClicked,
        this, [this]() { handleToolbarClick(ToolId::Undo); });
    connect(m_toolbarViewModel, &RegionToolbarViewModel::redoClicked,
        this, [this]() { handleToolbarClick(ToolId::Redo); });
    connect(m_toolbarViewModel, &RegionToolbarViewModel::cancelClicked,
        this, [this]() { handleToolbarClick(ToolId::Cancel); });
    connect(m_toolbarViewModel, &RegionToolbarViewModel::pinClicked,
        this, [this]() { handleToolbarClick(ToolId::Pin); });
    connect(m_toolbarViewModel, &RegionToolbarViewModel::recordClicked,
        this, [this]() { handleToolbarClick(ToolId::Record); });
    connect(m_toolbarViewModel, &RegionToolbarViewModel::saveClicked,
        this, [this]() { handleToolbarClick(ToolId::Save); });
    connect(m_toolbarViewModel, &RegionToolbarViewModel::copyClicked,
        this, [this]() { handleToolbarClick(ToolId::Copy); });
    connect(m_toolbarViewModel, &RegionToolbarViewModel::shareClicked,
        this, [this]() { handleToolbarClick(ToolId::Share); });
    connect(m_toolbarViewModel, &RegionToolbarViewModel::ocrClicked,
        this, [this]() { handleToolbarClick(ToolId::OCR); });
    connect(m_toolbarViewModel, &RegionToolbarViewModel::qrCodeClicked,
        this, [this]() { handleToolbarClick(ToolId::QRCode); });
    connect(m_toolbarViewModel, &RegionToolbarViewModel::multiRegionToggled,
        this, [this]() { handleToolbarClick(ToolId::MultiRegion); });
    connect(m_toolbarViewModel, &RegionToolbarViewModel::multiRegionDoneClicked,
        this, [this]() { handleToolbarClick(ToolId::MultiRegionDone); });

    // Initialize inline text editor
    m_textEditor = new InlineTextEditor(this);

    // Initialize text annotation editor component
    m_textAnnotationEditor = new TextAnnotationEditor(this);
    m_shapeAnnotationEditor = std::make_unique<ShapeAnnotationEditor>();
    m_shapeAnnotationEditor->setAnnotationLayer(m_annotationLayer);

    // Provide text dependencies to ToolManager (TextToolHandler).
    m_toolManager->setInlineTextEditor(m_textEditor);
    m_toolManager->setTextAnnotationEditor(m_textAnnotationEditor);
    m_toolManager->setShapeAnnotationEditor(m_shapeAnnotationEditor.get());
    m_toolManager->setTextEditingBounds(rect());
    m_toolManager->setTextColorSyncCallback([this](const QColor& color) {
        // Re-edit initialization should update runtime/UI state only.
        // Persisting here would rewrite default color without explicit user change.
        syncColorToRuntimeState(color);
    });
    m_toolManager->setHostFocusCallback([this]() {
        setFocus(Qt::OtherFocusReason);
    });

    // Initialize sub-toolbar ViewModel with persisted state
    m_toolOptionsViewModel->setCurrentColor(m_inputState.annotationColor);
    m_toolOptionsViewModel->setCurrentWidth(m_inputState.annotationWidth);
    m_toolOptionsViewModel->setArrowStyle(static_cast<int>(m_inputState.arrowStyle));
    m_toolOptionsViewModel->setLineStyle(static_cast<int>(m_inputState.lineStyle));

    // Centralized annotation/text setup and common signal wiring
    m_annotationContext = std::make_unique<AnnotationContext>(*this);
    m_annotationContext->setupTextAnnotationEditor();
    m_annotationContext->connectTextEditorSignals();

    // Connect sub-toolbar ViewModel signals (replaces connectToolOptionsSignals)
    connect(m_toolOptionsViewModel, &PinToolOptionsViewModel::colorSelected,
        this, &RegionSelector::onColorSelected);
    connect(m_toolOptionsViewModel, &PinToolOptionsViewModel::customColorPickerRequested,
        this, &RegionSelector::onMoreColorsRequested);
    connect(m_toolOptionsViewModel, &PinToolOptionsViewModel::widthValueChanged,
        this, &RegionSelector::onLineWidthChanged);
    connect(m_toolOptionsViewModel, &PinToolOptionsViewModel::arrowStyleSelected,
        this, [this](int style) { onArrowStyleChanged(static_cast<LineEndStyle>(style)); });
    connect(m_toolOptionsViewModel, &PinToolOptionsViewModel::lineStyleSelected,
        this, [this](int style) { onLineStyleChanged(static_cast<LineStyle>(style)); });
    connect(m_toolOptionsViewModel, &PinToolOptionsViewModel::fontSizeDropdownRequested,
        this, [this](double globalX, double globalY) {
            onFontSizeDropdownRequested(QPoint(qRound(globalX), qRound(globalY)));
        });
    connect(m_toolOptionsViewModel, &PinToolOptionsViewModel::fontFamilyDropdownRequested,
        this, [this](double globalX, double globalY) {
            onFontFamilyDropdownRequested(QPoint(qRound(globalX), qRound(globalY)));
        });
    connect(m_toolOptionsViewModel, &PinToolOptionsViewModel::arrowStyleDropdownRequested,
        this, [this](double globalX, double globalY) {
            if (!m_settingsHelper) {
                return;
            }
            m_settingsHelper->showArrowStyleDropdown(
                QPoint(qRound(globalX), qRound(globalY)),
                static_cast<LineEndStyle>(m_toolOptionsViewModel->arrowStyle()));
        });
    connect(m_toolOptionsViewModel, &PinToolOptionsViewModel::lineStyleDropdownRequested,
        this, [this](double globalX, double globalY) {
            if (!m_settingsHelper) {
                return;
            }
            m_settingsHelper->showLineStyleDropdown(
                QPoint(qRound(globalX), qRound(globalY)),
                static_cast<LineStyle>(m_toolOptionsViewModel->lineStyle()));
        });

    // Connect text formatting signals
    connect(m_toolOptionsViewModel, &PinToolOptionsViewModel::boldToggled,
        m_textAnnotationEditor, &TextAnnotationEditor::setBold);
    connect(m_toolOptionsViewModel, &PinToolOptionsViewModel::italicToggled,
        m_textAnnotationEditor, &TextAnnotationEditor::setItalic);
    connect(m_toolOptionsViewModel, &PinToolOptionsViewModel::underlineToggled,
        m_textAnnotationEditor, &TextAnnotationEditor::setUnderline);

    auto syncTextFormattingToSubToolbar = [this]() {
        if (!m_textAnnotationEditor) {
            return;
        }
        const auto formatting = m_textAnnotationEditor->formatting();
        if (m_textEditor && m_textEditor->isEditing()) {
            m_toolOptionsViewModel->setCurrentColor(m_textEditor->color());
        }
        m_toolOptionsViewModel->setBold(formatting.bold);
        m_toolOptionsViewModel->setItalic(formatting.italic);
        m_toolOptionsViewModel->setUnderline(formatting.underline);
        m_toolOptionsViewModel->setFontSize(formatting.fontSize);
        m_toolOptionsViewModel->setFontFamily(formatting.fontFamily);
    };
    connect(m_textAnnotationEditor, &TextAnnotationEditor::formattingChanged,
        this, syncTextFormattingToSubToolbar);
    syncTextFormattingToSubToolbar();

    // Connect shape/stepBadge/autoBlur signals via sub-toolbar ViewModel
    connect(m_toolOptionsViewModel, &PinToolOptionsViewModel::shapeTypeSelected,
        this, [this](int type) {
            m_inputState.shapeType = static_cast<ShapeType>(type);
            m_toolManager->setShapeType(type);
        });
    connect(m_toolOptionsViewModel, &PinToolOptionsViewModel::shapeFillModeSelected,
        this, [this](int mode) {
            m_inputState.shapeFillMode = static_cast<ShapeFillMode>(mode);
            m_toolManager->setShapeFillMode(mode);
        });
    connect(m_toolOptionsViewModel, &PinToolOptionsViewModel::stepBadgeSizeSelected,
        this, [this](int size) { onStepBadgeSizeChanged(static_cast<StepBadgeSize>(size)); });
    connect(m_toolOptionsViewModel, &PinToolOptionsViewModel::autoBlurRequested,
        this, &RegionSelector::performAutoBlur);
    // Track user drag to prevent paintEvent from overriding toolbar position
    connect(m_qmlToolbar.get(), &SnapTray::QmlFloatingToolbar::dragFinished,
        this, [this]() { m_toolbarUserDragged = true; });

    connect(m_qmlSubToolbar.get(), &SnapTray::QmlFloatingSubToolbar::emojiPickerRequested,
        this, [this]() { showEmojiPickerPopup(); });
    connect(m_annotationLayer, &AnnotationLayer::changed,
        this, [this]() {
            if (m_toolbarViewModel) {
                m_toolbarViewModel->setCanUndo(m_annotationLayer && m_annotationLayer->canUndo());
                m_toolbarViewModel->setCanRedo(m_annotationLayer && m_annotationLayer->canRedo());
            }
            update();
        });

    // EmojiPickerPopup is lazy-initialized via showEmojiPickerPopup() when first needed
    // LoadingSpinner is lazy-initialized via ensureLoadingSpinner() when first needed

    // Selection toast (shows OCR/QR/auto-blur results near selection)
    m_selectionToast = new SnapTray::QmlToast(this);

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
    m_magnifierOverlay = std::make_unique<MagnifierOverlay>(m_magnifierPanel);
    // Deliberately not a QML overlay window: this hint panel must share the
    // RegionSelector paint/lifecycle path to avoid native window ordering bugs.
    m_shortcutHintsOverlay = std::make_unique<CaptureShortcutHintsOverlay>();

    // Region control panel (QML - combines radius toggle + aspect ratio lock)
    m_regionControlViewModel = new RegionControlViewModel(this);

    int savedRatioWidth = settings.loadAspectRatioWidth();
    int savedRatioHeight = settings.loadAspectRatioHeight();
    m_regionControlViewModel->setLockedRatio(savedRatioWidth, savedRatioHeight);

    m_cornerRadius = settings.loadCornerRadius();
    m_regionControlViewModel->setCurrentRadius(m_cornerRadius);
    m_regionControlViewModel->setRadiusEnabled(settings.loadCornerRadiusEnabled());

    m_regionControlPanel = std::make_unique<SnapTray::QmlOverlayPanel>(
        QUrl(QStringLiteral("qrc:/SnapTrayQml/panels/RegionControlPanel.qml")),
        m_regionControlViewModel, QStringLiteral("viewModel"));
    m_regionControlPanel->setParentWidget(this);

    connect(m_regionControlViewModel, &RegionControlViewModel::radiusEnabledChanged,
        this, [](bool enabled) {
            AnnotationSettingsManager::instance().saveCornerRadiusEnabled(enabled);
        });

    connect(m_regionControlViewModel, &RegionControlViewModel::lockChanged,
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
            m_regionControlViewModel->setLockedRatio(ratioWidth, ratioHeight);
            settings.saveAspectRatio(ratioWidth, ratioHeight);
            m_selectionManager->setAspectRatio(static_cast<qreal>(ratioWidth) /
                static_cast<qreal>(ratioHeight));
            update();
        });

    connect(m_regionControlViewModel, &RegionControlViewModel::radiusChanged,
        this, &RegionSelector::onCornerRadiusChanged);

    // Multi-region list panel (QML - left dock)
    m_multiRegionListViewModel = new MultiRegionListViewModel(this);
    m_multiRegionListPanel = std::make_unique<SnapTray::QmlOverlayPanel>(
        QUrl(QStringLiteral("qrc:/SnapTrayQml/panels/MultiRegionListPanel.qml")),
        m_multiRegionListViewModel, QStringLiteral("viewModel"));
    m_multiRegionListPanel->setParentWidget(this);

    connect(m_multiRegionListViewModel, &MultiRegionListViewModel::regionActivated,
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
    connect(m_multiRegionListViewModel, &MultiRegionListViewModel::regionDeleteRequested,
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
    connect(m_multiRegionListViewModel, &MultiRegionListViewModel::regionReplaceRequested,
        this, &RegionSelector::beginMultiRegionReplace);
    connect(m_multiRegionListViewModel, &MultiRegionListViewModel::regionMoveRequested,
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
    connect(m_exportManager, &RegionExportManager::saveCompleted,
        this, [this](const QPixmap& pixmap, const QImage& image, const QString& filePath) {
            m_exportInProgress = false;
            if (m_loadingSpinner && !(m_ocrInProgress || m_qrCodeInProgress || m_autoBlurInProgress || m_shareInProgress)) {
                m_loadingSpinner->stop();
            }
            recordCaptureSession(image);
            emit saveCompleted(pixmap, filePath);
            close();
        });
    connect(m_exportManager, &RegionExportManager::saveFailed,
        this, [this](const QString& filePath, const QString& error) {
            m_exportInProgress = false;
            if (m_loadingSpinner && !(m_ocrInProgress || m_qrCodeInProgress || m_autoBlurInProgress || m_shareInProgress)) {
                m_loadingSpinner->stop();
            }
            emit saveFailed(filePath, error);
            restoreAfterDialogCancelled();
        });

    m_shareClient = new ShareUploadClient(this);
    connect(m_shareClient, &ShareUploadClient::uploadSucceeded,
        this, [this](const QString& url, const QDateTime& expiresAt, bool isProtected) {
            m_shareInProgress = false;
            if (m_toolbarHandler) {
                m_toolbarHandler->setShareInProgress(false);
            }
            m_toolbarViewModel->setShareInProgress(false);
            if (m_loadingSpinner && !(m_ocrInProgress || m_qrCodeInProgress || m_autoBlurInProgress)) {
                m_loadingSpinner->stop();
            }
            update();

            if (m_pendingShareSubmission.has_value()) {
                submitPendingHistorySubmission(std::move(*m_pendingShareSubmission));
                m_pendingShareSubmission.reset();
            }

            auto* vm = new ShareResultViewModel(this);
            vm->setResult(url, expiresAt, isProtected, m_pendingSharePassword);
            m_pendingSharePassword.clear();
            auto* dialog = createTransientDialog(
                QUrl("qrc:/SnapTrayQml/dialogs/ShareResultDialog.qml"),
                vm, "viewModel");
            trackBlockingDialog(dialog);
            connect(vm, &ShareResultViewModel::dialogClosed, this, [this, dialog]() {
                dialog->close();
                restoreAfterDialogCancelled();
            });
            dialog->showAt();
        });
    connect(m_shareClient, &ShareUploadClient::uploadFailed,
        this, [this](const QString& errorMessage) {
            m_shareInProgress = false;
            if (m_toolbarHandler) {
                m_toolbarHandler->setShareInProgress(false);
            }
            m_toolbarViewModel->setShareInProgress(false);
            if (m_loadingSpinner && !(m_ocrInProgress || m_qrCodeInProgress || m_autoBlurInProgress)) {
                m_loadingSpinner->stop();
            }
            m_selectionToast->showNearRect(SnapTray::QmlToast::Level::Error,
                errorMessage.isEmpty() ? tr("Failed to share screenshot") : errorMessage,
                m_selectionManager->selectionRect());
            m_pendingSharePassword.clear();
            m_pendingShareSubmission.reset();
            update();
        });

    // Initialize input handling component
    m_inputHandler = new RegionInputHandler(this);
    m_inputHandler->setSelectionManager(m_selectionManager);
    m_inputHandler->setAnnotationLayer(m_annotationLayer);
    m_inputHandler->setToolManager(m_toolManager);
    m_inputHandler->setTextEditor(m_textEditor);
    m_inputHandler->setTextAnnotationEditor(m_textAnnotationEditor);
    // EmojiPickerPopup is lazily created via showEmojiPickerPopup() when first needed
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
        this, [this](const QPoint& localPos) { updateWindowDetection(localPos); });
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
            } else {
                syncFloatingUiCursor();
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
            } else {
                syncFloatingUiCursor();
            }
        });
    connect(m_inputHandler, &RegionInputHandler::detectionCleared,
        this, [this](const QRect& previousHighlightRect) {
            QRect oldVisualRect;
            if (!previousHighlightRect.isNull()) {
                const QString oldTitle =
                    physicalPixelSizeLabel(previousHighlightRect, m_devicePixelRatio);
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
    m_toolbarHandler->setToolManager(m_toolManager);
    m_toolbarHandler->setAnnotationLayer(m_annotationLayer);

    // Connect toolbar handler signals (tool state changes)
    connect(m_toolbarHandler, &RegionToolbarHandler::toolChanged,
        this, [this](ToolId tool, bool showSubToolbar) {
            m_inputState.currentTool = tool;
            m_inputState.showSubToolbar = showSubToolbar;
            m_toolbarViewModel->setActiveTool(showSubToolbar ? static_cast<int>(tool) : -1);
            syncRegionSubToolbar(true);
            QTimer::singleShot(0, this, &RegionSelector::syncFloatingUiCursor);
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
    connect(m_toolbarHandler, &RegionToolbarHandler::shareRequested,
        this, &RegionSelector::shareToUrl);
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

    // Initialize settings helper
    m_settingsHelper = new RegionSettingsHelper(this);
    m_settingsHelper->setParentWidget(this);
    connect(m_settingsHelper, &RegionSettingsHelper::dropdownShown,
        this, [this]() {
            m_dropdownOpen = true;
            auto& authority = CursorAuthority::instance();
            if (QWidget* popup = QApplication::activePopupWidget()) {
                authority.submitWidgetRequest(
                    this, QStringLiteral("floating.popup"), CursorRequestSource::Popup,
                    cursorSpecForWidget(popup));
            }
            auto& cursorManager = CursorManager::instance();
            if (authority.modeForWidget(this) == CursorSurfaceMode::Authority) {
                cursorManager.reapplyCursorForWidget(this);
            } else {
                cursorManager.pushCursorForWidget(this, CursorContext::Override, Qt::ArrowCursor);
                cursorManager.reapplyCursorForWidget(this);
            }
        });
    connect(m_settingsHelper, &RegionSettingsHelper::dropdownHidden,
        this, [this]() {
            m_dropdownOpen = false;
            QTimer::singleShot(0, this, &RegionSelector::syncFloatingUiCursor);
        });
    connect(m_settingsHelper, &RegionSettingsHelper::fontSizeSelected,
        this, &RegionSelector::onFontSizeSelected);
    connect(m_settingsHelper, &RegionSettingsHelper::fontFamilySelected,
        this, &RegionSelector::onFontFamilySelected);
    connect(m_settingsHelper, &RegionSettingsHelper::arrowStyleSelected,
        this, &RegionSelector::onArrowStyleChanged);
    connect(m_settingsHelper, &RegionSettingsHelper::lineStyleSelected,
        this, &RegionSelector::onLineStyleChanged);

    // Initialize update throttler
    m_updateThrottler.startAll();

    // 注意: 不在此處初始化螢幕，由 CaptureManager 調用 initializeForScreen()
}

RegionSelector::~RegionSelector()
{
    if (m_autoBlurWatcher) {
        m_autoBlurWatcher->waitForFinished();
    }

    if (m_magnifierOverlay) {
        m_magnifierOverlay->hideOverlay();
    }

    // Clean up cursor state before destruction
    CursorManager::instance().clearAllForWidget(this);

    if (m_colorPickerDialog) {
        m_colorPickerDialog->close();
    }

    m_shapeAnnotationEditor.reset();

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

QWidget* RegionSelector::annotationHostWidget() const
{
    return const_cast<RegionSelector*>(this);
}

AnnotationLayer* RegionSelector::annotationLayerForContext() const
{
    return m_annotationLayer;
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
    m_toolOptionsViewModel->setCurrentColor(color);
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
    m_toolOptionsViewModel->setCurrentColor(m_inputState.annotationColor);

    AnnotationContext::showColorPickerDialog(
        this,
        m_colorPickerDialog,
        m_inputState.annotationColor,
        geometry().center(),
        [this](const QColor& color) {
            syncColorToAllWidgets(color);
        },
        m_colorPickerDialogFactory);
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

void RegionSelector::showEmojiPickerPopup()
{
    if (!m_emojiPickerPopup) {
        m_emojiPickerPopup = createEmojiPickerPopup();
        m_emojiPickerPopup->setParentWidget(this);
        connect(m_emojiPickerPopup, &SnapTray::QmlEmojiPickerPopup::emojiSelected,
            this, [this](const QString& emoji) {
                if (auto* handler = dynamic_cast<EmojiStickerToolHandler*>(
                    m_toolManager->handler(ToolId::EmojiSticker))) {
                    handler->setCurrentEmoji(emoji);
                }
            });
    }
    QRect anchor = m_qmlToolbar ? m_qmlToolbar->geometry() : QRect();
    m_emojiPickerPopup->showAt(anchor);
}

OCRManager* RegionSelector::ensureOCRManager()
{
    if (!m_ocrManager) {
        m_ocrManager = PlatformFeatures::instance().createOCRManager(this);
        if (m_ocrManager) {
            m_ocrManager->setRecognitionLanguages(OCRSettingsManager::instance().languages());
        }
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

void RegionSelector::setupScreenGeometry(QScreen* screen)
{
    m_currentScreen = screen ? screen : QGuiApplication::primaryScreen();
    if (m_currentScreen.isNull()) {
        qCritical() << "RegionSelector: No valid screen available";
        return;
    }
    m_devicePixelRatio = m_currentScreen->devicePixelRatio();

    QRect screenGeom = m_currentScreen->geometry();
    applyCanvasGeometry(screenGeom.size());

    if (m_exportManager) {
        const int monitorIndex = QGuiApplication::screens().indexOf(m_currentScreen.data());
        m_exportManager->setMonitorIdentifier(
            monitorIndex >= 0 ? QString::number(monitorIndex) : QStringLiteral("unknown"));
        m_exportManager->setSourceScreen(m_currentScreen.data());
    }
}

void RegionSelector::applyCanvasGeometry(const QSize& logicalSize)
{
    if (!logicalSize.isValid() || logicalSize.isEmpty()) {
        return;
    }

    if (m_currentScreen) {
        setGeometry(QRect(m_currentScreen->geometry().topLeft(), logicalSize));
    }
    setFixedSize(logicalSize);

    const QRect canvasRect(QPoint(0, 0), logicalSize);
    positionMultiRegionListPanel();
    m_selectionManager->setBounds(canvasRect);
    m_toolManager->setTextEditingBounds(canvasRect);

    m_inputState.currentPoint.setX(qBound(0, m_inputState.currentPoint.x(), qMax(0, logicalSize.width() - 1)));
    m_inputState.currentPoint.setY(qBound(0, m_inputState.currentPoint.y(), qMax(0, logicalSize.height() - 1)));
}

void RegionSelector::initializeForScreen(QScreen* screen, const QPixmap& preCapture)
{
    m_historyReplayEntries.clear();
    m_historyLiveSlot = {};
    m_historyReplayIndex = -1;
    m_historyReplayActive = false;
    ++m_postShowInitializationToken;
    m_postShowInitializationPending = false;
    clearPreservedSelection();
    m_shortcutHintsVisible = false;
    m_shortcutHintsTemporarilyHiddenByHover = false;
    if (m_regionControlPanel) m_regionControlPanel->hide();
    if (m_magnifierOverlay) m_magnifierOverlay->hideOverlay();

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
    m_exportManager->setSourceScreen(m_currentScreen.data());
    if (m_multiRegionListViewModel) {
        m_multiRegionListViewModel->setCaptureContext(m_backgroundPixmap, m_devicePixelRatio);
    }

    // 不預設選取整個螢幕，等待用戶操作
    m_selectionManager->clearSelection();

    // 將 cursor 全域座標轉換為 widget 本地座標，用於 crosshair/magnifier 初始位置
    QRect screenGeom = m_currentScreen->geometry();
    QPoint globalCursor = QCursor::pos();
    m_inputState.currentPoint = globalCursor - screenGeom.topLeft();
    applyCanvasGeometry(screenGeom.size());

    // Configure magnifier panel with device pixel ratio
    m_magnifierPanel->setDevicePixelRatio(m_devicePixelRatio);
    m_magnifierPanel->invalidateCache();

    // Reset dirty tracking for optimized QRegion-based updates
    m_inputHandler->resetDirtyTracking();

    // Note: Cursor initialization is handled by ensureCrossCursor() called from
    // CaptureManager::initializeRegionSelector() AFTER show/activate. This avoids
    // a race condition where Selection context set here would be popped by
    // updateCursorFromStateForWidget() when mouse moves during initialization.

    if (m_screenSwitchTimer && !m_screenSwitchTimer->isActive()) {
        m_screenSwitchTimer->start();
    }

    m_shortcutHintsVisible = !m_quickPinMode &&
        m_showShortcutHintsOnEntry &&
        RegionCaptureSettingsManager::instance().isShortcutHintsEnabled();
    m_shortcutHintsTemporarilyHiddenByHover = false;
    m_initialWindowDetectionMode = WindowDetector::QueryMode::TopLevelOnly;
    m_postShowInitializationPending = true;

}

void RegionSelector::initializeWithRegion(QScreen* screen, const QRect& region)
{
    m_historyReplayEntries.clear();
    m_historyLiveSlot = {};
    m_historyReplayIndex = -1;
    m_historyReplayActive = false;
    ++m_postShowInitializationToken;
    m_postShowInitializationPending = false;
    clearPreservedSelection();
    m_shortcutHintsVisible = false;
    m_shortcutHintsTemporarilyHiddenByHover = false;
    if (m_regionControlPanel) m_regionControlPanel->hide();
    if (m_magnifierOverlay) m_magnifierOverlay->hideOverlay();

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
    m_exportManager->setSourceScreen(m_currentScreen.data());
    if (m_multiRegionListViewModel) {
        m_multiRegionListViewModel->setCaptureContext(m_backgroundPixmap, m_devicePixelRatio);
    }

    // Convert global region to local coordinates
    QRect screenGeom = m_currentScreen->geometry();
    QRect localRegion = region.translated(-screenGeom.topLeft());

    // Set selection from the region (this marks selection as complete)
    m_selectionManager->setSelectionRect(localRegion);
    m_selectionManager->setFromDetectedWindow(localRegion);

    // Set cursor position
    QPoint globalCursor = QCursor::pos();
    m_inputState.currentPoint = globalCursor - screenGeom.topLeft();

    // Schedule a repaint so the floating QML toolbar can appear for the new selection.
    QTimer::singleShot(0, this, [this]() {
        update();
        });

    if (m_screenSwitchTimer && !m_screenSwitchTimer->isActive()) {
        m_screenSwitchTimer->start();
    }
}

bool RegionSelector::beginHistoryReplay(const QString& entryId)
{
    m_historyReplayEntries = SnapTray::HistoryStore::loadEntries();

    int entryIndex = -1;
    for (int i = 0; i < m_historyReplayEntries.size(); ++i) {
        if (m_historyReplayEntries.at(i).id == entryId) {
            entryIndex = i;
            break;
        }
    }

    if (entryIndex < 0) {
        return false;
    }

    ++m_postShowInitializationToken;
    m_postShowInitializationPending = false;

    if (!m_historyLiveSlot.valid) {
        snapshotLiveReplaySlot();
    }

    return loadHistoryReplayIndex(entryIndex);
}

bool RegionSelector::canNavigateHistoryReplay() const
{
    if (hasBlockingTransientUiOpen()) {
        return false;
    }
    if (m_textEditor->isEditing() ||
        (m_textAnnotationEditor && m_textAnnotationEditor->isEditing())) {
        return false;
    }
    if (m_selectionManager->isSelecting() ||
        m_selectionManager->isResizing() ||
        m_selectionManager->isMoving() ||
        m_inputState.isDrawing ||
        m_inputState.replaceTargetIndex >= 0) {
        return false;
    }
    return true;
}

void RegionSelector::navigateHistoryReplay(int direction)
{
    if (direction == 0 || !canNavigateHistoryReplay()) {
        return;
    }

    if (m_historyReplayEntries.isEmpty()) {
        m_historyReplayEntries = SnapTray::HistoryStore::loadEntries();
    }
    if (m_historyReplayEntries.isEmpty()) {
        return;
    }

    if (!m_historyLiveSlot.valid) {
        snapshotLiveReplaySlot();
    }

    int targetIndex = m_historyReplayIndex;
    if (direction < 0) {
        targetIndex = (m_historyReplayIndex < 0)
            ? 0
            : qMin(m_historyReplayIndex + 1, m_historyReplayEntries.size() - 1);
    }
    else {
        targetIndex = (m_historyReplayIndex <= 0) ? -1 : (m_historyReplayIndex - 1);
    }

    if (targetIndex == m_historyReplayIndex) {
        return;
    }

    if (targetIndex < 0) {
        restoreLiveReplaySlot();
        return;
    }

    loadHistoryReplayIndex(targetIndex);
}

void RegionSelector::snapshotLiveReplaySlot()
{
    m_historyLiveSlot.valid = true;
    m_historyLiveSlot.canvasLogicalSize = size();
    m_historyLiveSlot.backgroundPixmap = m_backgroundPixmap;
    m_historyLiveSlot.devicePixelRatio = m_devicePixelRatio;
    m_historyLiveSlot.selectionRect = currentHistorySelectionRect();
    m_historyLiveSlot.multiRegionMode = m_inputState.multiRegionMode;
    m_historyLiveSlot.multiRegions = currentHistoryCaptureRegions();
    m_historyLiveSlot.annotationsJson = SnapTray::serializeAnnotationLayer(*m_annotationLayer);
    m_historyLiveSlot.cornerRadius = m_cornerRadius;
}

void RegionSelector::restoreLiveReplaySlot()
{
    if (!m_historyLiveSlot.valid) {
        return;
    }

    clearHistoryReplaySelectionState();
    applyCanvasGeometry(m_historyLiveSlot.canvasLogicalSize);

    m_backgroundPixmap = m_historyLiveSlot.backgroundPixmap;
    m_devicePixelRatio = m_historyLiveSlot.devicePixelRatio;
    m_sharedSourcePixmap = std::make_shared<const QPixmap>(m_backgroundPixmap);
    m_toolManager->setSourcePixmap(m_sharedSourcePixmap);
    m_toolManager->setDevicePixelRatio(m_devicePixelRatio);
    m_exportManager->setBackgroundPixmap(m_backgroundPixmap);
    m_exportManager->setDevicePixelRatio(m_devicePixelRatio);
    m_exportManager->setSourceScreen(m_currentScreen.data());
    if (m_multiRegionListViewModel) {
        m_multiRegionListViewModel->setCaptureContext(m_backgroundPixmap, m_devicePixelRatio);
    }

    m_magnifierPanel->setDevicePixelRatio(m_devicePixelRatio);
    m_magnifierPanel->invalidateCache();
    m_magnifierPanel->preWarmCache(m_inputState.currentPoint, m_backgroundPixmap);

    m_cornerRadius = m_historyLiveSlot.cornerRadius;
    m_regionControlViewModel->setCurrentRadius(m_cornerRadius);
    m_painter->setCornerRadius(m_cornerRadius);

    if (m_historyLiveSlot.multiRegionMode) {
        setMultiRegionMode(true);
        if (m_multiRegionManager) {
            m_multiRegionManager->setRegions(m_historyLiveSlot.multiRegions);
            refreshMultiRegionListPanel();
        }
        m_selectionManager->clearSelection();
    }
    else {
        if (m_inputState.multiRegionMode) {
            setMultiRegionMode(false);
        }
        if (m_historyLiveSlot.selectionRect.isValid() && !m_historyLiveSlot.selectionRect.isEmpty()) {
            m_selectionManager->setSelectionRect(m_historyLiveSlot.selectionRect);
            m_selectionManager->setFromDetectedWindow(m_historyLiveSlot.selectionRect);
        }
        else {
            m_selectionManager->clearSelection();
        }
    }

    m_annotationLayer->clear();
    if (!m_historyLiveSlot.annotationsJson.isEmpty()) {
        QString errorMessage;
        SnapTray::deserializeAnnotationLayer(
            m_historyLiveSlot.annotationsJson, m_annotationLayer, m_sharedSourcePixmap, &errorMessage);
    }

    m_historyReplayIndex = -1;
    m_historyReplayActive = false;
    update();
}

bool RegionSelector::loadHistoryReplayIndex(int index)
{
    if (index < 0 || index >= m_historyReplayEntries.size()) {
        return false;
    }

    if (!applyHistoryReplayEntry(m_historyReplayEntries.at(index))) {
        return false;
    }

    m_historyReplayIndex = index;
    m_historyReplayActive = true;
    return true;
}

bool RegionSelector::applyHistoryReplayEntry(const SnapTray::HistoryEntry& entry)
{
    if (!entry.replayAvailable || entry.canvasPath.isEmpty()) {
        return false;
    }

    QPixmap canvas(entry.canvasPath);
    if (canvas.isNull()) {
        return false;
    }
    if (entry.devicePixelRatio > 0.0) {
        canvas.setDevicePixelRatio(entry.devicePixelRatio);
    }

    QByteArray annotationsData;
    if (!entry.annotationsPath.isEmpty()) {
        QFile annotationsFile(entry.annotationsPath);
        if (annotationsFile.open(QIODevice::ReadOnly)) {
            annotationsData = annotationsFile.readAll();
        }
    }

    clearHistoryReplaySelectionState();
    const QSize replayCanvasSize = entry.canvasLogicalSize.isValid()
        ? entry.canvasLogicalSize
        : CoordinateHelper::toLogical(
            canvas.size(),
            entry.devicePixelRatio > 0.0 ? entry.devicePixelRatio : 1.0);
    applyCanvasGeometry(replayCanvasSize);

    m_backgroundPixmap = canvas;
    m_devicePixelRatio = entry.devicePixelRatio > 0.0 ? entry.devicePixelRatio : 1.0;
    m_sharedSourcePixmap = std::make_shared<const QPixmap>(m_backgroundPixmap);
    m_toolManager->setSourcePixmap(m_sharedSourcePixmap);
    m_toolManager->setDevicePixelRatio(m_devicePixelRatio);
    m_exportManager->setBackgroundPixmap(m_backgroundPixmap);
    m_exportManager->setDevicePixelRatio(m_devicePixelRatio);
    m_exportManager->setSourceScreen(m_currentScreen.data());
    if (m_multiRegionListViewModel) {
        m_multiRegionListViewModel->setCaptureContext(m_backgroundPixmap, m_devicePixelRatio);
    }

    m_magnifierPanel->setDevicePixelRatio(m_devicePixelRatio);
    m_magnifierPanel->invalidateCache();
    m_magnifierPanel->preWarmCache(m_inputState.currentPoint, m_backgroundPixmap);

    m_cornerRadius = entry.cornerRadius;
    m_regionControlViewModel->setCurrentRadius(m_cornerRadius);
    m_painter->setCornerRadius(m_cornerRadius);

    if (!entry.captureRegions.isEmpty()) {
        setMultiRegionMode(true);
        if (m_multiRegionManager) {
            m_multiRegionManager->setRegions(entry.captureRegions);
            refreshMultiRegionListPanel();
        }
        m_selectionManager->clearSelection();
    }
    else {
        if (m_inputState.multiRegionMode) {
            setMultiRegionMode(false);
        }
        if (entry.selectionRect.isValid() && !entry.selectionRect.isEmpty()) {
            m_selectionManager->setSelectionRect(entry.selectionRect);
            m_selectionManager->setFromDetectedWindow(entry.selectionRect);
        }
    }

    m_annotationLayer->clear();
    if (!annotationsData.isEmpty()) {
        QString errorMessage;
        if (!SnapTray::deserializeAnnotationLayer(
                annotationsData, m_annotationLayer, m_sharedSourcePixmap, &errorMessage)) {
            m_annotationLayer->clear();
            return false;
        }
    }

    update();
    return true;
}

void RegionSelector::clearHistoryReplaySelectionState()
{
    clearPreservedSelection();
    cancelMultiRegionReplace(false);
    m_inputState.highlightedWindowRect = QRect();
    m_inputState.hasDetectedWindow = false;
    m_detectedWindow.reset();
    if (m_multiRegionManager) {
        m_multiRegionManager->clear();
    }
    m_selectionManager->clearSelection();
    m_annotationLayer->clear();
}

void RegionSelector::recordCaptureSession(const QPixmap& resultPixmap)
{
    if (resultPixmap.isNull()) {
        return;
    }

    auto snapshot = makeHistoryCaptureSnapshot();
    if (!snapshot.has_value()) {
        return;
    }

    const QPixmap resultPixmapCopy = resultPixmap;
    QTimer::singleShot(0, qApp, [snapshot = std::move(*snapshot), resultPixmapCopy]() mutable {
        if (resultPixmapCopy.isNull()) {
            return;
        }
        PendingHistorySubmission submission{
            std::move(snapshot),
            resultPixmapCopy.toImage()
        };
        if (submission.resultImage.isNull() || submission.snapshot.backgroundPixmap.isNull()) {
            return;
        }

        SnapTray::CaptureSessionWriteRequest request;
        request.canvasImage = submission.snapshot.backgroundPixmap.toImage();
        request.resultImage = submission.resultImage;
        request.selectionRect = submission.snapshot.selectionRect;
        request.captureRegions = submission.snapshot.captureRegions;
        request.annotationsJson = submission.snapshot.annotationsJson;
        request.devicePixelRatio = submission.snapshot.devicePixelRatio;
        request.canvasLogicalSize = submission.snapshot.canvasLogicalSize;
        request.cornerRadius = submission.snapshot.cornerRadius;
        request.maxEntries = submission.snapshot.maxEntries;
        request.createdAt = submission.snapshot.createdAt;
        SnapTray::HistoryRecorder::instance().submitCaptureSession(std::move(request));
    });
}

void RegionSelector::recordCaptureSession(const QImage& resultImage)
{
    if (resultImage.isNull()) {
        return;
    }

    auto snapshot = makeHistoryCaptureSnapshot();
    if (!snapshot.has_value()) {
        return;
    }

    submitPendingHistorySubmission(PendingHistorySubmission{
        std::move(*snapshot),
        resultImage
    });
}

std::optional<RegionSelector::HistoryCaptureSnapshot> RegionSelector::makeHistoryCaptureSnapshot(
    const QDateTime& createdAt) const
{
    if (m_backgroundPixmap.isNull() || !m_annotationLayer) {
        return std::nullopt;
    }

    const QRect selectionRect = currentHistorySelectionRect();
    if (!selectionRect.isValid() || selectionRect.isEmpty()) {
        return std::nullopt;
    }

    HistoryCaptureSnapshot snapshot;
    snapshot.backgroundPixmap = m_backgroundPixmap;
    snapshot.selectionRect = selectionRect;
    snapshot.captureRegions = currentHistoryCaptureRegions();
    snapshot.annotationsJson = SnapTray::serializeAnnotationLayer(*m_annotationLayer);
    snapshot.devicePixelRatio = m_devicePixelRatio;
    snapshot.canvasLogicalSize = size();
    snapshot.cornerRadius = m_cornerRadius;
    snapshot.maxEntries = PinWindowSettingsManager::instance().loadMaxCacheFiles();
    snapshot.createdAt = createdAt;
    return snapshot;
}

void RegionSelector::submitPendingHistorySubmission(PendingHistorySubmission submission) const
{
    QTimer::singleShot(0, qApp, [submission = std::move(submission)]() mutable {
        if (submission.resultImage.isNull() ||
            submission.snapshot.backgroundPixmap.isNull() ||
            !submission.snapshot.selectionRect.isValid() ||
            submission.snapshot.selectionRect.isEmpty()) {
            return;
        }

        SnapTray::CaptureSessionWriteRequest request;
        request.canvasImage = submission.snapshot.backgroundPixmap.toImage();
        request.resultImage = submission.resultImage;
        request.selectionRect = submission.snapshot.selectionRect;
        request.captureRegions = submission.snapshot.captureRegions;
        request.annotationsJson = submission.snapshot.annotationsJson;
        request.devicePixelRatio = submission.snapshot.devicePixelRatio;
        request.canvasLogicalSize = submission.snapshot.canvasLogicalSize;
        request.cornerRadius = submission.snapshot.cornerRadius;
        request.maxEntries = submission.snapshot.maxEntries;
        request.createdAt = submission.snapshot.createdAt;
        SnapTray::HistoryRecorder::instance().submitCaptureSession(std::move(request));
    });
}

QRect RegionSelector::currentHistorySelectionRect() const
{
    if (m_inputState.multiRegionMode && m_multiRegionManager && m_multiRegionManager->count() > 0) {
        return m_multiRegionManager->boundingBox();
    }
    return m_selectionManager->selectionRect();
}

QVector<MultiRegionManager::Region> RegionSelector::currentHistoryCaptureRegions() const
{
    if (m_inputState.multiRegionMode && m_multiRegionManager && m_multiRegionManager->count() > 0) {
        return m_multiRegionManager->regions();
    }
    return {};
}

void RegionSelector::setQuickPinMode(bool enabled)
{
    m_quickPinMode = enabled;
    if (enabled) {
        hideShortcutHints();
    }
}

void RegionSelector::setShowShortcutHintsOnEntry(bool enabled)
{
    m_showShortcutHintsOnEntry = enabled;
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
    ++m_postShowInitializationToken;
    m_postShowInitializationPending = false;

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
    if (m_magnifierOverlay) {
        m_magnifierOverlay->hideOverlay();
    }
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
    m_exportManager->setSourceScreen(m_currentScreen.data());

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
    if (m_isClosing || !isVisible() || !isScreenValid() || m_historyReplayActive) {
        return;
    }
    if (hasBlockingTransientUiOpen()) {
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

void RegionSelector::scheduleDeferredPostShowInitialization(
    WindowDetector::QueryMode initialQueryMode,
    const QPoint& initialCursorPos)
{
    const quint64 token = ++m_postShowInitializationToken;
    QTimer::singleShot(0, this, [this, token, initialQueryMode, initialCursorPos]() {
        runDeferredPostShowInitialization(token, initialQueryMode, initialCursorPos);
    });
}

void RegionSelector::runDeferredPostShowInitialization(
    quint64 token,
    WindowDetector::QueryMode initialQueryMode,
    const QPoint& initialCursorPos)
{
    if (m_postShowInitializationToken != token || m_isClosing || m_historyReplayActive ||
        !isVisible() || !isScreenValid()) {
        return;
    }

    m_magnifierPanel->setDevicePixelRatio(m_devicePixelRatio);
    m_magnifierPanel->invalidateCache();
    m_magnifierPanel->preWarmCache(initialCursorPos, m_backgroundPixmap);

    scheduleDeferredInitialWindowDetection(token, initialQueryMode, initialCursorPos);
}

void RegionSelector::scheduleDeferredInitialWindowDetection(
    quint64 token,
    WindowDetector::QueryMode queryMode,
    const QPoint& initialCursorPos)
{
    if (!m_windowDetector || !m_windowDetector->isEnabled() || m_historyReplayActive) {
        return;
    }

    const auto runDetection = [this, token, queryMode, initialCursorPos]() {
        if (m_postShowInitializationToken != token || m_isClosing || m_historyReplayActive ||
            !isVisible() || !isScreenValid()) {
            return;
        }

        if (m_inputState.currentPoint != initialCursorPos) {
            updateWindowDetection(m_inputState.currentPoint, queryMode);
            return;
        }

        refreshWindowDetectionAtCursor(queryMode);
    };

    if (m_windowDetector->isRefreshComplete()) {
        runDetection();
    } else {
        connect(m_windowDetector, &WindowDetector::windowListReady,
                this, [runDetection]() {
                    runDetection();
                }, Qt::SingleShotConnection);
    }
}

void RegionSelector::refreshMultiRegionListPanel()
{
    if (!m_multiRegionListViewModel || !m_multiRegionManager) {
        return;
    }

    m_multiRegionListRefreshPending = false;
    m_multiRegionListViewModel->setCaptureContext(m_backgroundPixmap, m_devicePixelRatio);
    m_multiRegionListViewModel->setRegions(m_multiRegionManager->regions());
    m_multiRegionListViewModel->setActiveIndex(m_multiRegionManager->activeIndex());
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
    if (m_toolManager) {
        m_toolManager->setCurrentTool(ToolId::Selection);
    }
    m_inputState.showSubToolbar = false;
    m_inputState.replaceTargetIndex = index;
    syncRegionSubToolbar(true);
    m_replaceOriginalRect = m_multiRegionManager->regionRect(index);

    m_multiRegionManager->setActiveIndex(index);
    m_selectionManager->clearSelection();
    m_selectionManager->setSelectionRect(m_replaceOriginalRect);
    syncFloatingUiCursor();
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
    refreshWindowDetectionAtCursor(WindowDetector::QueryMode::IncludeChildControls);
}

void RegionSelector::refreshWindowDetectionAtCursor(WindowDetector::QueryMode queryMode)
{
    if (!m_windowDetector || !m_windowDetector->isEnabled() || !m_currentScreen) {
        return;
    }

    const auto globalPos = QCursor::pos();
    const auto localPos = globalToLocal(globalPos);
    updateWindowDetection(localPos, queryMode);
}

void RegionSelector::updateWindowDetection(const QPoint& localPos)
{
    updateWindowDetection(localPos, WindowDetector::QueryMode::IncludeChildControls);
}

void RegionSelector::updateWindowDetection(const QPoint& localPos,
                                           WindowDetector::QueryMode queryMode)
{
    if (!m_windowDetector || !m_windowDetector->isEnabled() || !m_currentScreen) {
        return;
    }

    auto detected = m_windowDetector->detectWindowAt(localToGlobal(localPos), queryMode);

    if (detected.has_value()) {
        m_inputState.hasDetectedWindow = true;
        QRect localBounds = globalToLocal(detected->bounds).intersected(rect());

        if (localBounds != m_inputState.highlightedWindowRect) {
            // Calculate old visual rect for partial update
            QString oldTitle;
            if (!m_inputState.highlightedWindowRect.isNull()) {
                oldTitle = physicalPixelSizeLabel(m_inputState.highlightedWindowRect, m_devicePixelRatio);
            }
            QRect oldVisualRect = m_painter->getWindowHighlightVisualRect(m_inputState.highlightedWindowRect, oldTitle);

            m_inputState.highlightedWindowRect = localBounds;
            m_detectedWindow = detected;

            // Calculate new visual rect (use clipped local bounds, not global bounds)
            const QString newTitle = physicalPixelSizeLabel(localBounds, m_devicePixelRatio);
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
                oldTitle = physicalPixelSizeLabel(m_inputState.highlightedWindowRect, m_devicePixelRatio);
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
        ? physicalPixelSizeLabel(m_inputState.highlightedWindowRect, m_devicePixelRatio)
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

    // QML toolbars render themselves in separate windows.
    // Show/position them when selection is active.
    QRect selectionRect = m_selectionManager->selectionRect();
    const bool hasSelectableCapture =
        m_selectionManager->hasActiveSelection() &&
        selectionRect.isValid() &&
        m_selectionManager->hasSelection();
    const bool suppressSelectionFloatingUi = completedSelectionDragUiSuppressed();
    const bool shouldShowSelectionUi = hasSelectableCapture && !m_exportInProgress;
    const bool shouldDrawBusySpinner = hasSelectableCapture &&
        (m_ocrInProgress || m_qrCodeInProgress || m_autoBlurInProgress ||
         m_shareInProgress || m_exportInProgress);
    if (shouldShowSelectionUi && !suppressSelectionFloatingUi) {
        // Position and show QML toolbar
        if (m_qmlToolbar && !m_qmlToolbar->isVisible()) {
            m_qmlToolbar->show();
        }
        if (m_qmlToolbar) {
            if (!m_toolbarUserDragged) {
                m_qmlToolbar->positionForSelection(
                    selectionRect,
                    width(),
                    height(),
                    SnapTray::QmlFloatingToolbar::HorizontalAlignment::RightEdge);
            }
            // Update undo/redo state
            m_toolbarViewModel->setCanUndo(m_annotationLayer && m_annotationLayer->canUndo());
            m_toolbarViewModel->setCanRedo(m_annotationLayer && m_annotationLayer->canRedo());
            m_toolbarViewModel->setAutoBlurProcessing(m_autoBlurInProgress);
            if (m_emojiPickerPopup && m_emojiPickerPopup->isVisible()) {
                m_emojiPickerPopup->positionAt(m_qmlToolbar->geometry());
            }
        }

        syncRegionSubToolbar(false);

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

            QRect toolbarRect = floatingToolbarRectInLocalCoords();
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

        // Hide emoji picker popup when tool switches away from EmojiSticker
        if (m_inputState.currentTool != ToolId::EmojiSticker &&
            m_emojiPickerPopup && m_emojiPickerPopup->isVisible()) {
            m_emojiPickerPopup->hide();
        }
    }
    else {
        m_cursorOverSelectionToolbar = false;
        if (!hasSelectableCapture || m_exportInProgress) {
            m_toolbarUserDragged = false;
        }
        if (m_toolOptionsViewModel && (!hasSelectableCapture || m_exportInProgress)) {
            m_toolOptionsViewModel->clearSections();
        }
        hideSelectionFloatingUi(hasSelectableCapture && !m_exportInProgress);
    }

    if (shouldDrawBusySpinner && m_loadingSpinner) {
        m_loadingSpinner->draw(painter, selectionRect.center());
    }

    syncMagnifierOverlay();

    // Shortcut hints remain painter-based in the RegionSelector host window.
    if (m_shortcutHintsVisible && m_shortcutHintsOverlay) {
        m_shortcutHintsOverlay->draw(painter, size());
    }

    // Position region control panel after paint (painter computes dimension info rect)
    positionRegionControlPanel();
}

void RegionSelector::syncMagnifierOverlay()
{
    if (!m_magnifierOverlay) {
        return;
    }

    m_magnifierOverlay->syncToHost(
        this,
        m_inputState.currentPoint,
        &m_backgroundPixmap,
        shouldShowMagnifier());
}

void RegionSelector::positionRegionControlPanel()
{
    if (!m_regionControlPanel)
        return;

    bool shouldShow = m_selectionManager &&
                      m_selectionManager->hasSelection() &&
                      !m_exportInProgress &&
                      !completedSelectionDragUiSuppressed();
    if (shouldShow != m_regionControlPanel->isVisible()) {
        shouldShow ? m_regionControlPanel->show() : m_regionControlPanel->hide();
    }
    if (!shouldShow)
        return;

    QRect dimRect = m_painter->lastDimensionInfoRect();
    if (!dimRect.isValid())
        return;

    QRect panelGeom = m_regionControlPanel->geometry();
    int gap = 8;
    // The QML root is 60px tall: [popup 28] [gap 4] [main panel 28]
    // The visible main panel is the bottom 28px. Align it with dim label.
    int mainPanelHeight = 28;
    int popupOverhead = panelGeom.height() - mainPanelHeight;
    QPoint pos = mapToGlobal(QPoint(dimRect.right() + gap,
        dimRect.top() + (dimRect.height() - mainPanelHeight) / 2 - popupOverhead));

    // Fallback if off-screen right
    if (pos.x() + panelGeom.width() > mapToGlobal(QPoint(width(), 0)).x()) {
        pos = mapToGlobal(QPoint(dimRect.left(), dimRect.bottom() + 4 - popupOverhead));
    }
    m_regionControlPanel->setPosition(pos);
}

void RegionSelector::positionMultiRegionListPanel()
{
    if (!m_multiRegionListPanel || !m_multiRegionListPanel->isVisible())
        return;
    int margin = 12;
    QPoint pos = mapToGlobal(QPoint(margin, margin));
    m_multiRegionListPanel->setPosition(pos);
    m_multiRegionListPanel->resize(QSize(280, height() - 2 * margin));
}

void RegionSelector::hideSelectionFloatingUi(bool preserveToolState)
{
    if (m_qmlToolbar) {
        m_qmlToolbar->hide();
    }
    if (m_qmlSubToolbar) {
        m_qmlSubToolbar->hide();
    }
    if (m_regionControlPanel) {
        m_regionControlPanel->hide();
    }
    if (!preserveToolState && m_toolOptionsViewModel) {
        m_toolOptionsViewModel->clearSections();
    }
    if (m_emojiPickerPopup && m_emojiPickerPopup->isVisible()) {
        m_emojiPickerPopup->hide();
    }
    m_cursorOverSelectionToolbar = false;
    m_lastMagnifierRect = QRect();
}

void RegionSelector::updateCompletedSelectionDragUiSuppression()
{
    if (m_inputState.completedSelectionDragUiSuppressed) {
        hideSelectionFloatingUi(true);
    }

    update();
    syncMagnifierOverlay();

    if (!m_inputState.completedSelectionDragUiSuppressed) {
        QTimer::singleShot(0, this, &RegionSelector::syncFloatingUiCursor);
    }
}

bool RegionSelector::completedSelectionDragUiSuppressed() const
{
    return m_inputState.completedSelectionDragUiSuppressed;
}

void RegionSelector::hideShortcutHints()
{
    if (!m_shortcutHintsVisible) {
        m_shortcutHintsTemporarilyHiddenByHover = false;
        return;
    }

    m_shortcutHintsVisible = false;
    m_shortcutHintsTemporarilyHiddenByHover = false;
    update();
}

void RegionSelector::maybeDismissShortcutHintsAfterSelectionCompleted()
{
    if (!m_shortcutHintsVisible || !m_selectionManager) {
        return;
    }

    if (!m_selectionManager->isComplete()) {
        return;
    }

    hideShortcutHints();
}

void RegionSelector::updateShortcutHintsHoverVisibilityDuringSelection(const QPoint& localPos)
{
    if (!m_shortcutHintsOverlay || !m_selectionManager) {
        return;
    }

    const QRect panelRect = m_shortcutHintsOverlay->panelRectForViewport(size());
    if (!panelRect.isValid()) {
        return;
    }

    const bool isHoveringPanel = panelRect.contains(localPos);
    const QRect selectionRect = m_selectionManager->selectionRect().normalized();
    const bool isSelectionOverlappingPanel =
        m_selectionManager->hasActiveSelection() &&
        selectionRect.isValid() &&
        !selectionRect.isEmpty() &&
        selectionRect.intersects(panelRect);
    const bool shouldTemporarilyHide = isHoveringPanel || isSelectionOverlappingPanel;

    if (m_shortcutHintsTemporarilyHiddenByHover) {
        if (!shouldTemporarilyHide && !m_shortcutHintsVisible) {
            m_shortcutHintsVisible = true;
            m_shortcutHintsTemporarilyHiddenByHover = false;
            update(panelRect);
        }
        return;
    }

    if (shouldTemporarilyHide && m_shortcutHintsVisible) {
        m_shortcutHintsVisible = false;
        m_shortcutHintsTemporarilyHiddenByHover = true;
        update(panelRect);
    }
}

bool RegionSelector::isPureModifierKey(int key)
{
    switch (key) {
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Alt:
    case Qt::Key_Meta:
    case Qt::Key_AltGr:
        return true;
    default:
        return false;
    }
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
    cursorManager.reapplyCursorForWidget(this);
}

bool RegionSelector::isGlobalPosOverFloatingUi(const QPoint& globalPos) const
{
    if (m_qmlToolbar && m_qmlToolbar->isVisible() && m_qmlToolbar->geometry().contains(globalPos)) {
        return true;
    }

    if (m_qmlSubToolbar && m_qmlSubToolbar->isVisible() &&
        m_qmlSubToolbar->geometry().contains(globalPos)) {
        return true;
    }

    if (m_regionControlPanel && m_regionControlPanel->containsGlobalPoint(globalPos)) {
        return true;
    }

    if (m_multiRegionListPanel && m_multiRegionListPanel->containsGlobalPoint(globalPos)) {
        return true;
    }

    if (m_emojiPickerPopup && m_emojiPickerPopup->isVisible() &&
        m_emojiPickerPopup->geometry().contains(globalPos)) {
        return true;
    }

    if (QWidget* popup = QApplication::activePopupWidget(); containsGlobalPoint(popup, globalPos)) {
        return true;
    }

    return false;
}

void RegionSelector::restoreRegionCursorAt(const QPoint& localPos)
{
    auto& cursorManager = CursorManager::instance();
    cursorManager.popCursorForWidget(this, CursorContext::Override);

    if (!rect().contains(localPos)) {
        cursorManager.setHoverTargetForWidget(this, HoverTarget::None);
        cursorManager.reapplyCursorForWidget(this);
        return;
    }

    if (m_inputHandler) {
        m_inputHandler->syncHoverCursorAt(localPos);
    } else {
        cursorManager.setHoverTargetForWidget(this, HoverTarget::None);
        setToolCursor();
    }

    cursorManager.reapplyCursorForWidget(this);
}

void RegionSelector::hideDetachedFloatingUi()
{
    if (m_qmlToolbar) {
        m_qmlToolbar->hide();
    }
    if (m_qmlSubToolbar) {
        m_qmlSubToolbar->hide();
    }
    if (m_regionControlPanel) {
        m_regionControlPanel->hide();
    }
    if (m_multiRegionListPanel) {
        m_multiRegionListPanel->hide();
    }
    if (m_emojiPickerPopup && m_emojiPickerPopup->isVisible()) {
        m_emojiPickerPopup->hide();
    }
    if (m_magnifierOverlay) {
        m_magnifierOverlay->hideOverlay();
    }
}

void RegionSelector::restoreAfterDialogCancelled()
{
    if (m_restoreAfterDialogCancelledHook) {
        m_restoreAfterDialogCancelledHook();
        return;
    }

    const bool hasScreens = !QGuiApplication::screens().isEmpty();
    if (hasScreens) {
        show();
        activateWindow();
        raise();
    }
    if (m_inputState.multiRegionMode && m_multiRegionListPanel) {
        m_multiRegionListPanel->show();
        positionMultiRegionListPanel();
    }
    update();
    syncMagnifierOverlay();
    syncFloatingUiCursor();
}

bool RegionSelector::hasBlockingTransientUiOpen() const
{
    if (m_saveDialogOpen || m_dropdownOpen || m_openBlockingDialogCount > 0) {
        return true;
    }

    if (m_colorPickerDialog && m_colorPickerDialog->isVisible()) {
        return true;
    }

    if (QApplication::activePopupWidget() || QApplication::activeModalWidget()) {
        return true;
    }

    return false;
}

SnapTray::QmlDialog* RegionSelector::createTransientDialog(const QUrl& qmlSource,
                                                           QObject* viewModel,
                                                           const QString& contextPropertyName)
{
    if (m_dialogFactory) {
        return m_dialogFactory(qmlSource, viewModel, contextPropertyName, this);
    }

    return new SnapTray::QmlDialog(qmlSource, viewModel, contextPropertyName, this);
}

SnapTray::QmlEmojiPickerPopup* RegionSelector::createEmojiPickerPopup()
{
    if (m_emojiPickerFactory) {
        return m_emojiPickerFactory(this);
    }

    return new SnapTray::QmlEmojiPickerPopup(this);
}

void RegionSelector::trackBlockingDialog(SnapTray::QmlDialog* dialog)
{
    if (!dialog) {
        return;
    }

    ++m_openBlockingDialogCount;
    auto released = std::make_shared<bool>(false);
    auto release = [this, released]() {
        if (*released) {
            return;
        }

        *released = true;
        if (m_openBlockingDialogCount > 0) {
            --m_openBlockingDialogCount;
        }
    };

    connect(dialog, &SnapTray::QmlDialog::closed, this, release);
    connect(dialog, &QObject::destroyed, this, release);
}

bool RegionSelector::shouldShowMagnifier() const
{
    if (m_exportInProgress) {
        return false;
    }

    if (completedSelectionDragUiSuppressed()) {
        return false;
    }

    if (m_inputState.isDrawing || isAnnotationTool(m_inputState.currentTool)) {
        return false;
    }

    if (isGlobalPosOverFloatingUi(QCursor::pos())) {
        return false;
    }

    if (!m_selectionManager || !m_selectionManager->isComplete()) {
        return !m_cursorOverSelectionToolbar;
    }

    const QRect selectionRect = m_selectionManager->selectionRect();
    return selectionRect.contains(m_inputState.currentPoint) &&
           !m_cursorOverSelectionToolbar;
}

void RegionSelector::syncSelectionToolbarHoverState(const QPoint& globalPos)
{
    const bool cursorOverSelectionToolbar = isCursorOverSelectionToolbar(globalPos);
    if (m_cursorOverSelectionToolbar == cursorOverSelectionToolbar) {
        return;
    }

    m_cursorOverSelectionToolbar = cursorOverSelectionToolbar;

    if (!m_lastMagnifierRect.isNull()) {
        update(m_lastMagnifierRect);
    } else {
        update();
    }

    syncMagnifierOverlay();
}

bool RegionSelector::isCursorOverSelectionToolbar(const QPoint& globalPos) const
{
    return m_qmlToolbar &&
           m_qmlToolbar->isVisible() &&
           m_selectionManager &&
           m_selectionManager->hasSelection() &&
           m_qmlToolbar->geometry().contains(globalPos);
}

void RegionSelector::syncFloatingUiCursor()
{
    auto& cursorManager = CursorManager::instance();
    auto& authority = CursorAuthority::instance();
    const QPoint globalPos = QCursor::pos();

    syncSelectionToolbarHoverState(globalPos);

    bool popupMatched = false;
    if (QWidget* popup = QApplication::activePopupWidget();
        containsGlobalPoint(popup, globalPos)) {
        authority.submitWidgetRequest(
            this,
            QStringLiteral("floating.popup"),
            CursorRequestSource::Popup,
            cursorSpecForWidget(popup));
        popupMatched = true;
    } else {
        authority.clearWidgetRequest(this, QStringLiteral("floating.popup"));
    }

    authority.clearWidgetRequest(this, QStringLiteral("floating.overlay.toolbar"));
    authority.clearWidgetRequest(this, QStringLiteral("floating.overlay.subtoolbar"));
    authority.clearWidgetRequest(this, QStringLiteral("floating.overlay.regioncontrol"));
    authority.clearWidgetRequest(this, QStringLiteral("floating.overlay.multiregion"));
    authority.clearWidgetRequest(this, QStringLiteral("floating.overlay.emoji"));

    if (isGlobalPosOverFloatingUi(globalPos)) {
        if (m_magnifierOverlay) {
            m_magnifierOverlay->hideOverlay();
        }
        cursorManager.popCursorForWidget(this, CursorContext::Override);
        return;
    }

    if (!popupMatched) {
        authority.clearWidgetRequest(this, QStringLiteral("floating.popup"));
        authority.clearWidgetRequest(this, QStringLiteral("floating.overlay.emoji"));
    }

    restoreRegionCursorAt(globalToLocal(globalPos));
}


void RegionSelector::handleToolbarClick(ToolId tool)
{
    if (m_exportInProgress) {
        return;
    }

    finalizePolylineForToolbarInteraction();

    // Sync current state to handler
    m_toolbarHandler->setCurrentTool(m_inputState.currentTool);
    m_toolbarHandler->setShowSubToolbar(m_inputState.showSubToolbar);
    m_toolbarHandler->setAnnotationWidth(m_inputState.annotationWidth);
    m_toolbarHandler->setStepBadgeSize(m_stepBadgeSize);
    m_toolbarHandler->setShareInProgress(m_shareInProgress);
    m_toolbarHandler->setMultiRegionMode(m_inputState.multiRegionMode);

    // Delegate to handler
    m_toolbarHandler->handleToolbarClick(tool);
}

bool RegionSelector::ensureAutoBlurReadyForExport()
{
    if (!m_autoBlurInProgress) {
        return true;
    }

    if (m_selectionToast && m_selectionManager) {
        m_selectionToast->showNearRect(SnapTray::QmlToast::Level::Error,
            tr("Please wait for auto-blur to finish"),
            m_selectionManager->selectionRect());
    }
    return false;
}

void RegionSelector::updateToolbarAutoBlurState()
{
    if (m_toolbarViewModel) {
        m_toolbarViewModel->setAutoBlurProcessing(m_autoBlurInProgress);
    }
}

void RegionSelector::syncRegionSubToolbar(bool refreshContent)
{
    if (!m_qmlSubToolbar || !m_toolOptionsViewModel) {
        return;
    }

    const bool allowSubToolbar = m_inputState.showSubToolbar &&
                                 !completedSelectionDragUiSuppressed() &&
                                 !m_inputState.multiRegionMode &&
                                 m_inputState.replaceTargetIndex < 0;
    if (!allowSubToolbar) {
        if (!completedSelectionDragUiSuppressed()) {
            m_toolOptionsViewModel->showForTool(-1);
        }
        m_qmlSubToolbar->hide();
        return;
    }

    const bool shouldRefreshContent = refreshContent ||
                                      !m_qmlSubToolbar->isVisible() ||
                                      !m_toolOptionsViewModel->hasContent();
    if (shouldRefreshContent) {
        if (m_inputState.currentTool == ToolId::Mosaic) {
            m_toolOptionsViewModel->setAutoBlurEnabled(m_autoBlurManager != nullptr);
        }
        m_qmlSubToolbar->showForTool(static_cast<int>(m_inputState.currentTool));
    }

    if (m_qmlToolbar && m_qmlToolbar->isVisible() && m_qmlSubToolbar->isVisible()) {
        m_qmlSubToolbar->positionBelow(m_qmlToolbar->geometry());
    }
}

void RegionSelector::finalizePolylineForToolbarInteraction()
{
    if (!m_toolManager ||
        m_toolManager->currentTool() != ToolId::Arrow ||
        !m_toolManager->isDrawing()) {
        return;
    }

    const QPoint toolbarClickPos = globalToLocal(QCursor::pos());
    m_toolManager->handleDoubleClick(toolbarClickPos);
    m_inputState.isDrawing = m_toolManager->isDrawing();
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
    m_toolbarViewModel->setMultiRegionMode(enabled);

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
        if (m_toolManager) {
            m_toolManager->setCurrentTool(ToolId::Selection);
        }
        m_inputState.showSubToolbar = false;
        syncRegionSubToolbar(true);
        if (m_multiRegionListViewModel) {
            m_multiRegionListViewModel->setCaptureContext(m_backgroundPixmap, m_devicePixelRatio);
        }
        if (m_multiRegionListPanel) {
            m_multiRegionListPanel->show();
            positionMultiRegionListPanel();
        }
        refreshMultiRegionListPanel();
        syncFloatingUiCursor();
    }
    else {
        m_multiRegionManager->clear();
        m_selectionManager->clearSelection();
        m_inputState.showSubToolbar = true;
        syncRegionSubToolbar(true);
        if (m_multiRegionListPanel) {
            m_multiRegionListPanel->hide();
        }
        if (m_multiRegionListViewModel) {
            m_multiRegionListViewModel->setRegions(QVector<MultiRegionManager::Region>());
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
    recordCaptureSession(pixmap);
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
    if (m_exportInProgress || !ensureAutoBlurReadyForExport()) {
        return;
    }

    const RegionExportManager::PreparedExport prepared = m_exportManager->prepareExport(
        m_selectionManager->selectionRect(), effectiveCornerRadius());
    if (!prepared.isValid()) {
        if (m_selectionToast) {
            m_selectionToast->showNearRect(SnapTray::QmlToast::Level::Error,
                tr("Failed to process selected region"), m_selectionManager->selectionRect());
        }
        return;
    }

    recordCaptureSession(prepared.image);
    emit copyRequested(prepared.pixmap);

    const QImage clipboardImage = prepared.image;
    const auto clipboardWriter = m_guiClipboardWriter;
    QTimer::singleShot(0, qApp, [clipboardImage, clipboardWriter]() {
        if (clipboardWriter) {
            clipboardWriter(clipboardImage);
            return;
        }
        PlatformFeatures::instance().copyImageToClipboardForGui(clipboardImage);
    });
    close();
}

void RegionSelector::saveToFile()
{
    if (m_exportInProgress || !ensureAutoBlurReadyForExport()) {
        return;
    }

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

    const QRect selectionRect = m_selectionManager->selectionRect();
    const bool usesDialog = !FileSettingsManager::instance().loadAutoSaveScreenshots();
    if (usesDialog) {
        m_saveDialogOpen = true;
        hideDetachedFloatingUi();
        hide();
    }

    const RegionExportManager::SaveRequest saveRequest =
        m_exportManager->createSaveRequest(selectionRect, nullptr);

    if (usesDialog) {
        m_saveDialogOpen = false;
    }

    if (saveRequest.cancelled) {
        restoreAfterDialogCancelled();
        return;
    }

    if (!saveRequest.error.isEmpty()) {
        emit saveFailed(saveRequest.filePath, saveRequest.error);
        restoreAfterDialogCancelled();
        return;
    }

    RegionExportManager::PreparedExport prepared =
        m_exportManager->prepareExport(selectionRect, effectiveCornerRadius());
    if (!prepared.isValid()) {
        emit saveFailed(
            saveRequest.filePath,
            tr("Failed to save screenshot: unable to process selection"));
        restoreAfterDialogCancelled();
        return;
    }

    m_exportInProgress = true;
    hideDetachedFloatingUi();
    if (!QGuiApplication::screens().isEmpty()) {
        show();
        activateWindow();
        raise();
    }
    ensureLoadingSpinner()->start();
    update();

    m_exportManager->savePreparedExportAsync(
        std::move(prepared), saveRequest.filePath, saveRequest.renderWarning);
}

void RegionSelector::shareToUrl()
{
    if (m_exportInProgress || m_shareInProgress || m_ocrInProgress || m_qrCodeInProgress ||
        !m_shareClient || !m_selectionManager || !m_selectionManager->isComplete()) {
        return;
    }

    if (!ensureAutoBlurReadyForExport()) {
        return;
    }

    const QPixmap selectedRegion = m_exportManager->getSelectedRegion(
        m_selectionManager->selectionRect(), effectiveCornerRadius());
    if (selectedRegion.isNull()) {
        m_selectionToast->showNearRect(SnapTray::QmlToast::Level::Error,
            tr("Failed to process selected region"), m_selectionManager->selectionRect());
        return;
    }

    auto* vm = new SharePasswordViewModel(this);
    auto* dialog = createTransientDialog(
        QUrl("qrc:/SnapTrayQml/dialogs/SharePasswordDialog.qml"),
        vm, "viewModel");
    trackBlockingDialog(dialog);
    connect(vm, &SharePasswordViewModel::accepted, this, [this, vm, dialog, selectedRegion]() {
        dialog->close();
        if (m_shareInProgress || !m_shareClient) {
            return;
        }

        m_pendingSharePassword = vm->password();
        auto snapshot = makeHistoryCaptureSnapshot(QDateTime::currentDateTime());
        if (snapshot.has_value()) {
            m_pendingShareSubmission = PendingHistorySubmission{
                std::move(*snapshot),
                selectedRegion.toImage()
            };
        } else {
            m_pendingShareSubmission.reset();
        }

        m_shareInProgress = true;
        if (m_toolbarHandler) {
            m_toolbarHandler->setShareInProgress(true);
        }
        m_toolbarViewModel->setShareInProgress(true);
        ensureLoadingSpinner()->start();
        update();

        m_shareClient->uploadPixmap(selectedRegion, m_pendingSharePassword);
    });
    connect(vm, &SharePasswordViewModel::rejected, this, [this, dialog]() {
        m_pendingSharePassword.clear();
        m_pendingShareSubmission.reset();
        dialog->close();
        restoreAfterDialogCancelled();
    });
    dialog->setModal(true);
    dialog->showAt();
}

void RegionSelector::finishSelection()
{
    if (!ensureAutoBlurReadyForExport()) {
        return;
    }

    QRect sel = m_selectionManager->selectionRect();
    QPixmap selectedRegion = m_exportManager->getSelectedRegion(sel, effectiveCornerRadius());

    QRect globalRect(localToGlobal(sel.topLeft()), sel.size());
    recordCaptureSession(selectedRegion);
    emit regionSelected(selectedRegion, localToGlobal(sel.topLeft()), globalRect);
    close();
}

void RegionSelector::mousePressEvent(QMouseEvent* event)
{
    if (m_exportInProgress) {
        event->ignore();
        return;
    }

    if (isGlobalPosOverFloatingUi(event->globalPosition().toPoint())) {
        return;
    }

    maybeDismissShortcutHintsAfterSelectionCompleted();

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
    // QML multi-region panel handles cursor internally
}

void RegionSelector::mouseMoveEvent(QMouseEvent* event)
{
    if (m_exportInProgress) {
        event->ignore();
        return;
    }

    if (isGlobalPosOverFloatingUi(event->globalPosition().toPoint())) {
        syncFloatingUiCursor();
        return;
    }

    m_inputHandler->handleMouseMove(event);
    updateShortcutHintsHoverVisibilityDuringSelection(event->pos());
    m_lastMagnifierRect = m_inputHandler->lastMagnifierRect();
    // QML multi-region panel handles cursor internally
}

void RegionSelector::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_exportInProgress) {
        event->ignore();
        return;
    }

    if (isGlobalPosOverFloatingUi(event->globalPosition().toPoint())) {
        return;
    }

    m_inputHandler->handleMouseRelease(event);
    updateShortcutHintsHoverVisibilityDuringSelection(event->pos());
    // QML multi-region panel handles cursor internally
}

void RegionSelector::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (m_exportInProgress) {
        event->ignore();
        return;
    }

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
    if (m_exportInProgress) {
        event->ignore();
        return;
    }

    maybeDismissShortcutHintsAfterSelectionCompleted();

    int delta = event->angleDelta().y();
    if (delta == 0) {
        event->ignore();
        return;
    }

    // Handle scroll wheel for StepBadge size adjustment
    if (m_inputState.currentTool == ToolId::StepBadge) {
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
        m_toolOptionsViewModel->setStepBadgeSize(static_cast<int>(newSize));
        event->accept();
        return;
    }

    const bool allowWidthWheelAdjustment =
        m_selectionManager &&
        m_selectionManager->isComplete() &&
        m_inputState.showSubToolbar &&
        !m_inputState.multiRegionMode &&
        m_inputState.replaceTargetIndex < 0;
    if (allowWidthWheelAdjustment && m_toolOptionsViewModel->handleWidthWheelDelta(delta)) {
        event->accept();
        return;
    }

    event->ignore();
}

void RegionSelector::keyPressEvent(QKeyEvent* event)
{
    if (m_exportInProgress) {
        event->ignore();
        return;
    }

    if (!isPureModifierKey(event->key())) {
        maybeDismissShortcutHintsAfterSelectionCompleted();
    }

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

    if (!event->modifiers() && event->key() == Qt::Key_Comma) {
        navigateHistoryReplay(-1);
        event->accept();
        return;
    }

    if (!event->modifiers() && event->key() == Qt::Key_Period) {
        navigateHistoryReplay(1);
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Escape) {
        if (hasBlockingTransientUiOpen()) {
            event->ignore();
            return;
        }

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
        if (shouldShowMagnifier()) {
            m_magnifierPanel->toggleColorFormat();
            syncMagnifierOverlay();
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
    if (m_exportInProgress) {
        event->ignore();
        return;
    }

    m_isClosing = true;
    ++m_postShowInitializationToken;
    m_postShowInitializationPending = false;
    m_pendingSharePassword.clear();
    m_pendingShareSubmission.reset();
    if (m_qmlToolbar) m_qmlToolbar->hide();
    if (m_qmlSubToolbar) m_qmlSubToolbar->hide();
    if (m_magnifierOverlay) m_magnifierOverlay->hideOverlay();
    if (m_emojiPickerPopup) {
        m_emojiPickerPopup->close();
        delete m_emojiPickerPopup;
        m_emojiPickerPopup = nullptr;
    }
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
    syncMagnifierOverlay();

    if (m_postShowInitializationPending) {
        m_postShowInitializationPending = false;
        scheduleDeferredPostShowInitialization(m_initialWindowDetectionMode,
                                               m_inputState.currentPoint);
    }
}

void RegionSelector::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    positionMultiRegionListPanel();
    syncMagnifierOverlay();
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
            // Let the active dialog (save/share/OCR/QR) consume Esc so this
            // global handler does not cancel the entire capture session.
            if (hasBlockingTransientUiOpen()) {
                return false;
            }

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
        if (hasBlockingTransientUiOpen()) {
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
    if (!ocrMgr || m_exportInProgress || m_ocrInProgress || m_shareInProgress ||
        !m_selectionManager->isComplete()) {
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
        m_selectionToast->showNearRect(SnapTray::QmlToast::Level::Error,
            tr("Failed to process selected region"), m_selectionManager->selectionRect());
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
        m_selectionToast->showNearRect(SnapTray::QmlToast::Level::Success,
            tr("Copied %1 characters").arg(result.text.length()),
            m_selectionManager->selectionRect());
    }
    else {
        QString msg = result.error.isEmpty() ? tr("No text found") : result.error;
        m_selectionToast->showNearRect(SnapTray::QmlToast::Level::Error,
            msg, m_selectionManager->selectionRect());
    }

    update();
}

void RegionSelector::showOCRResultDialog(const OCRResult& result)
{
    auto* vm = new OCRResultViewModel(this);
    vm->setOCRResult(result);

    auto* dialog = createTransientDialog(
        QUrl("qrc:/SnapTrayQml/dialogs/OCRResultDialog.qml"),
        vm, "viewModel");
    trackBlockingDialog(dialog);

    connect(vm, &OCRResultViewModel::textCopied, this, [this](const QString& copiedText) {
        qDebug() << "OCR text copied:" << copiedText.length() << "characters";

        SnapTray::QmlToast::screenToast().showToast(
            SnapTray::QmlToast::Level::Success,
            tr("OCR"),
            tr("Copied %1 characters").arg(copiedText.length())
        );
    });

    connect(vm, &OCRResultViewModel::dialogClosed, this, [this, dialog]() {
        dialog->close();
        restoreAfterDialogCancelled();
    });

    dialog->showAt();
}

void RegionSelector::performQRCodeScan()
{
    QRCodeManager* qrMgr = ensureQRCodeManager();
    if (!qrMgr || m_exportInProgress || m_qrCodeInProgress || m_shareInProgress ||
        !m_selectionManager->isComplete()) {
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
        m_selectionToast->showNearRect(SnapTray::QmlToast::Level::Error,
            tr("Failed to process selected region"), m_selectionManager->selectionRect());
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
        auto* vm = new QRCodeResultViewModel(this);
        vm->setPinActionAvailable(true);
        vm->setResult(text, format, sourceImage);
        const QPoint selectionGlobalTopLeft =
            localToGlobal(m_selectionManager->selectionRect().topLeft());

        auto* dialog = createTransientDialog(
            QUrl("qrc:/SnapTrayQml/dialogs/QRCodeResultDialog.qml"),
            vm, "viewModel");
        trackBlockingDialog(dialog);

        connect(vm, &QRCodeResultViewModel::textCopied, this, [](const QString& copiedText) {
            qDebug() << "QR Code text copied:" << copiedText.length() << "characters";
        });

        connect(vm, &QRCodeResultViewModel::urlOpened, this, [](const QString& url) {
            qDebug() << "URL opened:" << url;
        });

        connect(vm, &QRCodeResultViewModel::qrCodeGenerated, this,
            [](const QImage& image, const QString& encodedText) {
                qDebug() << "QR Code generated:" << image.size() << "for" << encodedText.length() << "characters";
            });

        connect(vm, &QRCodeResultViewModel::pinGeneratedRequested, this,
            [this, dialog, selectionGlobalTopLeft](const QPixmap& pixmap) {
                // Generated QR codes are synthetic content, so they have no source-region
                // metadata. Open the pin near the current selection instead of (0, 0).
                const QRect globalRect;
                emit regionSelected(pixmap, selectionGlobalTopLeft, globalRect);
                dialog->close();
                close();
            });

        connect(vm, &QRCodeResultViewModel::dialogClosed, this, [this, dialog]() {
            dialog->close();
            restoreAfterDialogCancelled();
        });

        dialog->showAt();
    }
    else {
        QString msg = error.isEmpty() ? tr("No QR code found") : error;
        m_selectionToast->showNearRect(SnapTray::QmlToast::Level::Error,
            msg, m_selectionManager->selectionRect());
    }

    update();
}

void RegionSelector::performAutoBlur()
{
    if (!m_autoBlurManager || m_exportInProgress || m_autoBlurInProgress || m_shareInProgress ||
        !m_selectionManager->isComplete()) {
        return;
    }

    m_autoBlurInProgress = true;
    updateToolbarAutoBlurState();
    m_toolOptionsViewModel->setAutoBlurProcessing(true);
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
        updateToolbarAutoBlurState();
        m_toolOptionsViewModel->setAutoBlurProcessing(false);
        if (m_loadingSpinner) {
            m_loadingSpinner->stop();
        }
        m_selectionToast->showNearRect(SnapTray::QmlToast::Level::Error,
            tr("Failed to process selected region"), m_selectionManager->selectionRect());
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
        updateToolbarAutoBlurState();
        m_toolOptionsViewModel->setAutoBlurProcessing(false);
        if (m_loadingSpinner) {
            m_loadingSpinner->stop();
        }
        m_selectionToast->showNearRect(SnapTray::QmlToast::Level::Error,
            faceUnavailableError.isEmpty() ? tr("Detection unavailable") : faceUnavailableError,
            m_selectionManager->selectionRect());
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
            const auto blurType = options.blurType;

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
    updateToolbarAutoBlurState();
    m_toolOptionsViewModel->setAutoBlurProcessing(false);
    if (m_loadingSpinner) {
        m_loadingSpinner->stop();
    }

    QString msg;
    auto level = SnapTray::QmlToast::Level::Info;
    if (success && faceCount > 0 && credentialCount > 0) {
        msg = tr("Blurred %1 face(s), %2 credential(s)").arg(faceCount).arg(credentialCount);
        level = SnapTray::QmlToast::Level::Success;
    }
    else if (success && faceCount > 0) {
        msg = tr("Blurred %1 face(s)").arg(faceCount);
        level = SnapTray::QmlToast::Level::Success;
    }
    else if (success && credentialCount > 0) {
        msg = tr("Blurred %1 credential(s)").arg(credentialCount);
        level = SnapTray::QmlToast::Level::Success;
    }
    else if (success) {
        msg = tr("No faces or credentials detected");
        level = SnapTray::QmlToast::Level::Info;
    }
    else {
        msg = error.isEmpty() ? tr("Detection failed") : error;
        level = SnapTray::QmlToast::Level::Error;
    }

    m_selectionToast->showNearRect(level, msg, m_selectionManager->selectionRect());

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

QRect RegionSelector::floatingToolbarRectInLocalCoords() const
{
    return rectFromGlobal(const_cast<RegionSelector*>(this),
                          m_qmlToolbar ? m_qmlToolbar->geometry() : QRect());
}

void RegionSelector::onArrowStyleChanged(LineEndStyle style)
{
    if (m_toolOptionsViewModel) {
        m_toolOptionsViewModel->setArrowStyle(static_cast<int>(style));
    }
    m_inputState.arrowStyle = style;
    m_toolManager->setArrowStyle(style);
    AnnotationSettingsManager::instance().saveArrowStyle(style);
    update();
}

void RegionSelector::onLineStyleChanged(LineStyle style)
{
    if (m_toolOptionsViewModel) {
        m_toolOptionsViewModel->setLineStyle(static_cast<int>(style));
    }
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
    if (m_toolOptionsViewModel) {
        m_toolOptionsViewModel->setFontSize(size);
    }
    if (m_annotationContext) {
        m_annotationContext->applyTextFontSize(size);
    }
}

void RegionSelector::onFontFamilySelected(const QString& family)
{
    if (m_toolOptionsViewModel) {
        m_toolOptionsViewModel->setFontFamily(family);
    }
    if (m_annotationContext) {
        m_annotationContext->applyTextFontFamily(family);
    }
}
