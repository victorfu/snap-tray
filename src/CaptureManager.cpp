#include "CaptureManager.h"
#include "RegionSelector.h"
#include "PinWindowManager.h"
#include "PinWindow.h"
#include "scroll/ScrollCaptureSession.h"
#include "platform/WindowLevel.h"
#include "WindowDetector.h"
#include "PlatformFeatures.h"
#include "region/MultiRegionManager.h"
#include "pinwindow/RegionLayoutManager.h"

#include <QDebug>
#include <QGuiApplication>
#include <QApplication>
#include <QScreen>
#include <QCursor>
#include <QKeyEvent>
#include <QDialog>
#include <QTimer>

CaptureManager::CaptureManager(PinWindowManager *pinManager, QObject *parent)
    : QObject(parent)
    , m_regionSelector(nullptr)
    , m_scrollSession(nullptr)
    , m_pinManager(pinManager)
    , m_windowDetector(PlatformFeatures::instance().createWindowDetector(this))
{
}

CaptureManager::~CaptureManager()
{
}

bool CaptureManager::isActive() const
{
    return (m_regionSelector && m_regionSelector->isVisible()) ||
           m_scrollSession;
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
    if (m_scrollSession) {
        if (m_scrollSession->isRunning()) {
            m_scrollSession->cancel();
        } else {
            // Session is pending start; destroy immediately so delayed start cannot run.
            delete m_scrollSession;
            m_scrollSession = nullptr;
        }
        return;
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
    if (m_scrollSession) {
        qWarning() << "CaptureManager: Emergency cancel via re-trigger while scroll capture is active";
        cancelCapture();
        return;
    }

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

    // 2. Start window detection FIRST (async, runs in parallel with grabWindow)
    if (m_windowDetector) {
        m_windowDetector->setScreen(targetScreen);
        m_windowDetector->refreshWindowListAsync();
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
    if ((m_regionSelector && m_regionSelector->isVisible()) || m_scrollSession) {
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
    connect(m_regionSelector, &RegionSelector::scrollingCaptureRequested,
            this, &CaptureManager::onScrollingCaptureRequested);
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
    connect(m_regionSelector, &RegionSelector::scrollingCaptureRequested,
            this, &CaptureManager::onScrollingCaptureRequested);

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

void CaptureManager::onScrollingCaptureRequested(const QRect &region, QScreen *screen)
{
    if (!m_pinManager || region.isEmpty() || !screen) {
        emit captureCancelled();
        return;
    }

    if (m_scrollSession) {
        return;
    }

    m_scrollSession = new ScrollCaptureSession(region, screen, this);
    QPointer<ScrollCaptureSession> session = m_scrollSession;
    ScrollCaptureSession *createdSession = m_scrollSession;

    connect(m_scrollSession, &ScrollCaptureSession::captureReady,
            this, [this, session](const QPixmap &screenshot, const QPoint &globalPosition, const QRect &globalRect) {
                onRegionSelected(screenshot, globalPosition, globalRect);
                if (session) {
                    if (m_scrollSession == session) {
                        m_scrollSession = nullptr;
                    }
                    session->deleteLater();
                }
            });
    connect(m_scrollSession, &ScrollCaptureSession::cancelled,
            this, [this, session]() {
                onSelectionCancelled();
                if (session) {
                    if (m_scrollSession == session) {
                        m_scrollSession = nullptr;
                    }
                    session->deleteLater();
                }
            });
    connect(m_scrollSession, &ScrollCaptureSession::failed,
            this, [this, session](const QString &error) {
                qWarning() << "CaptureManager: Scroll capture failed:" << error;
                onSelectionCancelled();
                if (session) {
                    if (m_scrollSession == session) {
                        m_scrollSession = nullptr;
                    }
                    session->deleteLater();
                }
            });
    connect(m_scrollSession, &QObject::destroyed,
            this, [this, createdSession]() {
                if (m_scrollSession == createdSession) {
                    m_scrollSession = nullptr;
                }
            });

    QTimer::singleShot(120, this, [session]() {
        if (session) {
            session->start();
        }
    });
}
