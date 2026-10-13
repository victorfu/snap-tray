#include "CaptureManager.h"
#include "RegionSelector.h"
#include "PinWindowManager.h"
#include "PinWindow.h"
#include "capture/OwnWindowCaptureHider.h"
#include "capture/ScreenSnapshot.h"
#include "platform/WindowLevel.h"
#include "WindowDetector.h"
#include "PlatformFeatures.h"
#include "annotations/AnnotationLayer.h"
#include "history/AnnotationSerializer.h"
#include "history/HistoryRecorder.h"
#include "history/HistoryStore.h"
#include "region/RegionExportManager.h"
#include "settings/PinWindowSettingsManager.h"
#include "region/CapturePerfRecorder.h"
#include "region/MultiRegionManager.h"
#include "pinwindow/RegionLayoutManager.h"

#include <QDebug>
#include <QGuiApplication>
#include <QApplication>
#include <QScreen>
#include <QCursor>
#include <QKeyEvent>
#include <QDialog>
#include <QImage>
#include <QTimer>

namespace {

using FullScreenCaptureAction = RegionCaptureSettingsManager::FullScreenCaptureAction;

QScreen* screenAtCursor()
{
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    return screen;
}

// Close popups/modals only after the frame has been captured so they can be included.
void closeTransientWindows(QWidget* popup, QWidget* modal)
{
    if (popup) {
        popup->close();
    }
    if (modal) {
        if (QDialog* dialog = qobject_cast<QDialog*>(modal)) {
            dialog->reject();
        } else {
            modal->close();
        }
    }
}

QString monitorIdentifier(QScreen* screen)
{
    const int monitorIndex = QGuiApplication::screens().indexOf(screen);
    return monitorIndex >= 0 ? QString::number(monitorIndex) : QStringLiteral("unknown");
}

SnapTray::CaptureSessionWriteRequest makeFullScreenHistoryRequest(
    const QPixmap& snapshot, const QImage& resultImage, const QRect& localRect, QScreen* screen)
{
    const AnnotationLayer emptyAnnotations;

    SnapTray::CaptureSessionWriteRequest request;
    request.canvasImage = snapshot.toImage();
    request.resultImage = resultImage;
    request.selectionRect = localRect;
    request.annotationsJson = SnapTray::serializeAnnotationLayer(emptyAnnotations);
    request.devicePixelRatio = screen->devicePixelRatio();
    request.canvasLogicalSize = screen->geometry().size();
    request.maxEntries = PinWindowSettingsManager::instance().loadMaxCacheFiles();
    return request;
}

QScreen* chooseReplayScreen(const SnapTray::HistoryEntry& entry)
{
    if (entry.canvasLogicalSize.isValid()) {
        const auto screens = QGuiApplication::screens();
        for (QScreen* screen : screens) {
            if (screen && screen->geometry().size() == entry.canvasLogicalSize) {
                return screen;
            }
        }
    }

    return screenAtCursor();
}

} // namespace

CaptureManager::CaptureManager(PinWindowManager *pinManager, QObject *parent)
    : QObject(parent)
    , m_regionSelector(nullptr)
    , m_pinManager(pinManager)
    , m_windowDetector(PlatformFeatures::instance().createWindowDetector(this))
    , m_ownWindowHider(std::make_unique<snaptray::capture::OwnWindowCaptureHider>())
{
}

CaptureManager::~CaptureManager()
{
}

bool CaptureManager::isActive() const
{
    // A full-screen capture owns the screen until it is grabbed and exported, so
    // other capture, canvas and recording entry points must stay out.
    return (m_regionSelector && m_regionSelector->isVisible()) || isFullScreenCaptureInProgress();
}

bool CaptureManager::hasCompleteSelection() const
{
    return m_regionSelector && m_regionSelector->isVisible() &&
           m_regionSelector->isSelectionComplete();
}

void CaptureManager::triggerFinishSelection()
{
    if (m_regionSelector && m_regionSelector->isVisible()) {
        QKeyEvent event(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(m_regionSelector, &event);
    }
}

void CaptureManager::cancelCapture()
{
    if (m_fullScreenCapturePending) {
        qDebug() << "CaptureManager: Cancelling pending full screen capture";
        ++m_fullScreenCaptureGeneration;
        m_fullScreenCapturePending = false;
        m_ownWindowHider->restore();
        emit captureCancelled();
    }

    if (m_regionSelector && m_regionSelector->isVisible()) {
        qDebug() << "CaptureManager: Cancelling active capture via CLI";
        m_regionSelector->close();
    }
}

void CaptureManager::cycleOrSwitchCaptureScreenByCursor()
{
    if (!m_regionSelector || !m_regionSelector->isVisible()) {
        return;
    }

    QScreen* cursorScreen = QGuiApplication::screenAt(QCursor::pos());
    if (!cursorScreen) {
        cursorScreen = QGuiApplication::primaryScreen();
    }
    if (!cursorScreen) {
        return;
    }

    if (m_regionSelector->screen() == cursorScreen) {
        return;
    }

    m_regionSelector->switchToScreen(cursorScreen, true);
}

bool CaptureManager::startHistoryReplay(const QString& entryId)
{
    if (entryId.isEmpty() || isFullScreenCaptureInProgress()) {
        return false;
    }

    if (m_regionSelector && m_regionSelector->isVisible()) {
        return m_regionSelector->beginHistoryReplay(entryId);
    }

    if (m_regionSelector) {
        m_regionSelector->close();
    }

    const auto entry = SnapTray::HistoryStore::loadEntry(entryId);
    if (!entry.has_value()) {
        return false;
    }

    emit captureStarted();
    QScreen* targetScreen = chooseReplayScreen(*entry);
    if (!targetScreen) {
        qCritical() << "CaptureManager: No valid screen available for history replay";
        emit captureCancelled();
        return false;
    }

    refreshWindowDetectorForCapture(targetScreen);

    QWidget* popup = QApplication::activePopupWidget();
    QWidget* modal = QApplication::activeModalWidget();
    const QPixmap preCapture = snaptray::capture::captureScreenSnapshot(targetScreen);
    closeTransientWindows(popup, modal);

    initializeRegionSelector(targetScreen, preCapture, false, false);
    if (!m_regionSelector || !m_regionSelector->beginHistoryReplay(entryId)) {
        if (m_regionSelector) {
            m_regionSelector->close();
        }
        emit captureCancelled();
        return false;
    }

    return true;
}

void CaptureManager::startRegionCapture(bool showShortcutHintsOnEntry)
{
    startCaptureInternal(CaptureEntryMode::Region, showShortcutHintsOnEntry);
}

void CaptureManager::startQuickPinCapture()
{
    startCaptureInternal(CaptureEntryMode::QuickPin, false);
}

void CaptureManager::startCaptureInternal(CaptureEntryMode mode, bool showShortcutHintsOnEntry)
{
    snaptray::region::CapturePerfScope capturePerfScope("CaptureManager.startCaptureInternal");

    // Skip if already in capture mode
    if (isActive()) {
        return;
    }

    // Clean up any existing selector (QPointer auto-nulls when deleted)
    if (m_regionSelector) {
        m_regionSelector->close();
    }

    emit captureStarted();
    // 1. Determine target screen (the screen where cursor is located)
    QScreen *targetScreen = screenAtCursor();

    if (!targetScreen) {
        if (mode == CaptureEntryMode::QuickPin) {
            qCritical() << "CaptureManager: No valid screen available for quick pin capture";
        } else {
            qCritical() << "CaptureManager: No valid screen available for region capture";
        }
        return;
    }

    // 2. Keep detector state in sync with the frame we capture.
    {
        snaptray::region::CapturePerfScope detectorPerfScope(
            "CaptureManager.refreshWindowDetectorForCapture");
        refreshWindowDetectorForCapture(targetScreen);
    }

    // 3. Capture screenshot while popup/modal is still visible
    QWidget *popup = QApplication::activePopupWidget();
    QWidget *modal = QApplication::activeModalWidget();

    QPixmap preCapture;
    {
        snaptray::region::CapturePerfScope snapshotPerfScope(
            "CaptureManager.captureScreenSnapshot");
        preCapture = snaptray::capture::captureScreenSnapshot(targetScreen);
    }

    // 4. Close popup/modal AFTER screenshot
    closeTransientWindows(popup, modal);

    const bool quickPinMode = (mode == CaptureEntryMode::QuickPin);
    {
        snaptray::region::CapturePerfScope selectorPerfScope(
            "CaptureManager.initializeRegionSelector");
        initializeRegionSelector(targetScreen, preCapture, quickPinMode, showShortcutHintsOnEntry);
    }
}

void CaptureManager::onRegionSelected(const QPixmap &screenshot, const QPoint &globalPosition, const QRect &globalRect)
{
    // m_regionSelector will auto-null via QPointer when WA_DeleteOnClose triggers

    // Use the global coordinates computed by RegionSelector
    if (m_pinManager) {
        QScreen *targetScreen = nullptr;
        if (!globalRect.isEmpty()) {
            targetScreen = QGuiApplication::screenAt(globalRect.center());
        }
        if (!targetScreen) {
            targetScreen = QGuiApplication::screenAt(globalPosition);
        }
        if (!targetScreen && m_regionSelector) {
            targetScreen = m_regionSelector->screen();
        }

        PinWindow *window = m_pinManager->createPinWindow(screenshot, globalPosition, false);
        if (window
            && PlatformFeatures::instance().capabilities().supportsLiveCapture
            && !globalRect.isEmpty()
            && targetScreen) {
            window->setSourceRegion(globalRect, targetScreen);
        }

        // Pass multi-region data if this was a multi-region capture
        if (window && m_regionSelector && m_regionSelector->isMultiRegionCapture()) {
            MultiRegionManager* mrm = m_regionSelector->multiRegionManager();
            if (mrm && mrm->count() > 1) {
                qreal dpr = screenshot.devicePixelRatio();
                QVector<QImage> images = mrm->separateImages(
                    m_regionSelector->backgroundPixmap(), dpr);
                auto regions = mrm->regions();
                QRect bounds = mrm->boundingBox();

                QVector<LayoutRegion> layoutRegions;
                for (int i = 0; i < regions.size() && i < images.size(); ++i) {
                    LayoutRegion lr;
                    // Convert region rect to be relative to bounding box
                    lr.rect = regions[i].rect.translated(-bounds.topLeft());
                    lr.originalRect = lr.rect;
                    lr.image = images[i];
                    lr.color = regions[i].color;
                    lr.index = regions[i].index;
                    lr.isSelected = false;
                    layoutRegions.append(lr);
                }

                window->setMultiRegionData(layoutRegions);
            }
        }

        if (window) {
            window->showPreparedWindow();
            if (m_pinManager->arePinsHidden()) {
                m_pinManager->setAllPinsVisible(true);
            }
        }
    }

    emit captureCompleted(screenshot);
}

void CaptureManager::onSelectionCancelled()
{
    // m_regionSelector will auto-null via QPointer when WA_DeleteOnClose triggers
    emit captureCancelled();
}

void CaptureManager::startRegionCaptureWithPreset(const QRect &region, QScreen *screen)
{
    // If already in capture mode, ignore
    if (isActive()) {
        return;
    }

    // Clean up any existing selector
    if (m_regionSelector) {
        m_regionSelector->close();
    }

    emit captureStarted();
    startPresetRegionCapture(screen, region, QPixmap());
}

void CaptureManager::startPresetRegionCapture(QScreen *screen,
                                              const QRect &region,
                                              const QPixmap &preCapture)
{
    // Create RegionSelector
    m_regionSelector = createRegionSelector(false);

    // Keep detector state aligned with the prepared reveal flow used by normal region entry.
    if (m_windowDetector && screen) {
        refreshWindowDetectorForCapture(screen);
        m_regionSelector->setWindowDetector(m_windowDetector);
    }

    // Initialize with preset region
    m_regionSelector->initializeWithRegion(screen, region, preCapture);

    connect(m_regionSelector, &RegionSelector::regionSelected,
            this, &CaptureManager::onRegionSelected);
    connect(m_regionSelector, &RegionSelector::selectionCancelled,
            this, &CaptureManager::onSelectionCancelled);
    connect(m_regionSelector, &RegionSelector::saveCompleted,
            this, &CaptureManager::saveCompleted);
    connect(m_regionSelector, &RegionSelector::saveFailed,
            this, &CaptureManager::saveFailed);

    showPreparedRegionSelector(screen);
}

bool CaptureManager::isFullScreenCaptureInProgress() const
{
    return m_fullScreenCapturePending || !m_fullScreenExport.isNull();
}

void CaptureManager::startFullScreenCapture()
{
    startFullScreenCaptureOnScreen(screenAtCursor());
}

void CaptureManager::startFullScreenCaptureOnScreen(QScreen* screen)
{
    if (isActive()) {
        return;
    }
    if (!screen) {
        qCritical() << "CaptureManager: No valid screen available for full screen capture";
        return;
    }

    if (m_regionSelector) {
        m_regionSelector->close();
    }

    emit captureStarted();
    m_fullScreenCapturePending = true;
    const quint64 generation = ++m_fullScreenCaptureGeneration;

    // SnapTray's own UI must not appear in the capture. Give the compositor a
    // frame to drop the hidden windows before grabbing the screen.
    m_ownWindowHider->hide();
    const int settleDelayMs = m_ownWindowHider->hasHiddenWindows()
        ? snaptray::capture::OwnWindowCaptureHider::kSettleDelayMs
        : 0;
    const QPointer<QScreen> targetScreen(screen);
    QTimer::singleShot(settleDelayMs, this, [this, targetScreen, generation]() {
        if (!m_fullScreenCapturePending || generation != m_fullScreenCaptureGeneration) {
            return;  // Cancelled while waiting for the compositor.
        }
        captureFullScreenAfterHidingOwnWindows(targetScreen.data());
    });
}

void CaptureManager::captureFullScreenAfterHidingOwnWindows(QScreen* screen)
{
    const QPixmap snapshot = screen ? captureSnapshot(screen) : QPixmap();

    // Dismiss popups/modals the same way region capture does, then bring the
    // remaining windows back.
    closeTransientWindows(QApplication::activePopupWidget(), QApplication::activeModalWidget());
    m_ownWindowHider->restore();
    m_fullScreenCapturePending = false;

    if (snapshot.isNull()) {
        qWarning() << "CaptureManager: Failed to capture screen for full screen capture";
        emit captureCancelled();
        return;
    }

    const FullScreenCaptureAction action =
        RegionCaptureSettingsManager::instance().fullScreenCaptureAction();
    if (action == FullScreenCaptureAction::Edit) {
        startPresetRegionCapture(screen, screen->geometry(), snapshot);
        return;
    }

    exportFullScreenCapture(screen, snapshot, action);
}

QPixmap CaptureManager::captureSnapshot(QScreen* screen) const
{
    if (m_snapshotOverride) {
        return m_snapshotOverride(screen);
    }
    return snaptray::capture::captureScreenSnapshot(screen);
}

void CaptureManager::exportFullScreenCapture(QScreen* screen,
                                             const QPixmap& snapshot,
                                             FullScreenCaptureAction action)
{
    auto* exporter = new RegionExportManager(this);
    m_fullScreenExport = exporter;
    exporter->setBackgroundPixmap(snapshot);
    exporter->setDevicePixelRatio(screen->devicePixelRatio());
    exporter->setSourceScreen(screen);
    exporter->setMonitorIdentifier(monitorIdentifier(screen));

    // The whole screen in screen-local logical coordinates. The exporter maps it
    // to the covering device-pixel rect, clamped to the snapshot, so fractional
    // scale factors keep every native pixel.
    const QRect localRect(QPoint(0, 0), screen->geometry().size());
    RegionExportManager::PreparedExport prepared = exporter->prepareExport(localRect);
    if (!prepared.isValid()) {
        qWarning() << "CaptureManager: Failed to prepare full screen capture" << localRect;
        finishFullScreenExport();
        emit captureCancelled();
        return;
    }

    SnapTray::CaptureSessionWriteRequest historyRequest =
        makeFullScreenHistoryRequest(snapshot, prepared.image, localRect, screen);

    if (action == FullScreenCaptureAction::CopyToClipboard) {
        const QPixmap pixmap = prepared.pixmap;
        auto finishCopy = [this, pixmap, historyRequest = std::move(historyRequest)](
                              PlatformFeatures::ClipboardCopyResult result) mutable {
            finishFullScreenExport();
            switch (result) {
            case PlatformFeatures::ClipboardCopyResult::Success:
                SnapTray::HistoryRecorder::instance().submitCaptureSession(
                    std::move(historyRequest));
                emit copyCompleted(pixmap);
                emit captureCompleted(pixmap);
                break;
            case PlatformFeatures::ClipboardCopyResult::Failed:
                qWarning() << "CaptureManager: Failed to copy full screen capture to clipboard";
                emit copyFailed();
                break;
            case PlatformFeatures::ClipboardCopyResult::Superseded:
                emit captureCancelled();
                break;
            }
        };

        if (m_clipboardWriterOverride) {
            const QPointer<CaptureManager> guard(this);
            m_clipboardWriterOverride(
                prepared.image,
                [guard, finishCopy = std::move(finishCopy)](bool success) mutable {
                    if (guard) {
                        finishCopy(success ? PlatformFeatures::ClipboardCopyResult::Success
                                           : PlatformFeatures::ClipboardCopyResult::Failed);
                    }
                });
        } else {
            PlatformFeatures::instance().copyImageToClipboardForGuiAsync(
                prepared.image, this, std::move(finishCopy));
        }
        return;
    }

    const RegionExportManager::SaveRequest saveRequest =
        exporter->createSaveRequest(localRect, nullptr);
    if (saveRequest.cancelled) {
        finishFullScreenExport();
        emit captureCancelled();
        return;
    }
    if (!saveRequest.error.isEmpty()) {
        finishFullScreenExport();
        emit saveFailed(saveRequest.filePath, saveRequest.error);
        return;
    }

    connect(exporter, &RegionExportManager::saveCompleted, this,
        [this, historyRequest = std::move(historyRequest)](
            const QPixmap& pixmap, const QImage& image, const QString& filePath) mutable {
            finishFullScreenExport();
            historyRequest.resultImage = image;
            SnapTray::HistoryRecorder::instance().submitCaptureSession(std::move(historyRequest));
            emit saveCompleted(pixmap, filePath);
            emit captureCompleted(pixmap);
        });
    connect(exporter, &RegionExportManager::saveFailed, this,
        [this](const QString& filePath, const QString& error) {
            finishFullScreenExport();
            emit saveFailed(filePath, error);
        });
    exporter->savePreparedExportAsync(std::move(prepared), saveRequest);
}

void CaptureManager::finishFullScreenExport()
{
    if (m_fullScreenExport) {
        m_fullScreenExport->deleteLater();
        m_fullScreenExport = nullptr;
    }
}

void CaptureManager::initializeRegionSelector(QScreen *targetScreen,
                                              const QPixmap &preCapture,
                                              bool quickPinMode,
                                              bool showShortcutHintsOnEntry)
{
    m_regionSelector = createRegionSelector(showShortcutHintsOnEntry);

    if (quickPinMode) {
        m_regionSelector->setQuickPinMode(true);
    }

    if (m_windowDetector) {
        m_regionSelector->setWindowDetector(m_windowDetector);
    }

    m_regionSelector->initializeForScreen(targetScreen, preCapture);

    // Core signals (always connected)
    connect(m_regionSelector, &RegionSelector::regionSelected,
            this, &CaptureManager::onRegionSelected);
    connect(m_regionSelector, &RegionSelector::selectionCancelled,
            this, &CaptureManager::onSelectionCancelled);

    // Additional signals for full region capture mode
    if (!quickPinMode) {
        connect(m_regionSelector, &RegionSelector::saveCompleted,
                this, &CaptureManager::saveCompleted);
        connect(m_regionSelector, &RegionSelector::saveFailed,
                this, &CaptureManager::saveFailed);
    }

    showPreparedRegionSelector(targetScreen);
}

void CaptureManager::showPreparedRegionSelector(QScreen *targetScreen)
{
    snaptray::region::CapturePerfScope perfScope("CaptureManager.showPreparedRegionSelector");

    if (!m_regionSelector || !targetScreen) {
        return;
    }

    m_regionSelector->setGeometry(targetScreen->geometry());
#ifdef Q_OS_LINUX
    // Keep the window manager in control on GNOME/X11; bypass windows can render
    // as a black ARGB fullscreen surface instead of the captured desktop image.
    m_regionSelector->showFullScreen();
#else
    m_regionSelector->show();
#endif
    raiseWindowAboveMenuBar(m_regionSelector);

    m_regionSelector->activateWindow();
    m_regionSelector->raise();

    // Ensure cursor is set AFTER show/activate to fix race condition where
    // cursor was set before widget visibility, then Qt reset it to ArrowCursor
    m_regionSelector->ensureCrossCursor();
}

void CaptureManager::prewarmWindowDetector()
{
    if (!m_windowDetector) {
        return;
    }

    QScreen *targetScreen = screenAtCursor();
    if (!targetScreen) {
        return;
    }

    refreshWindowDetectorAsync(targetScreen);
}

void CaptureManager::refreshWindowDetectorForCapture(QScreen *screen)
{
    if (!m_windowDetector || !screen) {
        return;
    }

    m_windowDetector->setScreen(screen);
#ifdef Q_OS_MACOS
    m_windowDetector->refreshWindowList(WindowDetector::QueryMode::TopLevelOnly);
#else
    m_windowDetector->refreshWindowListAsync(WindowDetector::QueryMode::TopLevelOnly);
#endif
}

void CaptureManager::refreshWindowDetectorAsync(QScreen *screen)
{
    if (!m_windowDetector || !screen) {
        return;
    }

    m_windowDetector->setScreen(screen);
    m_windowDetector->refreshWindowListAsync(WindowDetector::QueryMode::TopLevelOnly);
}

RegionSelector *CaptureManager::createRegionSelector(bool showShortcutHintsOnEntry)
{
    auto *selector = new RegionSelector();
    selector->setShowShortcutHintsOnEntry(showShortcutHintsOnEntry);
    return selector;
}
