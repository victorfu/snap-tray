#include "ScreenCanvasSession.h"

#include "InlineTextEditor.h"
#include "LaserPointerRenderer.h"
#include "PlatformFeatures.h"
#include "ScreenCanvas.h"
#include "annotation/AnnotationContext.h"
#include "annotation/AnnotationRenderHelper.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/EmojiStickerAnnotation.h"
#include "capture/ScreenSnapshot.h"
#include "annotations/PolylineAnnotation.h"
#include "annotations/TextBoxAnnotation.h"
#include "colorwidgets/ColorPickerDialogCompat.h"
#include "cursor/CursorAuthority.h"
#include "cursor/CursorManager.h"
#include "ImageColorSpaceHelper.h"
#include "platform/WindowLevel.h"
#include "qml/CanvasToolbarViewModel.h"
#include "qml/PinToolOptionsViewModel.h"
#include "qml/QmlEmojiPickerPopup.h"
#include "qml/QmlFloatingSubToolbar.h"
#include "qml/QmlFloatingToolbar.h"
#include "qml/QmlToast.h"
#include "region/RegionSettingsHelper.h"
#include "region/ShapeAnnotationEditor.h"
#include "region/TextAnnotationEditor.h"
#include "settings/AnnotationSettingsManager.h"
#include "tools/ToolManager.h"
#include "tools/ToolRepaintHelper.h"
#include "tools/ToolSectionConfig.h"
#include "tools/ToolTraits.h"
#include "tools/handlers/EmojiStickerToolHandler.h"
#include "utils/CoordinateHelper.h"

#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QDebug>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QTextEdit>
#include <QTimer>
#include <QWheelEvent>
#include <QtMath>

using snaptray::colorwidgets::ColorPickerDialogCompat;

namespace {
constexpr int kLaserPointerButtonId = 10001;
constexpr int kToolbarBottomMargin = 30;
constexpr int kToolPreviewRepaintMargin = 30;
constexpr int kAnnotationInteractionVisualMargin = 52;

bool isManagedToolButtonId(int buttonId)
{
    return buttonId >= 0 && buttonId < static_cast<int>(ToolId::Count);
}

bool isLaserPointerButtonId(int buttonId)
{
    return buttonId == kLaserPointerButtonId;
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

QRect clampBoundsForScreen(QScreen* screen)
{
    if (!screen) {
        return {};
    }

    return screen->availableGeometry().isValid()
        ? screen->availableGeometry()
        : screen->geometry();
}

QString screenIdentityPrefix(const QString& screenId)
{
    const QString trimmed = screenId.trimmed();
    const qsizetype geometryMarker = trimmed.indexOf(QStringLiteral("|geom:"));
    return geometryMarker >= 0 ? trimmed.left(geometryMarker) : trimmed;
}

QPixmap createSolidCanvasPixmap(const QSize& logicalSize, qreal devicePixelRatio, const QColor& color)
{
    const qreal normalizedDpr = devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;
    const QSize physicalSize = CoordinateHelper::toPhysical(logicalSize, normalizedDpr);
    if (logicalSize.isEmpty() || physicalSize.isEmpty()) {
        return {};
    }

    QPixmap result(physicalSize);
    if (result.isNull()) {
        return {};
    }

    result.setDevicePixelRatio(normalizedDpr);
    result.fill(color);
    return result;
}
} // namespace

const std::map<ToolId, ScreenCanvasSession::ToolbarClickHandler>&
ScreenCanvasSession::toolbarDispatchTable()
{
    static const std::map<ToolId, ToolbarClickHandler> kDispatch = {
        {ToolId::Pencil, &ScreenCanvasSession::handlePersistentToolClick},
        {ToolId::Marker, &ScreenCanvasSession::handlePersistentToolClick},
        {ToolId::Arrow, &ScreenCanvasSession::handlePersistentToolClick},
        {ToolId::Shape, &ScreenCanvasSession::handlePersistentToolClick},
        {ToolId::Text, &ScreenCanvasSession::handlePersistentToolClick},
        {ToolId::Eraser, &ScreenCanvasSession::handlePersistentToolClick},
        {ToolId::StepBadge, &ScreenCanvasSession::handlePersistentToolClick},
        {ToolId::EmojiSticker, &ScreenCanvasSession::handlePersistentToolClick},
        {ToolId::CanvasWhiteboard, &ScreenCanvasSession::handleCanvasModeToggle},
        {ToolId::CanvasBlackboard, &ScreenCanvasSession::handleCanvasModeToggle},
        {ToolId::Undo, &ScreenCanvasSession::handleActionToolClick},
        {ToolId::Redo, &ScreenCanvasSession::handleActionToolClick},
        {ToolId::Clear, &ScreenCanvasSession::handleActionToolClick},
        {ToolId::Copy, &ScreenCanvasSession::handleActionToolClick},
        {ToolId::Exit, &ScreenCanvasSession::handleActionToolClick},
    };
    return kDispatch;
}

const std::map<ToolId, ScreenCanvasSession::ToolbarClickHandler>&
ScreenCanvasSession::actionDispatchTable()
{
    static const std::map<ToolId, ToolbarClickHandler> kDispatch = {
        {ToolId::Undo, &ScreenCanvasSession::handleUndoAction},
        {ToolId::Redo, &ScreenCanvasSession::handleRedoAction},
        {ToolId::Clear, &ScreenCanvasSession::handleClearAction},
        {ToolId::Copy, &ScreenCanvasSession::handleCopyAction},
        {ToolId::Exit, &ScreenCanvasSession::handleExitAction},
    };
    return kDispatch;
}

ScreenCanvasSession::ScreenCanvasSession(QObject* parent)
    : QObject(parent)
{
    initializeSharedState();
}

ScreenCanvasSession::~ScreenCanvasSession()
{
    teardown();
    if (m_emojiPickerPopup) {
        m_emojiPickerPopup->close();
        delete m_emojiPickerPopup;
        m_emojiPickerPopup = nullptr;
    }
    if (m_colorPickerDialog) {
        m_colorPickerDialog->close();
    }
}

void ScreenCanvasSession::initializeSharedState()
{
    if (m_annotationLayer) {
        return;
    }

    m_annotationLayer = new AnnotationLayer(this);
    m_toolManager = new ToolManager(this);
    m_toolManager->registerDefaultHandlers();
    m_toolManager->setAnnotationLayer(m_annotationLayer);
    m_toolManager->setCurrentTool(m_currentToolId);

    auto& annotationSettings = AnnotationSettingsManager::instance();
    const QColor savedColor = annotationSettings.loadColor();
    const int savedWidth = annotationSettings.loadWidth();
    const LineEndStyle savedArrowStyle = annotationSettings.loadArrowStyle();
    const LineStyle savedLineStyle = annotationSettings.loadLineStyle();
    m_stepBadgeSize = annotationSettings.loadStepBadgeSize();

    m_toolManager->setColor(savedColor);
    m_toolManager->setWidth(savedWidth);
    m_toolManager->setArrowStyle(savedArrowStyle);
    m_toolManager->setLineStyle(savedLineStyle);

    connect(m_toolManager, &ToolManager::needsRepaint, this, [this]() {
        requestLocalizedToolRepaint();
    });
    m_toolManager->setTextColorSyncCallback([this](const QColor& color) {
        syncColorToAllWidgets(color);
    });
    m_toolManager->setHostFocusCallback([this]() {
        if (m_activeSurface) {
            m_activeSurface->setFocus(Qt::OtherFocusReason);
        }
    });
    m_toolManager->setTextReEditStartedCallback([this]() {
        m_consumeNextToolRelease = true;
    });

    m_toolbarViewModel = new CanvasToolbarViewModel(this);
    m_toolOptionsViewModel = new PinToolOptionsViewModel(this);

    m_qmlToolbar = std::make_unique<SnapTray::QmlFloatingToolbar>(m_toolbarViewModel, this);
    m_qmlSubToolbar = std::make_unique<SnapTray::QmlFloatingSubToolbar>(m_toolOptionsViewModel, this);

    connect(m_toolbarViewModel, &CanvasToolbarViewModel::toolSelected,
            this, [this](int toolId) { handleToolbarClick(toolId); });
    connect(m_toolbarViewModel, &CanvasToolbarViewModel::undoClicked,
            this, [this]() { handleToolbarClick(static_cast<int>(ToolId::Undo)); });
    connect(m_toolbarViewModel, &CanvasToolbarViewModel::redoClicked,
            this, [this]() { handleToolbarClick(static_cast<int>(ToolId::Redo)); });
    connect(m_toolbarViewModel, &CanvasToolbarViewModel::clearClicked,
            this, [this]() { handleToolbarClick(static_cast<int>(ToolId::Clear)); });
    connect(m_toolbarViewModel, &CanvasToolbarViewModel::copyClicked,
            this, [this]() { handleToolbarClick(static_cast<int>(ToolId::Copy)); });
    connect(m_toolbarViewModel, &CanvasToolbarViewModel::exitClicked,
            this, [this]() { handleToolbarClick(static_cast<int>(ToolId::Exit)); });
    connect(m_toolbarViewModel, &CanvasToolbarViewModel::canvasWhiteboardClicked,
            this, [this]() { handleToolbarClick(static_cast<int>(ToolId::CanvasWhiteboard)); });
    connect(m_toolbarViewModel, &CanvasToolbarViewModel::canvasBlackboardClicked,
            this, [this]() { handleToolbarClick(static_cast<int>(ToolId::CanvasBlackboard)); });

    connect(m_annotationLayer, &AnnotationLayer::changed, this, [this]() {
        m_toolbarViewModel->setCanUndo(m_annotationLayer->canUndo());
        m_toolbarViewModel->setCanRedo(m_annotationLayer->canRedo());
        resetAnnotationInteractionTracking();
        updateAllSurfaces();
    });

    m_toolOptionsViewModel->setCurrentColor(savedColor);
    m_toolOptionsViewModel->setCurrentWidth(savedWidth);
    m_toolOptionsViewModel->setArrowStyle(static_cast<int>(savedArrowStyle));
    m_toolOptionsViewModel->setLineStyle(static_cast<int>(savedLineStyle));
    m_toolOptionsViewModel->setStepBadgeSize(static_cast<int>(m_stepBadgeSize));

    connect(m_toolOptionsViewModel, &PinToolOptionsViewModel::colorSelected,
            this, &ScreenCanvasSession::onColorSelected);
    connect(m_toolOptionsViewModel, &PinToolOptionsViewModel::customColorPickerRequested,
            this, &ScreenCanvasSession::onMoreColorsRequested);
    connect(m_toolOptionsViewModel, &PinToolOptionsViewModel::widthValueChanged,
            this, &ScreenCanvasSession::onLineWidthChanged);
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
                if (RegionSettingsHelper* helper = activeSettingsHelper()) {
                    helper->showArrowStyleDropdown(
                        QPoint(qRound(globalX), qRound(globalY)),
                        static_cast<LineEndStyle>(m_toolOptionsViewModel->arrowStyle()));
                }
            });
    connect(m_toolOptionsViewModel, &PinToolOptionsViewModel::lineStyleDropdownRequested,
            this, [this](double globalX, double globalY) {
                if (RegionSettingsHelper* helper = activeSettingsHelper()) {
                    helper->showLineStyleDropdown(
                        QPoint(qRound(globalX), qRound(globalY)),
                        static_cast<LineStyle>(m_toolOptionsViewModel->lineStyle()));
                }
            });
    connect(m_toolOptionsViewModel, &PinToolOptionsViewModel::shapeTypeSelected,
            this, [this](int type) {
                m_shapeType = static_cast<ShapeType>(type);
                m_toolManager->setShapeType(type);
            });
    connect(m_toolOptionsViewModel, &PinToolOptionsViewModel::shapeFillModeSelected,
            this, [this](int mode) {
                m_shapeFillMode = static_cast<ShapeFillMode>(mode);
                m_toolManager->setShapeFillMode(mode);
            });
    connect(m_toolOptionsViewModel, &PinToolOptionsViewModel::stepBadgeSizeSelected,
            this, [this](int size) {
                m_stepBadgeSize = static_cast<StepBadgeSize>(size);
                AnnotationSettingsManager::instance().saveStepBadgeSize(m_stepBadgeSize);
            });
    connect(m_toolOptionsViewModel, &PinToolOptionsViewModel::boldToggled,
            this, [this](bool enabled) {
                for (const QPointer<ScreenCanvas>& surface : m_surfaces) {
                    if (surface && surface->textAnnotationEditor()) {
                        surface->textAnnotationEditor()->setBold(enabled);
                    }
                }
                syncTextFormattingToSubToolbar();
            });
    connect(m_toolOptionsViewModel, &PinToolOptionsViewModel::italicToggled,
            this, [this](bool enabled) {
                for (const QPointer<ScreenCanvas>& surface : m_surfaces) {
                    if (surface && surface->textAnnotationEditor()) {
                        surface->textAnnotationEditor()->setItalic(enabled);
                    }
                }
                syncTextFormattingToSubToolbar();
            });
    connect(m_toolOptionsViewModel, &PinToolOptionsViewModel::underlineToggled,
            this, [this](bool enabled) {
                for (const QPointer<ScreenCanvas>& surface : m_surfaces) {
                    if (surface && surface->textAnnotationEditor()) {
                        surface->textAnnotationEditor()->setUnderline(enabled);
                    }
                }
                syncTextFormattingToSubToolbar();
            });
    connect(m_qmlSubToolbar.get(), &SnapTray::QmlFloatingSubToolbar::emojiPickerRequested,
            this, &ScreenCanvasSession::showEmojiPickerPopup);

    connect(m_qmlToolbar.get(), &SnapTray::QmlFloatingToolbar::dragStarted,
            this, [this]() {
                m_toolbarDragging = true;
            });
    connect(m_qmlToolbar.get(), &SnapTray::QmlFloatingToolbar::dragMoved,
            this, [this](double, double) {
                if (m_qmlSubToolbar && m_qmlSubToolbar->isVisible()) {
                    m_qmlSubToolbar->positionBelow(m_qmlToolbar->geometry());
                }
                if (m_emojiPickerPopup && m_emojiPickerPopup->isVisible()) {
                    m_emojiPickerPopup->positionAt(m_qmlToolbar->geometry());
                }
            });
    connect(m_qmlToolbar.get(), &SnapTray::QmlFloatingToolbar::dragFinished,
            this, [this]() {
                m_toolbarDragging = false;
                refreshFloatingUiParentWidget();
                saveToolbarPlacement();
                relayoutFloatingUi(false);
            });

    m_laserRenderer = new LaserPointerRenderer(this);
    m_laserRenderer->setColor(savedColor);
    m_laserRenderer->setWidth(savedWidth);
    connect(m_laserRenderer, &LaserPointerRenderer::needsRepaint, this, [this]() {
        updateAllSurfaces();
    });
}

void ScreenCanvasSession::open(QScreen* activationScreen)
{
    if (m_isOpen || m_isClosing) {
        return;
    }

    const QList<QScreen*> screens = QGuiApplication::screens();
    m_activationScreen = activationScreen;
    if (!m_activationScreen || !screens.contains(m_activationScreen.data())) {
        m_activationScreen = QGuiApplication::primaryScreen();
    }
    if (!m_activationScreen && !screens.isEmpty()) {
        m_activationScreen = screens.first();
    }
    if (!m_activationScreen) {
        qWarning() << "ScreenCanvasSession: No valid screen available";
        return;
    }

    m_desktopGeometry = virtualDesktopGeometry(screens);
    if (!m_desktopGeometry.isValid()) {
        qWarning() << "ScreenCanvasSession: No valid desktop geometry";
        return;
    }

    m_bgMode = CanvasBackgroundMode::Screen;
    m_toolbarPlacementInitialized = false;
    m_toolbarDragging = false;
    m_skipToolbarPlacementSaveOnClose = false;
    m_showSubToolbar = true;
    m_isOpen = true;

    createSurfaces(screens);
    if (m_surfaces.isEmpty()) {
        qWarning() << "ScreenCanvasSession: Failed to create surfaces";
        m_isOpen = false;
        return;
    }

    QList<QRect> toolbarDragBounds;
    for (QScreen* screen : screens) {
        const QRect bounds = clampBoundsForScreen(screen);
        if (bounds.isValid()) {
            toolbarDragBounds.append(bounds);
        }
    }
    if (!toolbarDragBounds.isEmpty()) {
        m_qmlToolbar->setDragBounds(toolbarDragBounds);
    } else {
        m_qmlToolbar->clearDragBounds();
    }
    connectScreenTopologySignals();
    connectApplicationStateSignal();
    setToolCursor();
    const bool toolbarPlacementReady = initializeToolbarPlacement();
    updateQmlToolbarState();
    if (!toolbarPlacementReady) {
        relayoutFloatingUi(true);
    }

    if (m_activeSurface) {
        m_activeSurface->activateWindow();
        m_activeSurface->setFocus(Qt::OtherFocusReason);
        m_activeSurface->raise();
        raiseFloatingUiWindows();
    }
}

void ScreenCanvasSession::close()
{
    if (m_isClosing || !m_isOpen) {
        return;
    }

    m_isClosing = true;
    if (!m_skipToolbarPlacementSaveOnClose) {
        saveToolbarPlacement();
    }

    teardown();
    emit closed();
    deleteLater();
}

void ScreenCanvasSession::teardown()
{
    endMouseGrab();

    if (m_applicationStateChangedConnection) {
        disconnect(m_applicationStateChangedConnection);
        m_applicationStateChangedConnection = {};
    }

    if (m_qmlToolbar) {
        m_qmlToolbar->hide();
    }
    if (m_qmlSubToolbar) {
        m_qmlSubToolbar->hide();
    }
    if (m_emojiPickerPopup) {
        m_emojiPickerPopup->close();
    }

    destroySurfaces();

    m_activeSurface = nullptr;
    m_grabbedSurface = nullptr;
    m_isOpen = false;
}

void ScreenCanvasSession::createSurfaces(const QList<QScreen*>& screens)
{
    for (QScreen* screen : screens) {
        if (!screen) {
            continue;
        }

        auto* surface = new ScreenCanvas(this);
        surface->setSession(this);
        surface->initializeForScreenSurface(screen, m_desktopGeometry);
        surface->setSharedToolManager(m_toolManager);
        configureSurface(surface);

        connect(surface, &QObject::destroyed, this, [this, surface]() {
            m_surfaces.removeAll(surface);
            if (m_activeSurface == surface) {
                m_activeSurface = nullptr;
            }
            if (m_grabbedSurface == surface) {
                m_grabbedSurface = nullptr;
            }
        });

        m_surfaces.append(surface);
        surface->show();
        preventWindowHideOnDeactivate(surface);
        raiseWindowAboveMenuBar(surface);
        setWindowClickThrough(surface, false);
        surface->raise();

        if (screen == m_activationScreen) {
            m_activeSurface = surface;
        }
    }

    if (!m_activeSurface && !m_surfaces.isEmpty()) {
        m_activeSurface = m_surfaces.first();
    }
    activateSurface(m_activeSurface);
}

void ScreenCanvasSession::configureSurface(ScreenCanvas* surface)
{
    if (!surface) {
        return;
    }

    if (auto* shapeEditor = surface->shapeAnnotationEditor()) {
        shapeEditor->setAnnotationLayer(m_annotationLayer);
    }

    if (auto* textEditor = surface->textAnnotationEditor()) {
        textEditor->setAnnotationLayer(m_annotationLayer);
        textEditor->setTextEditor(surface->inlineTextEditor());
        textEditor->setParentWidget(surface);
        textEditor->setCoordinateMappers(
            [surface](const QPointF& point) { return surface->toAnnotationPointF(point); },
            [surface](const QPointF& point) { return surface->toLocalPointF(point); });

        connect(textEditor, &TextAnnotationEditor::updateRequested, this, [this]() {
            updateAllSurfaces();
        });
        connect(textEditor, &TextAnnotationEditor::formattingChanged, this, [this, surface]() {
            if (!m_activeSurface || surface == m_activeSurface) {
                syncTextFormattingToSubToolbar();
            }
        });
    }

    if (surface->settingsHelper()) {
        surface->settingsHelper()->setParentWidget(surface);
        connect(surface->settingsHelper(), &RegionSettingsHelper::dropdownShown,
                this, [this, surface]() {
                    auto& authority = CursorAuthority::instance();
                    if (QWidget* popup = QApplication::activePopupWidget()) {
                        authority.submitWidgetRequest(
                            surface,
                            QStringLiteral("floating.popup"),
                            CursorRequestSource::Popup,
                            cursorSpecForWidget(popup));
                    }
                    auto& cursorManager = CursorManager::instance();
                    if (authority.modeForWidget(surface) == CursorSurfaceMode::Authority) {
                        cursorManager.reapplyCursorForWidget(surface);
                    } else {
                        cursorManager.pushCursorForWidget(surface, CursorContext::Override, Qt::ArrowCursor);
                        cursorManager.reapplyCursorForWidget(surface);
                    }
                });
        connect(surface->settingsHelper(), &RegionSettingsHelper::dropdownHidden,
                this, [this, surface]() {
                    QTimer::singleShot(0, this, [this, surface]() {
                        syncFloatingUiCursor(surface);
                    });
                });
        connect(surface->settingsHelper(), &RegionSettingsHelper::fontSizeSelected,
                this, &ScreenCanvasSession::onFontSizeSelected);
        connect(surface->settingsHelper(), &RegionSettingsHelper::fontFamilySelected,
                this, &ScreenCanvasSession::onFontFamilySelected);
        connect(surface->settingsHelper(), &RegionSettingsHelper::arrowStyleSelected,
                this, &ScreenCanvasSession::onArrowStyleChanged);
        connect(surface->settingsHelper(), &RegionSettingsHelper::lineStyleSelected,
                this, &ScreenCanvasSession::onLineStyleChanged);
    }

    if (surface->inlineTextEditor()) {
        connect(surface->inlineTextEditor(), &InlineTextEditor::editingFinished,
                this, [this, surface](const QString& text, const QPoint& position) {
                    onTextEditingFinished(surface, text, position);
                });
        connect(surface->inlineTextEditor(), &InlineTextEditor::editingCancelled,
                this, [this, surface]() {
                    if (surface && surface->textAnnotationEditor()) {
                        surface->textAnnotationEditor()->cancelEditing();
                    }
                });
    }
}

void ScreenCanvasSession::destroySurfaces()
{
    const QList<QPointer<ScreenCanvas>> surfaces = m_surfaces;
    m_surfaces.clear();

    for (const QPointer<ScreenCanvas>& surface : surfaces) {
        if (!surface) {
            continue;
        }
        surface->closeFromSession();
        surface->deleteLater();
    }
}

int ScreenCanvasSession::surfaceCount() const
{
    int count = 0;
    for (const QPointer<ScreenCanvas>& surface : m_surfaces) {
        if (surface) {
            ++count;
        }
    }
    return count;
}

QRect ScreenCanvasSession::virtualDesktopGeometry(const QList<QScreen*>& screens)
{
    QRect result;
    for (QScreen* screen : screens) {
        if (!screen) {
            continue;
        }
        result = result.isValid() ? result.united(screen->geometry()) : screen->geometry();
    }
    return result;
}

QPoint ScreenCanvasSession::globalToDesktop(const QPoint& globalPos, const QRect& desktopGeometry)
{
    return desktopGeometry.isValid() ? globalPos - desktopGeometry.topLeft() : globalPos;
}

QPoint ScreenCanvasSession::desktopToLocal(const QPoint& desktopPos,
                                           const QRect& screenGeometry,
                                           const QRect& desktopGeometry)
{
    if (!screenGeometry.isValid() || !desktopGeometry.isValid()) {
        return desktopPos;
    }
    return desktopPos + desktopGeometry.topLeft() - screenGeometry.topLeft();
}

QPoint ScreenCanvasSession::clampToolbarTopLeftToBounds(const QPoint& desiredTopLeft,
                                                        const QRect& bounds,
                                                        const QSize& toolbarSize)
{
    if (!bounds.isValid()) {
        return desiredTopLeft;
    }

    const int width = qMax(1, toolbarSize.width());
    const int height = qMax(1, toolbarSize.height());
    const int maxX = bounds.right() - width + 1;
    const int maxY = bounds.bottom() - height + 1;

    const int clampedX = (width >= bounds.width())
        ? bounds.left()
        : qBound(bounds.left(), desiredTopLeft.x(), maxX);
    const int clampedY = (height >= bounds.height())
        ? bounds.top()
        : qBound(bounds.top(), desiredTopLeft.y(), maxY);
    return QPoint(clampedX, clampedY);
}

QPoint ScreenCanvasSession::defaultToolbarTopLeftForScreen(const QRect& screenBounds,
                                                           const QSize& toolbarSize)
{
    if (!screenBounds.isValid()) {
        return {};
    }

    const QPoint desiredTopLeft(
        screenBounds.center().x() - toolbarSize.width() / 2,
        screenBounds.bottom() - kToolbarBottomMargin - toolbarSize.height() + 1);
    return clampToolbarTopLeftToBounds(desiredTopLeft, screenBounds, toolbarSize);
}

ScreenCanvasSession::ToolbarPlacementResolution
ScreenCanvasSession::resolveToolbarPlacement(const ScreenCanvasToolbarPlacement& savedPlacement,
                                             const QList<QScreen*>& screens,
                                             QScreen* activationScreen,
                                             const QSize& toolbarSize)
{
    ToolbarPlacementResolution result;
    if (screens.isEmpty()) {
        return result;
    }

    auto resolveActivationScreen = [&screens, activationScreen]() -> QScreen* {
        if (activationScreen && screens.contains(activationScreen)) {
            return activationScreen;
        }
        if (QScreen* primary = QGuiApplication::primaryScreen();
            primary && screens.contains(primary)) {
            return primary;
        }
        return screens.first();
    };

    QScreen* targetScreen = nullptr;
    if (savedPlacement.isValid()) {
        for (QScreen* screen : screens) {
            if (ScreenCanvasSettingsManager::screenIdentifier(screen) == savedPlacement.screenId) {
                targetScreen = screen;
                break;
            }
        }

        if (!targetScreen) {
            const QString savedPrefix = screenIdentityPrefix(savedPlacement.screenId);
            QList<QScreen*> prefixMatches;
            for (QScreen* screen : screens) {
                if (screenIdentityPrefix(ScreenCanvasSettingsManager::screenIdentifier(screen)) ==
                    savedPrefix) {
                    prefixMatches.append(screen);
                }
            }

            if (prefixMatches.size() == 1) {
                targetScreen = prefixMatches.first();
            }
        }
    }

    if (!targetScreen) {
        targetScreen = resolveActivationScreen();
    }
    if (!targetScreen) {
        return result;
    }

    const QRect clampBounds = clampBoundsForScreen(targetScreen);

    QPoint desiredTopLeft;
    if (savedPlacement.isValid()) {
        desiredTopLeft = targetScreen->geometry().topLeft() + savedPlacement.topLeftInScreen;
        result.usedStoredPlacement = true;
    } else {
        desiredTopLeft = defaultToolbarTopLeftForScreen(clampBounds, toolbarSize);
    }

    result.globalTopLeft = clampToolbarTopLeftToBounds(desiredTopLeft, clampBounds, toolbarSize);
    result.topLeftInScreen = result.globalTopLeft - targetScreen->geometry().topLeft();
    result.screenId = ScreenCanvasSettingsManager::screenIdentifier(targetScreen);
    return result;
}

void ScreenCanvasSession::updateAllSurfaces()
{
    for (const QPointer<ScreenCanvas>& surface : m_surfaces) {
        if (surface) {
            surface->update();
        }
    }
}

void ScreenCanvasSession::updateSurfacesForAnnotationRect(const QRect& annotationRect)
{
    if (!annotationRect.isValid() || annotationRect.isEmpty()) {
        return;
    }

    for (const QPointer<ScreenCanvas>& surface : m_surfaces) {
        if (!surface) {
            continue;
        }

        const QRect localRect(
            surface->toLocalPoint(annotationRect.topLeft()),
            annotationRect.size());
        const QRect clippedRect = localRect.intersected(surface->rect());
        if (clippedRect.isValid() && !clippedRect.isEmpty()) {
            surface->update(clippedRect);
        }
    }
}

bool ScreenCanvasSession::hasActiveAnnotationInteraction() const
{
    const ScreenCanvas* interactionSurface =
        m_grabbedSurface ? m_grabbedSurface.data() : m_activeSurface.data();
    const bool textInteraction =
        interactionSurface &&
        interactionSurface->textAnnotationEditor() &&
        (interactionSurface->textAnnotationEditor()->isDragging() ||
         interactionSurface->textAnnotationEditor()->isTransforming());
    const bool shapeInteraction =
        interactionSurface &&
        interactionSurface->shapeAnnotationEditor() &&
        (interactionSurface->shapeAnnotationEditor()->isDragging() ||
         interactionSurface->shapeAnnotationEditor()->isTransforming());

    return textInteraction || shapeInteraction ||
           m_isEmojiDragging || m_isEmojiScaling || m_isEmojiRotating ||
           m_isArrowDragging || m_isPolylineDragging;
}

QRect ScreenCanvasSession::selectedAnnotationInteractionRect() const
{
    return snaptray::tools::selectedAnnotationRepaintRect(
        m_annotationLayer,
        kAnnotationInteractionVisualMargin);
}

void ScreenCanvasSession::requestLocalizedToolRepaint()
{
    const QRect previewRect = snaptray::tools::previewRepaintRect(
        m_toolManager,
        kToolPreviewRepaintMargin);
    if (previewRect.isValid() && !previewRect.isEmpty()) {
        updateSurfacesForAnnotationRect(previewRect);
        return;
    }

    if (!hasActiveAnnotationInteraction()) {
        resetAnnotationInteractionTracking();
        updateAllSurfaces();
        return;
    }

    const QRect currentRect = selectedAnnotationInteractionRect();
    QRect dirtyRect = currentRect;
    if (m_lastAnnotationInteractionRect.isValid()) {
        dirtyRect = dirtyRect.united(m_lastAnnotationInteractionRect);
    }

    m_lastAnnotationInteractionRect = currentRect;
    if (dirtyRect.isValid() && !dirtyRect.isEmpty()) {
        updateSurfacesForAnnotationRect(dirtyRect);
    }
}

void ScreenCanvasSession::resetAnnotationInteractionTracking()
{
    m_lastAnnotationInteractionRect = QRect();
}

ScreenCanvasSession::FloatingUiVisibilityState ScreenCanvasSession::hideFloatingUiForCapture()
{
    FloatingUiVisibilityState state;
    if (m_qmlToolbar) {
        state.toolbarVisible = m_qmlToolbar->isVisible();
        if (state.toolbarVisible) {
            m_qmlToolbar->hide();
        }
    }

    if (m_qmlSubToolbar && m_qmlSubToolbar->isVisible()) {
        m_qmlSubToolbar->hide();
    }

    if (m_emojiPickerPopup) {
        state.emojiPickerVisible = m_emojiPickerPopup->isVisible();
        if (state.emojiPickerVisible) {
            m_emojiPickerPopup->hide();
        }
    }

    qApp->processEvents();
    return state;
}

void ScreenCanvasSession::restoreFloatingUiAfterCapture(const FloatingUiVisibilityState& state)
{
    if (!m_isOpen) {
        return;
    }

    if (state.toolbarVisible && m_qmlToolbar) {
        m_qmlToolbar->show();
    }

    updateQmlToolbarState();
    if (state.emojiPickerVisible) {
        showEmojiPickerPopup();
    }
    raiseFloatingUiWindows();
}

ScreenCanvasSession::ScreenSnapshotVisibilityState ScreenCanvasSession::hideUiForScreenSnapshot()
{
    ScreenSnapshotVisibilityState state;
    state.floatingUi = hideFloatingUiForCapture();

    for (const QPointer<ScreenCanvas>& surface : m_surfaces) {
        if (!surface || !surface->isVisible()) {
            continue;
        }

        state.visibleSurfaces.append(surface);
        surface->hide();
    }

    if (!state.visibleSurfaces.isEmpty()) {
        qApp->processEvents();
    }

    return state;
}

void ScreenCanvasSession::restoreUiAfterScreenSnapshot(
    const ScreenSnapshotVisibilityState& state)
{
    if (!m_isOpen) {
        return;
    }

    for (const QPointer<ScreenCanvas>& surface : state.visibleSurfaces) {
        if (!surface) {
            continue;
        }

        if (!surface->isVisible()) {
            surface->show();
        }
        preventWindowHideOnDeactivate(surface);
        raiseWindowAboveMenuBar(surface);
        setWindowClickThrough(surface, false);
        surface->raise();
        surface->update();
    }

    if (!m_activeSurface && !state.visibleSurfaces.isEmpty()) {
        m_activeSurface = state.visibleSurfaces.first();
    }

    refreshFloatingUiParentWidget();
    setToolCursor();
    restoreFloatingUiAfterCapture(state.floatingUi);
}

void ScreenCanvasSession::connectApplicationStateSignal()
{
    if (m_applicationStateChangedConnection) {
        return;
    }

    m_applicationStateChangedConnection = connect(
        qApp,
        &QGuiApplication::applicationStateChanged,
        this,
        &ScreenCanvasSession::handleApplicationStateChanged);
}

void ScreenCanvasSession::activateSurface(ScreenCanvas* surface)
{
    if (!surface) {
        return;
    }

    m_activeSurface = surface;
    if (m_toolManager) {
        m_toolManager->setInlineTextEditor(surface->inlineTextEditor());
        m_toolManager->setTextAnnotationEditor(surface->textAnnotationEditor());
        m_toolManager->setShapeAnnotationEditor(surface->shapeAnnotationEditor());
        m_toolManager->setTextEditingBounds(surface->rect());
        if (surface->canvasScreen()) {
            m_toolManager->setDevicePixelRatio(surface->canvasScreen()->devicePixelRatio());
        }
    }
    syncTextFormattingToSubToolbar();
    refreshFloatingUiParentWidget();
}

void ScreenCanvasSession::beginMouseGrab(ScreenCanvas* surface)
{
    if (!surface || m_grabbedSurface == surface) {
        return;
    }

    endMouseGrab();
    m_grabbedSurface = surface;
    surface->grabMouse();
}

void ScreenCanvasSession::endMouseGrab()
{
    if (!m_grabbedSurface) {
        return;
    }

    m_grabbedSurface->releaseMouse();
    CursorManager::instance().setInputStateForWidget(m_grabbedSurface, InputState::Idle);
    m_grabbedSurface = nullptr;
}

ScreenCanvas* ScreenCanvasSession::editingSurface() const
{
    for (const QPointer<ScreenCanvas>& surface : m_surfaces) {
        if (surface && surface->inlineTextEditor() && surface->inlineTextEditor()->isEditing()) {
            return surface;
        }
    }
    return nullptr;
}

ScreenCanvas* ScreenCanvasSession::toolbarHostSurface() const
{
    if (m_qmlToolbar) {
        const QRect toolbarGeometry = m_qmlToolbar->geometry();
        if (toolbarGeometry.isValid()) {
            if (ScreenCanvas* surface = surfaceForGlobalPoint(toolbarGeometry.center())) {
                return surface;
            }
        }
    }

    if (m_activeSurface) {
        return m_activeSurface;
    }
    if (ScreenCanvas* surface = surfaceForScreen(m_activationScreen.data())) {
        return surface;
    }
    return m_surfaces.isEmpty() ? nullptr : m_surfaces.first();
}

ScreenCanvas* ScreenCanvasSession::surfaceForGlobalPoint(const QPoint& globalPos) const
{
    for (const QPointer<ScreenCanvas>& surface : m_surfaces) {
        if (surface && surface->screenGeometry().contains(globalPos)) {
            return surface;
        }
    }
    return nullptr;
}

ScreenCanvas* ScreenCanvasSession::surfaceForScreen(QScreen* screen) const
{
    for (const QPointer<ScreenCanvas>& surface : m_surfaces) {
        if (surface && surface->canvasScreen() == screen) {
            return surface;
        }
    }
    return nullptr;
}

RegionSettingsHelper* ScreenCanvasSession::activeSettingsHelper() const
{
    if (m_activeSurface && m_activeSurface->settingsHelper()) {
        return m_activeSurface->settingsHelper();
    }
    if (ScreenCanvas* host = toolbarHostSurface()) {
        return host->settingsHelper();
    }
    return nullptr;
}

void ScreenCanvasSession::refreshFloatingUiParentWidget()
{
    ScreenCanvas* hostSurface = toolbarHostSurface();
    if (!hostSurface) {
        return;
    }

    if (m_qmlToolbar) {
        m_qmlToolbar->setParentWidget(hostSurface);
    }
    if (m_qmlSubToolbar) {
        m_qmlSubToolbar->setParentWidget(hostSurface);
    }
    if (m_emojiPickerPopup) {
        m_emojiPickerPopup->setParentWidget(hostSurface);
    }
}

void ScreenCanvasSession::restoreSurfaceVisibilityAndStacking(bool refocusActiveSurface)
{
    ScreenCanvas* fallbackSurface = nullptr;
    for (const QPointer<ScreenCanvas>& surface : m_surfaces) {
        if (!surface) {
            continue;
        }

        if (!fallbackSurface) {
            fallbackSurface = surface;
        }

        if (!surface->isVisible()) {
            surface->show();
        }
        preventWindowHideOnDeactivate(surface);
        raiseWindowAboveMenuBar(surface);
        setWindowClickThrough(surface, false);
        surface->raise();
        surface->update();
    }

    if (!m_activeSurface) {
        m_activeSurface = fallbackSurface;
    }

    refreshFloatingUiParentWidget();
    relayoutFloatingUi(false);
    setToolCursor();
    updateAllSurfaces();

    if (refocusActiveSurface && m_activeSurface) {
        m_activeSurface->activateWindow();
        m_activeSurface->setFocus(Qt::OtherFocusReason);
        m_activeSurface->raise();
    }
    raiseFloatingUiWindows();
}

void ScreenCanvasSession::raiseFloatingUiWindows()
{
    if (m_qmlToolbar && m_qmlToolbar->isVisible()) {
        if (QWindow* toolbarWindow = m_qmlToolbar->window()) {
            toolbarWindow->raise();
        }
    }
    if (m_qmlSubToolbar && m_qmlSubToolbar->isVisible()) {
        if (QWindow* subToolbarWindow = m_qmlSubToolbar->window()) {
            subToolbarWindow->raise();
        }
    }
    if (m_emojiPickerPopup && m_emojiPickerPopup->isVisible()) {
        if (QWindow* emojiWindow = m_emojiPickerPopup->window()) {
            emojiWindow->raise();
        }
    }
}

void ScreenCanvasSession::drawAnnotations(ScreenCanvas* surface, QPainter& painter)
{
    if (!m_annotationLayer || !surface) {
        return;
    }

    const qreal devicePixelRatio = surface->canvasScreen()
        ? surface->canvasScreen()->devicePixelRatio()
        : surface->devicePixelRatioF();
    const QPoint origin = surface->annotationOffset();
    const ScreenCanvas* interactionSurface = m_grabbedSurface ? m_grabbedSurface.data() : m_activeSurface.data();
    const bool shapeInteractionActive =
        interactionSurface &&
        interactionSurface->shapeAnnotationEditor() &&
        (interactionSurface->shapeAnnotationEditor()->isDragging() ||
         interactionSurface->shapeAnnotationEditor()->isTransforming());

    snaptray::annotation::SelectedAnnotationItems selectedItems;
    selectedItems.text = getSelectedTextAnnotation();
    selectedItems.emoji = getSelectedEmojiStickerAnnotation();
    selectedItems.shape = getSelectedShapeAnnotation();
    selectedItems.arrow = getSelectedArrowAnnotation();
    selectedItems.polyline = getSelectedPolylineAnnotation();

    snaptray::annotation::drawAnnotationVisuals(
        painter,
        m_annotationLayer,
        surface->size(),
        devicePixelRatio,
        origin,
        m_isEmojiDragging || m_isEmojiScaling || m_isEmojiRotating ||
            m_isArrowDragging || m_isPolylineDragging || shapeInteractionActive,
        selectedItems);
}

void ScreenCanvasSession::drawCurrentAnnotation(QPainter& painter)
{
    if (m_toolManager) {
        m_toolManager->drawCurrentPreview(painter);
    }
}

void ScreenCanvasSession::handleSurfacePaint(ScreenCanvas* surface, QPainter& painter)
{
    if (!surface) {
        return;
    }

    if (m_bgMode == CanvasBackgroundMode::Whiteboard) {
        painter.fillRect(surface->rect(), Qt::white);
    } else if (m_bgMode == CanvasBackgroundMode::Blackboard) {
        painter.fillRect(surface->rect(), Qt::black);
    }
#ifdef Q_OS_WIN
    else {
        painter.fillRect(surface->rect(), QColor(255, 255, 255, 1));
    }
#endif

    drawAnnotations(surface, painter);

    painter.save();
    painter.translate(-QPointF(surface->annotationOffset()));
    drawCurrentAnnotation(painter);
    if (m_laserRenderer) {
        m_laserRenderer->draw(painter);
    }
    painter.restore();
}

void ScreenCanvasSession::handleToolbarClick(int buttonId)
{
    finalizePolylineForToolbarInteraction();

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

void ScreenCanvasSession::finalizePolylineForToolbarInteraction()
{
    if (!m_toolManager ||
        m_toolManager->currentTool() != ToolId::Arrow ||
        !m_toolManager->isDrawing()) {
        return;
    }

    m_toolManager->handleDoubleClick(annotationPointForCurrentCursor());
}

void ScreenCanvasSession::handlePersistentToolClick(ToolId toolId)
{
    if (m_laserPointerActive) {
        m_laserPointerActive = false;
        if (m_currentToolId != toolId) {
            m_currentToolId = toolId;
            m_toolManager->setCurrentTool(toolId);
        }
        m_showSubToolbar = true;
    } else if (m_currentToolId == toolId) {
        m_showSubToolbar = !m_showSubToolbar;
    } else {
        m_currentToolId = toolId;
        m_toolManager->setCurrentTool(toolId);
        m_showSubToolbar = true;
    }

    updateQmlToolbarState();
    setToolCursor();
    updateAllSurfaces();
}

void ScreenCanvasSession::handleLaserPointerClick()
{
    if (m_laserPointerActive) {
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

    updateQmlToolbarState();
    setToolCursor();
    updateAllSurfaces();
}

void ScreenCanvasSession::handleCanvasModeToggle(ToolId toolId)
{
    if (toolId == ToolId::CanvasWhiteboard) {
        setBackgroundMode(m_bgMode == CanvasBackgroundMode::Whiteboard
                              ? CanvasBackgroundMode::Screen
                              : CanvasBackgroundMode::Whiteboard);
        return;
    }

    if (toolId == ToolId::CanvasBlackboard) {
        setBackgroundMode(m_bgMode == CanvasBackgroundMode::Blackboard
                              ? CanvasBackgroundMode::Screen
                              : CanvasBackgroundMode::Blackboard);
    }
}

void ScreenCanvasSession::handleActionToolClick(ToolId toolId)
{
    const auto& dispatch = actionDispatchTable();
    auto it = dispatch.find(toolId);
    if (it == dispatch.end() || !it->second) {
        return;
    }
    (this->*(it->second))(toolId);
}

void ScreenCanvasSession::handleUndoAction(ToolId)
{
    if (m_annotationLayer->canUndo()) {
        m_annotationLayer->undo();
        updateAllSurfaces();
    }
}

void ScreenCanvasSession::handleRedoAction(ToolId)
{
    if (m_annotationLayer->canRedo()) {
        m_annotationLayer->redo();
        updateAllSurfaces();
    }
}

void ScreenCanvasSession::handleClearAction(ToolId)
{
    m_annotationLayer->clear();
    updateAllSurfaces();
}

void ScreenCanvasSession::handleCopyAction(ToolId)
{
    finalizePolylineForToolbarInteraction();

    QScreen* targetScreen = resolveCopyTargetScreen();
    ScreenSnapshotVisibilityState hiddenUiState;
    const bool screenSnapshotUiAlreadyHidden =
        targetScreen && m_bgMode == CanvasBackgroundMode::Screen;
    if (screenSnapshotUiAlreadyHidden) {
        hiddenUiState = hideUiForScreenSnapshot();
    }

    const QPixmap exportPixmap = exportCanvasPixmapForScreen(
        targetScreen,
        screenSnapshotUiAlreadyHidden);
    if (exportPixmap.isNull() || !targetScreen) {
        if (screenSnapshotUiAlreadyHidden) {
            restoreUiAfterScreenSnapshot(hiddenUiState);
        }
        SnapTray::QmlToast::screenToast().showToast(
            SnapTray::QmlToast::Level::Error,
            tr("Copy failed"));
        return;
    }

    const QImage clipboardImage = normalizeImageForExport(exportPixmap.toImage(), targetScreen);
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

void ScreenCanvasSession::handleExitAction(ToolId)
{
    close();
}

bool ScreenCanvasSession::isDrawingTool(ToolId toolId) const
{
    return ToolTraits::isDrawingTool(toolId);
}

QScreen* ScreenCanvasSession::resolveCopyTargetScreen() const
{
    if (QScreen* cursorScreen = QGuiApplication::screenAt(QCursor::pos())) {
        return cursorScreen;
    }

    if (m_activeSurface && m_activeSurface->canvasScreen()) {
        return m_activeSurface->canvasScreen();
    }

    if (m_activationScreen) {
        return m_activationScreen.data();
    }

    return QGuiApplication::primaryScreen();
}

QPixmap ScreenCanvasSession::buildCopyExportBasePixmap(QScreen* screen,
                                                       bool screenSnapshotUiAlreadyHidden) const
{
    if (!screen) {
        return {};
    }

    if (m_bgMode == CanvasBackgroundMode::Screen) {
        if (screenSnapshotUiAlreadyHidden) {
            if (m_screenSnapshotProvider) {
                return m_screenSnapshotProvider(screen);
            }
            return snaptray::capture::captureScreenSnapshot(screen);
        }

        auto* mutableThis = const_cast<ScreenCanvasSession*>(this);
        const ScreenSnapshotVisibilityState uiState = mutableThis->hideUiForScreenSnapshot();
        QPixmap snapshot;
        if (m_screenSnapshotProvider) {
            snapshot = m_screenSnapshotProvider(screen);
        } else {
            snapshot = snaptray::capture::captureScreenSnapshot(screen);
        }
        mutableThis->restoreUiAfterScreenSnapshot(uiState);
        return snapshot;
    }

    const QColor fillColor = m_bgMode == CanvasBackgroundMode::Whiteboard ? Qt::white : Qt::black;
    return createSolidCanvasPixmap(
        screen->geometry().size(),
        screen->devicePixelRatio(),
        fillColor);
}

QPixmap ScreenCanvasSession::exportCanvasPixmapForScreen(QScreen* screen,
                                                         bool screenSnapshotUiAlreadyHidden) const
{
    if (!screen) {
        return {};
    }

    QPixmap result = buildCopyExportBasePixmap(screen, screenSnapshotUiAlreadyHidden);
    if (result.isNull()) {
        return result;
    }

    if (!m_annotationLayer || m_annotationLayer->isEmpty()) {
        return result;
    }

    const QRect desktopGeometry = m_desktopGeometry.isValid() ? m_desktopGeometry : screen->geometry();
    const QPoint origin = screen->geometry().topLeft() - desktopGeometry.topLeft();
    const qreal devicePixelRatio =
        result.devicePixelRatio() > 0.0 ? result.devicePixelRatio() : screen->devicePixelRatio();

    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    m_annotationLayer->drawCached(
        painter,
        screen->geometry().size(),
        devicePixelRatio > 0.0 ? devicePixelRatio : 1.0,
        origin);
    painter.end();

    return result;
}

void ScreenCanvasSession::setToolCursor()
{
    auto& cursorManager = CursorManager::instance();
    for (const QPointer<ScreenCanvas>& surface : m_surfaces) {
        if (!surface) {
            continue;
        }

        if (m_laserPointerActive) {
            cursorManager.pushCursorForWidget(surface, CursorContext::Tool, Qt::CrossCursor);
        } else {
            cursorManager.updateToolCursorForWidget(surface);
        }
        cursorManager.reapplyCursorForWidget(surface);
    }
}

bool ScreenCanvasSession::isGlobalPosOverFloatingUi(const QPoint& globalPos) const
{
    if (m_qmlToolbar && m_qmlToolbar->isVisible() && m_qmlToolbar->geometry().contains(globalPos)) {
        return true;
    }
    if (m_qmlSubToolbar && m_qmlSubToolbar->isVisible() &&
        m_qmlSubToolbar->geometry().contains(globalPos)) {
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

void ScreenCanvasSession::restoreCanvasCursorAt(ScreenCanvas* surface, const QPoint& localPos)
{
    if (!surface) {
        return;
    }

    auto& cursorManager = CursorManager::instance();
    if (!surface->rect().contains(localPos)) {
        cursorManager.setHoverTargetForWidget(surface, HoverTarget::None);
        cursorManager.popCursorForWidget(surface, CursorContext::Override);
        return;
    }

    cursorManager.popCursorForWidget(surface, CursorContext::Override);
    if (!m_toolManager || m_toolManager->isDrawing()) {
        setToolCursor();
        return;
    }

    updateAnnotationCursor(surface, surface->toAnnotationPoint(localPos));
    if (cursorManager.hoverTargetForWidget(surface) == HoverTarget::None) {
        setToolCursor();
        if (m_currentToolId == ToolId::Eraser && m_toolManager) {
            activateSurface(surface);
            m_toolManager->handleMouseMove(surface->toAnnotationPoint(localPos), Qt::NoModifier);
        }
    }

    cursorManager.reapplyCursorForWidget(surface);
}

void ScreenCanvasSession::syncFloatingUiCursor(ScreenCanvas* surface)
{
    if (!surface) {
        return;
    }

    auto& cursorManager = CursorManager::instance();
    auto& authority = CursorAuthority::instance();
    const QPoint globalPos = QCursor::pos();

    bool popupMatched = false;
    if (QWidget* popup = QApplication::activePopupWidget();
        containsGlobalPoint(popup, globalPos)) {
        authority.submitWidgetRequest(
            surface,
            QStringLiteral("floating.popup"),
            CursorRequestSource::Popup,
            cursorSpecForWidget(popup));
        popupMatched = true;
    } else {
        authority.clearWidgetRequest(surface, QStringLiteral("floating.popup"));
    }

    authority.clearWidgetRequest(surface, QStringLiteral("floating.overlay.toolbar"));
    authority.clearWidgetRequest(surface, QStringLiteral("floating.overlay.subtoolbar"));
    authority.clearWidgetRequest(surface, QStringLiteral("floating.overlay.emoji"));

    if (isGlobalPosOverFloatingUi(globalPos)) {
        cursorManager.popCursorForWidget(surface, CursorContext::Override);
        return;
    }

    if (!popupMatched) {
        authority.clearWidgetRequest(surface, QStringLiteral("floating.popup"));
        authority.clearWidgetRequest(surface, QStringLiteral("floating.overlay.emoji"));
    }

    restoreCanvasCursorAt(surface, surface->mapFromGlobal(globalPos));
}

QPoint ScreenCanvasSession::annotationPointForEvent(ScreenCanvas* surface, QMouseEvent* event) const
{
    if (m_grabbedSurface) {
        return annotationPointForCurrentCursor();
    }
    if (!surface || !event) {
        return {};
    }
    return surface->toAnnotationPoint(event->position().toPoint());
}

QPoint ScreenCanvasSession::annotationPointForCurrentCursor() const
{
    return globalToDesktop(QCursor::pos(), m_desktopGeometry);
}

void ScreenCanvasSession::handleSurfaceMousePress(ScreenCanvas* surface, QMouseEvent* event)
{
    if (!surface || !event) {
        return;
    }

    activateSurface(surface);
    if (isGlobalPosOverFloatingUi(event->globalPosition().toPoint())) {
        return;
    }

    if (event->button() == Qt::LeftButton) {
        m_consumeNextToolRelease = false;

        if (ScreenCanvas* editing = editingSurface()) {
            if (editing == surface &&
                editing->inlineTextEditor() &&
                editing->inlineTextEditor()->contains(event->position().toPoint())) {
                return;
            }

            if (editing->inlineTextEditor() &&
                !editing->inlineTextEditor()->textEdit()->toPlainText().trimmed().isEmpty()) {
                editing->inlineTextEditor()->finishEditing();
            } else if (editing->inlineTextEditor()) {
                editing->inlineTextEditor()->cancelEditing();
            }
        }

        const QPoint annotationPos = surface->toAnnotationPoint(event->position().toPoint());
        const QPointF annotationPosF = surface->toAnnotationPointF(event->position());

        if (m_laserPointerActive) {
            beginMouseGrab(surface);
            m_laserRenderer->startDrawing(annotationPos);
            updateAllSurfaces();
            return;
        }

        if (m_toolManager && m_toolManager->isDrawing()) {
            beginMouseGrab(surface);
            m_toolManager->handleMousePress(annotationPosF, event->modifiers());
            return;
        }

        if (handleEmojiStickerAnnotationPress(annotationPos)) {
            beginMouseGrab(surface);
            return;
        }
        if (handleArrowAnnotationPress(annotationPos)) {
            beginMouseGrab(surface);
            return;
        }
        if (handlePolylineAnnotationPress(annotationPos)) {
            beginMouseGrab(surface);
            return;
        }

        if (m_toolManager &&
            m_toolManager->handleShapeInteractionPress(annotationPos, event->modifiers())) {
            if (surface->shapeAnnotationEditor() &&
                (surface->shapeAnnotationEditor()->isDragging() ||
                 surface->shapeAnnotationEditor()->isTransforming())) {
                beginMouseGrab(surface);
            }
            return;
        }

        if (m_toolManager &&
            m_toolManager->handleTextInteractionPress(annotationPos, event->modifiers())) {
            if (surface->textAnnotationEditor() &&
                (surface->textAnnotationEditor()->isDragging() ||
                 surface->textAnnotationEditor()->isTransforming())) {
                beginMouseGrab(surface);
            }
            return;
        }

        if (m_annotationLayer->selectedIndex() >= 0) {
            bool clearedSelectedEmojiInEmojiTool = false;
            if (m_currentToolId == ToolId::EmojiSticker) {
                clearedSelectedEmojiInEmojiTool =
                    dynamic_cast<EmojiStickerAnnotation*>(m_annotationLayer->selectedItem()) != nullptr;
            }
            m_annotationLayer->clearSelection();
            updateAllSurfaces();
            if (clearedSelectedEmojiInEmojiTool) {
                m_consumeNextToolRelease = true;
                return;
            }
        }

        if (!m_laserPointerActive && isDrawingTool(m_currentToolId)) {
            beginMouseGrab(surface);
            m_toolManager->handleMousePress(annotationPosF, event->modifiers());
        }
    } else if (event->button() == Qt::RightButton) {
        if (m_toolManager && m_toolManager->isDrawing()) {
            m_toolManager->cancelDrawing();
            endMouseGrab();
            updateAllSurfaces();
        }
    }
}

void ScreenCanvasSession::handleSurfaceMouseMove(ScreenCanvas* surface, QMouseEvent* event)
{
    if (!surface || !event) {
        return;
    }

    ScreenCanvas* inputSurface = m_grabbedSurface ? m_grabbedSurface.data() : surface;
    activateSurface(inputSurface);

    if (!m_grabbedSurface && isGlobalPosOverFloatingUi(event->globalPosition().toPoint())) {
        syncFloatingUiCursor(surface);
        return;
    }

    const QPoint annotationPos = annotationPointForEvent(inputSurface, event);
    const QPointF annotationPosF = inputSurface->toAnnotationPointF(event->position());

    if (m_laserPointerActive && m_laserRenderer->isDrawing()) {
        m_laserRenderer->updateDrawing(annotationPos);
        updateAllSurfaces();
        return;
    }

    if (m_toolManager &&
        m_toolManager->handleTextInteractionMove(annotationPos, event->modifiers())) {
        return;
    }

    if (m_isEmojiDragging || m_isEmojiScaling || m_isEmojiRotating) {
        handleEmojiStickerAnnotationMove(annotationPos);
        return;
    }

    if (m_toolManager &&
        m_toolManager->handleShapeInteractionMove(annotationPos, event->modifiers())) {
        return;
    }

    if (m_isArrowDragging) {
        handleArrowAnnotationMove(annotationPos);
        return;
    }
    if (m_isPolylineDragging) {
        handlePolylineAnnotationMove(annotationPos);
        return;
    }

    if (m_toolManager && m_toolManager->isDrawing()) {
        m_toolManager->handleMouseMove(annotationPosF, event->modifiers());
    } else {
        syncFloatingUiCursor(surface);
    }
}

void ScreenCanvasSession::handleSurfaceMouseRelease(ScreenCanvas* surface, QMouseEvent* event)
{
    if (!surface || !event) {
        return;
    }

    ScreenCanvas* inputSurface = m_grabbedSurface ? m_grabbedSurface.data() : surface;
    activateSurface(inputSurface);

    if (!m_grabbedSurface && isGlobalPosOverFloatingUi(event->globalPosition().toPoint())) {
        return;
    }
    if (event->button() != Qt::LeftButton) {
        endMouseGrab();
        return;
    }

    const QPoint annotationPos = annotationPointForEvent(inputSurface, event);
    const QPointF annotationPosF = inputSurface->toAnnotationPointF(event->position());

    if (m_laserPointerActive && m_laserRenderer->isDrawing()) {
        m_laserRenderer->stopDrawing();
        endMouseGrab();
        updateAllSurfaces();
        return;
    }
    if (m_laserPointerActive) {
        endMouseGrab();
        return;
    }

    if (m_toolManager &&
        m_toolManager->handleTextInteractionRelease(annotationPos, event->modifiers())) {
        updateAllSurfaces();
        endMouseGrab();
        return;
    }

    if (m_consumeNextToolRelease) {
        m_consumeNextToolRelease = false;
        endMouseGrab();
        return;
    }

    if (handleEmojiStickerAnnotationRelease(annotationPos)) {
        endMouseGrab();
        return;
    }

    if (m_toolManager &&
        m_toolManager->handleShapeInteractionRelease(annotationPos, event->modifiers())) {
        updateAllSurfaces();
        endMouseGrab();
        return;
    }

    if (handleArrowAnnotationRelease(annotationPos)) {
        endMouseGrab();
        return;
    }
    if (handlePolylineAnnotationRelease(annotationPos)) {
        endMouseGrab();
        return;
    }

    if (m_toolManager &&
        (m_toolManager->isDrawing() ||
         m_currentToolId == ToolId::StepBadge ||
         m_currentToolId == ToolId::EmojiSticker)) {
        m_toolManager->handleMouseRelease(annotationPosF, event->modifiers());
    }

    endMouseGrab();
}

void ScreenCanvasSession::handleSurfaceMouseDoubleClick(ScreenCanvas* surface, QMouseEvent* event)
{
    if (!surface || !event || event->button() != Qt::LeftButton) {
        return;
    }

    activateSurface(surface);
    const QPoint annotationPos = surface->toAnnotationPoint(event->position().toPoint());

    if (m_toolManager && m_toolManager->isDrawing()) {
        m_toolManager->handleDoubleClick(annotationPos);
        updateAllSurfaces();
        return;
    }

    if (m_toolManager &&
        m_toolManager->handleTextInteractionDoubleClick(annotationPos)) {
        updateAllSurfaces();
        return;
    }

    m_toolManager->handleDoubleClick(annotationPos);
    updateAllSurfaces();
}

void ScreenCanvasSession::handleSurfaceWheel(ScreenCanvas* surface, QWheelEvent* event)
{
    Q_UNUSED(surface);

    const int delta = event->angleDelta().y();
    if (delta == 0) {
        event->ignore();
        return;
    }

    if (m_currentToolId == ToolId::StepBadge) {
        int current = static_cast<int>(m_stepBadgeSize);
        current = delta > 0 ? (current + 1) % 3 : (current + 2) % 3;
        m_toolOptionsViewModel->handleStepBadgeSizeSelected(current);
        event->accept();
        return;
    }

    if (m_toolOptionsViewModel->handleWidthWheelDelta(delta)) {
        event->accept();
        return;
    }

    event->ignore();
}

void ScreenCanvasSession::handleSurfaceKeyPress(ScreenCanvas* surface, QKeyEvent* event)
{
    activateSurface(surface);

    if (ScreenCanvas* editing = editingSurface()) {
        if (editing->inlineTextEditor() && editing->inlineTextEditor()->isEditing()) {
            if (event->key() == Qt::Key_Escape) {
                editing->inlineTextEditor()->cancelEditing();
            }
            return;
        }
    }

    if (event->key() == Qt::Key_Escape) {
        if (m_toolManager->handleEscape()) {
            updateAllSurfaces();
            return;
        }
        close();
    } else if (event->key() == Qt::Key_W) {
        setBackgroundMode(m_bgMode == CanvasBackgroundMode::Whiteboard
                              ? CanvasBackgroundMode::Screen
                              : CanvasBackgroundMode::Whiteboard);
    } else if (event->key() == Qt::Key_B) {
        setBackgroundMode(m_bgMode == CanvasBackgroundMode::Blackboard
                              ? CanvasBackgroundMode::Screen
                              : CanvasBackgroundMode::Blackboard);
    } else if (event->matches(QKeySequence::Copy)) {
        handleCopyAction(ToolId::Copy);
    } else if (event->matches(QKeySequence::Undo)) {
        if (m_annotationLayer->canUndo()) {
            m_annotationLayer->undo();
            updateAllSurfaces();
        }
    } else if (event->matches(QKeySequence::Redo)) {
        if (m_annotationLayer->canRedo()) {
            m_annotationLayer->redo();
            updateAllSurfaces();
        }
    }
}

void ScreenCanvasSession::handleSurfaceCloseRequest(ScreenCanvas* surface)
{
    Q_UNUSED(surface);
    close();
}

void ScreenCanvasSession::syncColorToAllWidgets(const QColor& color)
{
    m_toolManager->setColor(color);
    m_laserRenderer->setColor(color);
    m_toolOptionsViewModel->setCurrentColor(color);
    for (const QPointer<ScreenCanvas>& surface : m_surfaces) {
        if (surface && surface->inlineTextEditor()) {
            surface->inlineTextEditor()->setColor(color);
        }
    }
}

void ScreenCanvasSession::onColorSelected(const QColor& color)
{
    syncColorToAllWidgets(color);
    AnnotationSettingsManager::instance().saveColor(color);
    updateAllSurfaces();
}

void ScreenCanvasSession::onMoreColorsRequested()
{
    m_toolOptionsViewModel->setCurrentColor(m_toolManager->color());

    QWidget* hostWidget = toolbarHostSurface();
    if (!hostWidget) {
        hostWidget = m_activeSurface;
    }

    AnnotationContext::showColorPickerDialog(
        hostWidget,
        m_colorPickerDialog,
        m_toolManager->color(),
        colorPickerAnchorPoint(),
        [this](const QColor& color) {
            m_toolManager->setColor(color);
            m_toolOptionsViewModel->setCurrentColor(color);
            updateAllSurfaces();
        });
}

QPoint ScreenCanvasSession::colorPickerAnchorPoint() const
{
    if (m_qmlToolbar && m_qmlToolbar->isVisible() && m_qmlToolbar->geometry().isValid()) {
        return m_qmlToolbar->geometry().center();
    }

    QScreen* screen = m_activationScreen.data();
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen) {
        const QRect bounds = clampBoundsForScreen(screen);
        if (bounds.isValid()) {
            return bounds.center();
        }
    }

    return m_desktopGeometry.center();
}

void ScreenCanvasSession::onLineWidthChanged(int width)
{
    m_toolManager->setWidth(width);
    m_laserRenderer->setWidth(width);
    AnnotationSettingsManager::instance().saveWidth(width);
    updateAllSurfaces();
}

void ScreenCanvasSession::onArrowStyleChanged(LineEndStyle style)
{
    m_toolOptionsViewModel->setArrowStyle(static_cast<int>(style));
    m_toolManager->setArrowStyle(style);
    AnnotationSettingsManager::instance().saveArrowStyle(style);
    updateAllSurfaces();
}

void ScreenCanvasSession::onLineStyleChanged(LineStyle style)
{
    m_toolOptionsViewModel->setLineStyle(static_cast<int>(style));
    m_toolManager->setLineStyle(style);
    AnnotationSettingsManager::instance().saveLineStyle(style);
    updateAllSurfaces();
}

void ScreenCanvasSession::onFontSizeDropdownRequested(const QPoint& pos)
{
    if (RegionSettingsHelper* helper = activeSettingsHelper()) {
        if (ScreenCanvas* surface = m_activeSurface ? m_activeSurface.data() : toolbarHostSurface()) {
            const TextFormattingState formatting = surface->textAnnotationEditor()->formatting();
            helper->showFontSizeDropdown(pos, formatting.fontSize);
        }
    }
}

void ScreenCanvasSession::onFontFamilyDropdownRequested(const QPoint& pos)
{
    if (RegionSettingsHelper* helper = activeSettingsHelper()) {
        if (ScreenCanvas* surface = m_activeSurface ? m_activeSurface.data() : toolbarHostSurface()) {
            const TextFormattingState formatting = surface->textAnnotationEditor()->formatting();
            helper->showFontFamilyDropdown(pos, formatting.fontFamily);
        }
    }
}

void ScreenCanvasSession::onFontSizeSelected(int size)
{
    m_toolOptionsViewModel->setFontSize(size);
    for (const QPointer<ScreenCanvas>& surface : m_surfaces) {
        if (surface && surface->textAnnotationEditor()) {
            surface->textAnnotationEditor()->setFontSize(size);
        }
    }
    syncTextFormattingToSubToolbar();
}

void ScreenCanvasSession::onFontFamilySelected(const QString& family)
{
    m_toolOptionsViewModel->setFontFamily(family);
    for (const QPointer<ScreenCanvas>& surface : m_surfaces) {
        if (surface && surface->textAnnotationEditor()) {
            surface->textAnnotationEditor()->setFontFamily(family);
        }
    }
    syncTextFormattingToSubToolbar();
}

void ScreenCanvasSession::onTextEditingFinished(ScreenCanvas* surface,
                                                const QString& text,
                                                const QPoint& position)
{
    if (surface && surface->textAnnotationEditor()) {
        surface->textAnnotationEditor()->finishEditing(text, position, m_toolManager->color());
    }
}

void ScreenCanvasSession::syncTextFormattingToSubToolbar()
{
    ScreenCanvas* surface = m_activeSurface ? m_activeSurface.data() : editingSurface();
    if (!surface && !m_surfaces.isEmpty()) {
        surface = m_surfaces.first();
    }
    if (!surface || !surface->textAnnotationEditor()) {
        return;
    }

    const auto formatting = surface->textAnnotationEditor()->formatting();
    if (surface->inlineTextEditor() && surface->inlineTextEditor()->isEditing()) {
        m_toolOptionsViewModel->setCurrentColor(surface->inlineTextEditor()->color());
    }
    m_toolOptionsViewModel->setBold(formatting.bold);
    m_toolOptionsViewModel->setItalic(formatting.italic);
    m_toolOptionsViewModel->setUnderline(formatting.underline);
    m_toolOptionsViewModel->setFontSize(formatting.fontSize);
    m_toolOptionsViewModel->setFontFamily(formatting.fontFamily);
}

void ScreenCanvasSession::updateQmlToolbarState()
{
    int activeToolId = -1;
    if (m_showSubToolbar) {
        if (m_laserPointerActive) {
            activeToolId = kLaserPointerButtonId;
        } else if (isDrawingTool(m_currentToolId)) {
            activeToolId = static_cast<int>(m_currentToolId);
        }
    }

    if (m_bgMode == CanvasBackgroundMode::Whiteboard) {
        activeToolId = static_cast<int>(ToolId::CanvasWhiteboard);
    }
    if (m_bgMode == CanvasBackgroundMode::Blackboard) {
        activeToolId = static_cast<int>(ToolId::CanvasBlackboard);
    }
    if (m_toolbarViewModel) {
        m_toolbarViewModel->setActiveTool(activeToolId);
    }

    if (!m_toolOptionsViewModel || !m_qmlSubToolbar) {
        if ((m_laserPointerActive || m_currentToolId != ToolId::EmojiSticker || !m_showSubToolbar) &&
            m_emojiPickerPopup && m_emojiPickerPopup->isVisible()) {
            m_emojiPickerPopup->hide();
        }

        if (m_isOpen && m_toolbarPlacementInitialized && !m_toolbarDragging) {
            relayoutFloatingUi(false);
        }
        return;
    }

    if (m_showSubToolbar && !m_laserPointerActive) {
        m_qmlSubToolbar->showForTool(static_cast<int>(m_currentToolId));
    } else if (m_showSubToolbar && m_laserPointerActive) {
        m_toolOptionsViewModel->showLaserPointerOptions();
        if (m_toolOptionsViewModel->hasContent()) {
            m_qmlSubToolbar->show();
        } else {
            m_qmlSubToolbar->hide();
        }
    } else {
        m_toolOptionsViewModel->clearSections();
        m_qmlSubToolbar->hide();
    }

    if ((m_laserPointerActive || m_currentToolId != ToolId::EmojiSticker || !m_showSubToolbar) &&
        m_emojiPickerPopup && m_emojiPickerPopup->isVisible()) {
        m_emojiPickerPopup->hide();
    }

    if (m_isOpen && m_toolbarPlacementInitialized && !m_toolbarDragging) {
        relayoutFloatingUi(false);
    }
}

bool ScreenCanvasSession::initializeToolbarPlacement()
{
    if (!m_qmlToolbar) {
        return false;
    }

    const QSize toolbarSize = m_qmlToolbar->sizeHint();
    if (toolbarSize.isEmpty()) {
        return false;
    }

    const ToolbarPlacementResolution resolution = resolveToolbarPlacement(
        ScreenCanvasSettingsManager::instance().loadToolbarPlacement(),
        QGuiApplication::screens(),
        m_activationScreen.data(),
        toolbarSize);
    if (resolution.screenId.isEmpty()) {
        return false;
    }

    m_qmlToolbar->setPosition(resolution.globalTopLeft);
    m_toolbarPlacementInitialized = true;
    return true;
}

void ScreenCanvasSession::relayoutFloatingUi(bool restoreToolbarPosition)
{
    if (!m_qmlToolbar) {
        return;
    }
    if (m_toolbarDragging && !restoreToolbarPosition) {
        return;
    }

    QSize toolbarSize = m_qmlToolbar->sizeHint();
    if (toolbarSize.isEmpty()) {
        QTimer::singleShot(0, this, [this, restoreToolbarPosition]() {
            relayoutFloatingUi(restoreToolbarPosition);
        });
        return;
    }

    if (restoreToolbarPosition || !m_toolbarPlacementInitialized) {
        const ToolbarPlacementResolution resolution = resolveToolbarPlacement(
            ScreenCanvasSettingsManager::instance().loadToolbarPlacement(),
            QGuiApplication::screens(),
            m_activationScreen.data(),
            toolbarSize);
        if (!resolution.screenId.isEmpty()) {
            m_qmlToolbar->setPosition(resolution.globalTopLeft);
            m_toolbarPlacementInitialized = true;
        }
    }

    refreshFloatingUiParentWidget();
    if (!m_qmlToolbar->isVisible()) {
        m_qmlToolbar->show();
    }
    if (m_qmlSubToolbar && m_qmlSubToolbar->isVisible()) {
        m_qmlSubToolbar->positionBelow(m_qmlToolbar->geometry());
    }
    if (m_emojiPickerPopup && m_emojiPickerPopup->isVisible()) {
        m_emojiPickerPopup->positionAt(m_qmlToolbar->geometry());
    }
    raiseFloatingUiWindows();
}

void ScreenCanvasSession::saveToolbarPlacement() const
{
    if (!m_qmlToolbar || !m_toolbarPlacementInitialized) {
        return;
    }

    const QRect toolbarGeometry = m_qmlToolbar->geometry();
    if (!toolbarGeometry.isValid()) {
        return;
    }

    QScreen* screen = QGuiApplication::screenAt(toolbarGeometry.center());
    if (!screen) {
        screen = m_activationScreen.data();
    }
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return;
    }

    ScreenCanvasSettingsManager::instance().saveToolbarPlacement(
        ScreenCanvasSettingsManager::screenIdentifier(screen),
        toolbarGeometry.topLeft() - screen->geometry().topLeft());
}

void ScreenCanvasSession::handleApplicationStateChanged(Qt::ApplicationState state)
{
    if (!m_isOpen || m_isClosing || state != Qt::ApplicationActive) {
        return;
    }

    restoreSurfaceVisibilityAndStacking(true);
}

void ScreenCanvasSession::connectScreenTopologySignals()
{
    if (m_screenTopologySignalsConnected) {
        return;
    }
    m_screenTopologySignalsConnected = true;

    connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen*) {
        closeForScreenTopologyChange(QStringLiteral("ScreenCanvasSession: Screen added, closing"));
    });
    connect(qApp, &QGuiApplication::screenRemoved, this, [this](QScreen*) {
        closeForScreenTopologyChange(QStringLiteral("ScreenCanvasSession: Screen removed, closing"));
    });

    for (QScreen* screen : QGuiApplication::screens()) {
        connect(screen, &QScreen::geometryChanged, this, [this](const QRect&) {
            closeForScreenTopologyChange(
                QStringLiteral("ScreenCanvasSession: Screen geometry changed, closing"));
        });
    }
}

void ScreenCanvasSession::closeForScreenTopologyChange(const QString& reason)
{
    qWarning().noquote() << reason;
    m_skipToolbarPlacementSaveOnClose = true;
    close();
}

void ScreenCanvasSession::setBackgroundMode(CanvasBackgroundMode mode)
{
    if (m_toolManager && m_toolManager->isDrawing()) {
        m_toolManager->cancelDrawing();
    }
    if (m_laserRenderer && m_laserRenderer->isDrawing()) {
        m_laserRenderer->stopDrawing();
    }

    m_bgMode = mode;
    updateQmlToolbarState();
    updateAllSurfaces();
}

void ScreenCanvasSession::showEmojiPickerPopup()
{
    if (!m_emojiPickerPopup) {
        m_emojiPickerPopup = new SnapTray::QmlEmojiPickerPopup(this);
        connect(m_emojiPickerPopup, &SnapTray::QmlEmojiPickerPopup::emojiSelected,
                this, [this](const QString& emoji) {
                    if (auto* handler = dynamic_cast<EmojiStickerToolHandler*>(
                            m_toolManager->handler(ToolId::EmojiSticker))) {
                        handler->setCurrentEmoji(emoji);
                    }
                });
    }

    refreshFloatingUiParentWidget();
    const QRect anchor = m_qmlToolbar ? m_qmlToolbar->geometry() : QRect();
    m_emojiPickerPopup->showAt(anchor);
}

TextBoxAnnotation* ScreenCanvasSession::getSelectedTextAnnotation()
{
    const int index = m_annotationLayer ? m_annotationLayer->selectedIndex() : -1;
    return index >= 0 ? dynamic_cast<TextBoxAnnotation*>(m_annotationLayer->itemAt(index)) : nullptr;
}

EmojiStickerAnnotation* ScreenCanvasSession::getSelectedEmojiStickerAnnotation()
{
    const int index = m_annotationLayer ? m_annotationLayer->selectedIndex() : -1;
    return index >= 0 ? dynamic_cast<EmojiStickerAnnotation*>(m_annotationLayer->itemAt(index)) : nullptr;
}

ShapeAnnotation* ScreenCanvasSession::getSelectedShapeAnnotation()
{
    const int index = m_annotationLayer ? m_annotationLayer->selectedIndex() : -1;
    return index >= 0 ? dynamic_cast<ShapeAnnotation*>(m_annotationLayer->itemAt(index)) : nullptr;
}

ArrowAnnotation* ScreenCanvasSession::getSelectedArrowAnnotation()
{
    const int index = m_annotationLayer ? m_annotationLayer->selectedIndex() : -1;
    return index >= 0 ? dynamic_cast<ArrowAnnotation*>(m_annotationLayer->itemAt(index)) : nullptr;
}

PolylineAnnotation* ScreenCanvasSession::getSelectedPolylineAnnotation()
{
    const int index = m_annotationLayer ? m_annotationLayer->selectedIndex() : -1;
    return index >= 0 ? dynamic_cast<PolylineAnnotation*>(m_annotationLayer->itemAt(index)) : nullptr;
}

bool ScreenCanvasSession::handleEmojiStickerAnnotationPress(const QPoint& pos)
{
    auto& cursorManager = CursorManager::instance();

    if (auto* emojiItem = getSelectedEmojiStickerAnnotation()) {
        const GizmoHandle handle = TransformationGizmo::hitTest(emojiItem, pos);
        if (handle != GizmoHandle::None) {
            m_isEmojiDragging = false;
            m_isEmojiScaling = false;
            m_isEmojiRotating = false;
            if (handle == GizmoHandle::Body) {
                m_isEmojiDragging = true;
                m_emojiDragStart = pos;
                if (m_activeSurface) {
                    cursorManager.setInputStateForWidget(m_activeSurface, InputState::Moving);
                }
            } else if (handle == GizmoHandle::Rotation) {
                m_isEmojiRotating = true;
                m_activeEmojiHandle = handle;
                m_emojiStartCenter = emojiItem->center();
                m_emojiStartRotation = emojiItem->rotation();
                const QPointF delta = QPointF(pos) - m_emojiStartCenter;
                m_emojiStartAngle = qRadiansToDegrees(qAtan2(delta.y(), delta.x()));
                if (m_activeSurface) {
                    cursorManager.setHoverTargetForWidget(
                        m_activeSurface, HoverTarget::GizmoHandle, static_cast<int>(handle));
                }
            } else {
                m_isEmojiScaling = true;
                m_activeEmojiHandle = handle;
                m_emojiStartCenter = emojiItem->center();
                m_emojiStartScale = emojiItem->scale();
                const QPointF delta = QPointF(pos) - m_emojiStartCenter;
                m_emojiStartDistance = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
                if (m_activeSurface) {
                    cursorManager.setHoverTargetForWidget(
                        m_activeSurface, HoverTarget::GizmoHandle, static_cast<int>(handle));
                }
            }
            updateAllSurfaces();
            return true;
        }
    }

    const int hitIndex = m_annotationLayer->hitTestEmojiSticker(pos);
    if (hitIndex < 0) {
        return false;
    }

    m_annotationLayer->setSelectedIndex(hitIndex);
    if (auto* emojiItem = getSelectedEmojiStickerAnnotation()) {
        m_isEmojiDragging = false;
        m_isEmojiScaling = false;
        m_isEmojiRotating = false;
        const GizmoHandle handle = TransformationGizmo::hitTest(emojiItem, pos);
        if (handle == GizmoHandle::Body || handle == GizmoHandle::None) {
            m_isEmojiDragging = true;
            m_emojiDragStart = pos;
            if (m_activeSurface) {
                cursorManager.setInputStateForWidget(m_activeSurface, InputState::Moving);
            }
        } else if (handle == GizmoHandle::Rotation) {
            m_isEmojiRotating = true;
            m_activeEmojiHandle = handle;
            m_emojiStartCenter = emojiItem->center();
            m_emojiStartRotation = emojiItem->rotation();
            const QPointF delta = QPointF(pos) - m_emojiStartCenter;
            m_emojiStartAngle = qRadiansToDegrees(qAtan2(delta.y(), delta.x()));
        } else {
            m_isEmojiScaling = true;
            m_activeEmojiHandle = handle;
            m_emojiStartCenter = emojiItem->center();
            m_emojiStartScale = emojiItem->scale();
            const QPointF delta = QPointF(pos) - m_emojiStartCenter;
            m_emojiStartDistance = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
        }
    }

    requestLocalizedToolRepaint();
    return true;
}

bool ScreenCanvasSession::handleEmojiStickerAnnotationMove(const QPoint& pos)
{
    if (m_annotationLayer->selectedIndex() < 0) {
        return false;
    }

    auto* emojiItem = getSelectedEmojiStickerAnnotation();
    if (!emojiItem) {
        return false;
    }

    if (m_isEmojiDragging) {
        const QPoint delta = pos - m_emojiDragStart;
        emojiItem->moveBy(delta);
        m_emojiDragStart = pos;
    } else if (m_isEmojiRotating) {
        const QPointF delta = QPointF(pos) - m_emojiStartCenter;
        const qreal currentAngle = qRadiansToDegrees(qAtan2(delta.y(), delta.x()));
        const qreal angleDelta = normalizeAngleDelta(currentAngle - m_emojiStartAngle);
        emojiItem->setRotation(m_emojiStartRotation + angleDelta);
    } else if (m_isEmojiScaling && m_emojiStartDistance > 0.0) {
        const QPointF delta = QPointF(pos) - m_emojiStartCenter;
        const qreal currentDistance = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
        emojiItem->setScale(m_emojiStartScale * (currentDistance / m_emojiStartDistance));
    } else {
        return false;
    }

    requestLocalizedToolRepaint();
    return true;
}

bool ScreenCanvasSession::handleEmojiStickerAnnotationRelease(const QPoint& pos)
{
    Q_UNUSED(pos);
    if (!m_isEmojiDragging && !m_isEmojiScaling && !m_isEmojiRotating) {
        return false;
    }

    m_isEmojiDragging = false;
    m_isEmojiScaling = false;
    m_isEmojiRotating = false;
    m_activeEmojiHandle = GizmoHandle::None;
    m_emojiStartDistance = 0.0;
    m_emojiStartAngle = 0.0;
    if (m_activeSurface) {
        CursorManager::instance().setInputStateForWidget(m_activeSurface, InputState::Idle);
    }
    requestLocalizedToolRepaint();
    return true;
}

bool ScreenCanvasSession::handleArrowAnnotationPress(const QPoint& pos)
{
    if (auto* arrowItem = getSelectedArrowAnnotation()) {
        const GizmoHandle handle = TransformationGizmo::hitTest(arrowItem, pos);
        if (handle != GizmoHandle::None) {
            m_isArrowDragging = true;
            m_arrowDragHandle = handle;
            m_dragStartPos = pos;
            requestLocalizedToolRepaint();
            return true;
        }
    }

    const int hitIndex = m_annotationLayer->hitTestArrow(pos);
    if (hitIndex >= 0) {
        m_annotationLayer->setSelectedIndex(hitIndex);
        m_isArrowDragging = true;
        m_arrowDragHandle = GizmoHandle::Body;
        m_dragStartPos = pos;

        if (auto* arrowItem = getSelectedArrowAnnotation()) {
            const GizmoHandle handle = TransformationGizmo::hitTest(arrowItem, pos);
            if (handle != GizmoHandle::None) {
                m_arrowDragHandle = handle;
            }
        }

        requestLocalizedToolRepaint();
        return true;
    }

    return false;
}

bool ScreenCanvasSession::handleArrowAnnotationMove(const QPoint& pos)
{
    if (!m_isArrowDragging || m_annotationLayer->selectedIndex() < 0) {
        return false;
    }

    auto* arrowItem = getSelectedArrowAnnotation();
    if (!arrowItem) {
        return false;
    }

    if (m_arrowDragHandle == GizmoHandle::Body) {
        const QPoint delta = pos - m_dragStartPos;
        arrowItem->moveBy(delta);
        m_dragStartPos = pos;
    } else if (m_arrowDragHandle == GizmoHandle::ArrowStart) {
        arrowItem->setStart(pos);
    } else if (m_arrowDragHandle == GizmoHandle::ArrowEnd) {
        arrowItem->setEnd(pos);
    } else if (m_arrowDragHandle == GizmoHandle::ArrowControl) {
        const QPointF start = arrowItem->start();
        const QPointF end = arrowItem->end();
        const QPointF newControl = 2.0 * QPointF(pos) - 0.5 * (start + end);
        arrowItem->setControlPoint(newControl.toPoint());
    }

    requestLocalizedToolRepaint();
    return true;
}

bool ScreenCanvasSession::handleArrowAnnotationRelease(const QPoint& pos)
{
    Q_UNUSED(pos);
    if (!m_isArrowDragging) {
        return false;
    }

    m_isArrowDragging = false;
    m_arrowDragHandle = GizmoHandle::None;
    setToolCursor();
    requestLocalizedToolRepaint();
    return true;
}

bool ScreenCanvasSession::handlePolylineAnnotationPress(const QPoint& pos)
{
    if (auto* polylineItem = getSelectedPolylineAnnotation()) {
        const int vertexIndex = TransformationGizmo::hitTestVertex(polylineItem, pos);
        if (vertexIndex >= 0) {
            m_isPolylineDragging = true;
            m_activePolylineVertexIndex = vertexIndex;
            m_dragStartPos = pos;
            requestLocalizedToolRepaint();
            return true;
        }
        if (vertexIndex == -1) {
            m_isPolylineDragging = true;
            m_activePolylineVertexIndex = -1;
            m_dragStartPos = pos;
            requestLocalizedToolRepaint();
            return true;
        }
    }

    const int hitIndex = m_annotationLayer->hitTestPolyline(pos);
    if (hitIndex >= 0) {
        m_annotationLayer->setSelectedIndex(hitIndex);
        m_isPolylineDragging = true;
        m_activePolylineVertexIndex = -1;
        m_dragStartPos = pos;

        if (auto* polylineItem = getSelectedPolylineAnnotation()) {
            const int vertexIndex = TransformationGizmo::hitTestVertex(polylineItem, pos);
            if (vertexIndex >= 0) {
                m_activePolylineVertexIndex = vertexIndex;
            }
        }

        requestLocalizedToolRepaint();
        return true;
    }

    return false;
}

bool ScreenCanvasSession::handlePolylineAnnotationMove(const QPoint& pos)
{
    if (!m_isPolylineDragging || m_annotationLayer->selectedIndex() < 0) {
        return false;
    }

    auto* polylineItem = getSelectedPolylineAnnotation();
    if (!polylineItem) {
        return false;
    }

    if (m_activePolylineVertexIndex >= 0) {
        polylineItem->setPoint(m_activePolylineVertexIndex, pos);
    } else {
        const QPoint delta = pos - m_dragStartPos;
        polylineItem->moveBy(delta);
        m_dragStartPos = pos;
    }

    requestLocalizedToolRepaint();
    return true;
}

bool ScreenCanvasSession::handlePolylineAnnotationRelease(const QPoint& pos)
{
    Q_UNUSED(pos);
    if (!m_isPolylineDragging) {
        return false;
    }

    m_isPolylineDragging = false;
    m_activePolylineVertexIndex = -1;
    setToolCursor();
    requestLocalizedToolRepaint();
    return true;
}

void ScreenCanvasSession::updateAnnotationCursor(ScreenCanvas* surface, const QPoint& pos)
{
    if (!surface) {
        return;
    }

    auto& cursorManager = CursorManager::instance();

    if (auto* textItem = getSelectedTextAnnotation()) {
        const GizmoHandle handle = TransformationGizmo::hitTest(textItem, pos);
        if (handle != GizmoHandle::None) {
            cursorManager.setHoverTargetForWidget(surface, HoverTarget::GizmoHandle, static_cast<int>(handle));
            return;
        }
    }

    if (auto* shapeItem = getSelectedShapeAnnotation()) {
        const GizmoHandle handle = TransformationGizmo::hitTest(shapeItem, pos);
        if (handle != GizmoHandle::None) {
            cursorManager.setHoverTargetForWidget(surface, HoverTarget::GizmoHandle, static_cast<int>(handle));
            return;
        }
    }

    const auto result = CursorManager::hitTestAnnotations(
        pos,
        m_annotationLayer,
        getSelectedEmojiStickerAnnotation(),
        getSelectedArrowAnnotation(),
        getSelectedPolylineAnnotation(),
        m_currentToolId != ToolId::EmojiSticker);

    if (result.hit) {
        cursorManager.setHoverTargetForWidget(surface, result.target, result.handleIndex);
    } else if (m_annotationLayer->hitTestShape(pos) >= 0) {
        cursorManager.setHoverTargetForWidget(surface, HoverTarget::Annotation);
    } else {
        cursorManager.setHoverTargetForWidget(surface, HoverTarget::None);
    }
}
