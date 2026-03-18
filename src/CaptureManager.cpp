#include "CaptureManager.h"
#include "RegionSelector.h"
#include "PinWindowManager.h"
#include "PinWindow.h"
#include "platform/WindowLevel.h"
#include "WindowDetector.h"
#include "PlatformFeatures.h"
#include "history/HistoryStore.h"
#include "region/MultiRegionManager.h"
#include "pinwindow/RegionLayoutManager.h"

#include <QDebug>
#include <QGuiApplication>
#include <QApplication>
#include <QScreen>
#include <QCursor>
#include <QKeyEvent>
#include <QDialog>

namespace {

QScreen* fallbackReplayScreen()
{
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    return screen;
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

    return fallbackReplayScreen();
}

} // namespace

CaptureManager::CaptureManager(PinWindowManager *pinManager, QObject *parent)
    : QObject(parent)
    , m_regionSelector(nullptr)
    , m_pinManager(pinManager)
    , m_windowDetector(PlatformFeatures::instance().createWindowDetector(this))
{
}

CaptureManager::~CaptureManager()
{
}

bool CaptureManager::isActive() const
{
    return m_regionSelector && m_regionSelector->isVisible();
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
    if (entryId.isEmpty()) {
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

    if (m_windowDetector) {
        m_windowDetector->setScreen(targetScreen);
#ifdef Q_OS_MACOS
        m_windowDetector->refreshWindowList();
#else
        m_windowDetector->refreshWindowListAsync();
#endif
    }

    QWidget* popup = QApplication::activePopupWidget();
    QWidget* modal = QApplication::activeModalWidget();
    const QPixmap preCapture = targetScreen->grabWindow(0);

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
    // Skip if already in capture mode
    if (m_regionSelector && m_regionSelector->isVisible()) {
        return;
    }

    // Clean up any existing selector (QPointer auto-nulls when deleted)
    if (m_regionSelector) {
        m_regionSelector->close();
    }

    emit captureStarted();

    // 1. Determine target screen (the screen where cursor is located)
    QScreen *targetScreen = QGuiApplication::screenAt(QCursor::pos());
    if (!targetScreen) {
        targetScreen = QGuiApplication::primaryScreen();
    }

    if (!targetScreen) {
        if (mode == CaptureEntryMode::QuickPin) {
            qCritical() << "CaptureManager: No valid screen available for quick pin capture";
        } else {
            qCritical() << "CaptureManager: No valid screen available for region capture";
        }
        return;
    }

    // 2. Prime window detection before we capture the screen.
    // On macOS, transient UI such as menu-bar popups can disappear as soon as
    // the capture overlay activates. Refresh synchronously here so the
    // detector cache matches the same UI snapshot used for preCapture.
    if (m_windowDetector) {
        m_windowDetector->setScreen(targetScreen);
#ifdef Q_OS_MACOS
        m_windowDetector->refreshWindowList();
#else
        m_windowDetector->refreshWindowListAsync();
#endif
    }

    // 3. Capture screenshot while popup/modal is still visible
    QWidget *popup = QApplication::activePopupWidget();
    QWidget *modal = QApplication::activeModalWidget();

    QPixmap preCapture = targetScreen->grabWindow(0);

    // 4. Close popup/modal AFTER screenshot
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

    const bool quickPinMode = (mode == CaptureEntryMode::QuickPin);
    initializeRegionSelector(targetScreen, preCapture, quickPinMode, showShortcutHintsOnEntry);
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

        PinWindow *window = m_pinManager->createPinWindow(screenshot, globalPosition);
        if (window && !globalRect.isEmpty() && targetScreen) {
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
    if (m_regionSelector && m_regionSelector->isVisible()) {
        return;
    }

    // Clean up any existing selector
    if (m_regionSelector) {
        m_regionSelector->close();
    }

    emit captureStarted();

    // Create RegionSelector
    m_regionSelector = new RegionSelector();
    m_regionSelector->setShowShortcutHintsOnEntry(false);

    // Setup window detector if available
    // Use async version to avoid blocking UI startup
    if (m_windowDetector) {
        m_windowDetector->setScreen(screen);
        m_windowDetector->refreshWindowListAsync();
        m_regionSelector->setWindowDetector(m_windowDetector);

        // Trigger initial window highlight once the async window list is ready.
        if (m_windowDetector->isEnabled()) {
            const auto regionSelector = m_regionSelector;
            const auto triggerDetection = [regionSelector]() {
                if (!regionSelector) {
                    return;
                }
                regionSelector->refreshWindowDetectionAtCursor();
            };

            if (m_windowDetector->isRefreshComplete()) {
                triggerDetection();
            } else {
                connect(m_windowDetector, &WindowDetector::windowListReady,
                        m_regionSelector, [triggerDetection]() {
                            triggerDetection();
                        });
            }
        }
    }

    // Initialize with preset region
    m_regionSelector->initializeWithRegion(screen, region);

    connect(m_regionSelector, &RegionSelector::regionSelected,
            this, &CaptureManager::onRegionSelected);
    connect(m_regionSelector, &RegionSelector::selectionCancelled,
            this, &CaptureManager::onSelectionCancelled);
    connect(m_regionSelector, &RegionSelector::recordingRequested,
            this, &CaptureManager::recordingRequested);
    connect(m_regionSelector, &RegionSelector::saveCompleted,
            this, &CaptureManager::saveCompleted);
    connect(m_regionSelector, &RegionSelector::saveFailed,
            this, &CaptureManager::saveFailed);

    m_regionSelector->setGeometry(screen->geometry());
    m_regionSelector->show();
    raiseWindowAboveMenuBar(m_regionSelector);

    m_regionSelector->activateWindow();
    m_regionSelector->raise();
}

void CaptureManager::initializeRegionSelector(QScreen *targetScreen,
                                              const QPixmap &preCapture,
                                              bool quickPinMode,
                                              bool showShortcutHintsOnEntry)
{
    m_regionSelector = new RegionSelector();
    m_regionSelector->setShowShortcutHintsOnEntry(showShortcutHintsOnEntry);

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
        connect(m_regionSelector, &RegionSelector::recordingRequested,
                this, &CaptureManager::recordingRequested);
        connect(m_regionSelector, &RegionSelector::saveCompleted,
                this, &CaptureManager::saveCompleted);
        connect(m_regionSelector, &RegionSelector::saveFailed,
                this, &CaptureManager::saveFailed);
    }

    // Trigger initial window highlight once the async window list is ready.
    if (m_windowDetector && m_windowDetector->isEnabled()) {
        const auto regionSelector = m_regionSelector;
        const auto triggerDetection = [regionSelector]() {
            if (!regionSelector) {
                return;
            }
            regionSelector->refreshWindowDetectionAtCursor();
        };

        if (m_windowDetector->isRefreshComplete()) {
            triggerDetection();
        } else {
            connect(m_windowDetector, &WindowDetector::windowListReady,
                    m_regionSelector, [triggerDetection]() {
                        triggerDetection();
                    });
        }
    }

    m_regionSelector->setGeometry(targetScreen->geometry());
    m_regionSelector->show();
    raiseWindowAboveMenuBar(m_regionSelector);

    m_regionSelector->activateWindow();
    m_regionSelector->raise();

    // Ensure cursor is set AFTER show/activate to fix race condition where
    // cursor was set before widget visibility, then Qt reset it to ArrowCursor
    m_regionSelector->ensureCrossCursor();
}
