#include "CaptureManager.h"
#include "RegionSelector.h"
#include "PinWindowManager.h"
#include "PinWindow.h"
#include "platform/WindowLevel.h"
#include "WindowDetector.h"
#include "PlatformFeatures.h"

#include <QDebug>
#include <QGuiApplication>
#include <QApplication>
#include <QScreen>
#include <QCursor>
#include <QKeyEvent>

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

void CaptureManager::startRegionCapture()
{
    // Skip if already in capture mode
    if (m_regionSelector && m_regionSelector->isVisible()) {
        qDebug() << "CaptureManager: Already in capture mode, ignoring";
        return;
    }

    qDebug() << "CaptureManager: Starting region capture";

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
        qCritical() << "CaptureManager: No valid screen available for region capture";
        return;
    }

    qDebug() << "CaptureManager: Target screen:" << targetScreen->name()
             << "geometry:" << targetScreen->geometry()
             << "cursor pos:" << QCursor::pos();

    // 2. Start window detection FIRST (async, runs in parallel with grabWindow)
    // This allows window list to be ready by the time RegionSelector is shown
    if (m_windowDetector) {
        m_windowDetector->setScreen(targetScreen);
        m_windowDetector->refreshWindowListAsync();
    }

    // 3. Capture screenshot while popup is still visible
    // This allows capturing context menus (like Snipaste)
    // NOTE: grabWindow() is blocking (50-200ms on 4K). Qt doesn't guarantee thread safety
    // for grabWindow(), so async capture via QtConcurrent is risky without platform testing.
    QWidget *popup = QApplication::activePopupWidget();
    qDebug() << "CaptureManager: Pre-capture screenshot, popup active:" << (popup != nullptr);

    QPixmap preCapture = targetScreen->grabWindow(0);
    qDebug() << "CaptureManager: Screenshot captured, size:" << preCapture.size();

    // 4. Close popup AFTER screenshot to avoid event loop conflict with RegionSelector
    if (popup) {
        qDebug() << "CaptureManager: Closing popup to avoid event loop conflict";
        popup->close();
    }

    initializeRegionSelector(targetScreen, preCapture, /*quickPinMode=*/false);
}

void CaptureManager::startQuickPinCapture()
{
    // Skip if already in capture mode
    if (m_regionSelector && m_regionSelector->isVisible()) {
        qDebug() << "CaptureManager: Already in capture mode, ignoring";
        return;
    }

    qDebug() << "CaptureManager: Starting quick pin capture";

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
        qCritical() << "CaptureManager: No valid screen available for quick pin capture";
        return;
    }

    qDebug() << "CaptureManager: Target screen:" << targetScreen->name()
             << "geometry:" << targetScreen->geometry()
             << "cursor pos:" << QCursor::pos();

    // 2. Start window detection FIRST (async, runs in parallel with grabWindow)
    if (m_windowDetector) {
        m_windowDetector->setScreen(targetScreen);
        m_windowDetector->refreshWindowListAsync();
    }

    // 3. Capture screenshot while popup is still visible
    QWidget *popup = QApplication::activePopupWidget();
    qDebug() << "CaptureManager: Pre-capture screenshot, popup active:" << (popup != nullptr);

    QPixmap preCapture = targetScreen->grabWindow(0);
    qDebug() << "CaptureManager: Screenshot captured, size:" << preCapture.size();

    // 4. Close popup AFTER screenshot
    if (popup) {
        qDebug() << "CaptureManager: Closing popup to avoid event loop conflict";
        popup->close();
    }

    initializeRegionSelector(targetScreen, preCapture, /*quickPinMode=*/true);
}

void CaptureManager::onRegionSelected(const QPixmap &screenshot, const QPoint &globalPosition, const QRect &globalRect)
{
    qDebug() << "CaptureManager: Region selected at global position:" << globalPosition;
    // m_regionSelector will auto-null via QPointer when WA_DeleteOnClose triggers

    // Use the global coordinates computed by RegionSelector
    if (m_pinManager) {
        QScreen *targetScreen = m_regionSelector ? m_regionSelector->screen() : nullptr;
        PinWindow *window = m_pinManager->createPinWindow(screenshot, globalPosition);
        if (window && targetScreen) {
            window->setSourceRegion(globalRect, targetScreen);
        }
    }

    emit captureCompleted(screenshot);
}

void CaptureManager::onSelectionCancelled()
{
    qDebug() << "CaptureManager: Selection cancelled";
    // m_regionSelector will auto-null via QPointer when WA_DeleteOnClose triggers

    emit captureCancelled();
}

void CaptureManager::startRegionCaptureWithPreset(const QRect &region, QScreen *screen)
{
    // If already in capture mode, ignore
    if (m_regionSelector && m_regionSelector->isVisible()) {
        qDebug() << "CaptureManager: Already in capture mode, ignoring";
        return;
    }

    qDebug() << "CaptureManager: Starting region capture with preset region:" << region;

    // Clean up any existing selector
    if (m_regionSelector) {
        m_regionSelector->close();
    }

    emit captureStarted();

    // Create RegionSelector
    m_regionSelector = new RegionSelector();

    // Setup window detector if available
    // Use async version to avoid blocking UI startup
    if (m_windowDetector) {
        m_windowDetector->setScreen(screen);
        m_windowDetector->refreshWindowListAsync();
        m_regionSelector->setWindowDetector(m_windowDetector);
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
                                              bool quickPinMode)
{
    m_regionSelector = new RegionSelector();

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
}
