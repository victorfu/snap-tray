#include "CaptureManager.h"
#include "RegionSelector.h"
#include "PinWindowManager.h"
#include "platform/WindowLevel.h"
#include "WindowDetector.h"
#include "PlatformFeatures.h"
#include "scrolling/ScrollingCaptureManager.h"

#include <QDebug>
#include <QGuiApplication>
#include <QApplication>
#include <QScreen>
#include <QCursor>

CaptureManager::CaptureManager(PinWindowManager *pinManager, QObject *parent)
    : QObject(parent)
    , m_regionSelector(nullptr)
    , m_pinManager(pinManager)
    , m_windowDetector(PlatformFeatures::instance().createWindowDetector(this))
    , m_scrollingManager(new ScrollingCaptureManager(pinManager, this))
{
}

CaptureManager::~CaptureManager()
{
}

bool CaptureManager::isActive() const
{
    return (m_regionSelector && m_regionSelector->isVisible()) ||
           (m_scrollingManager && m_scrollingManager->isActive());
}

void CaptureManager::startRegionCapture()
{
    // 如果已經在 capture mode 中，不要重複進入
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

    // 1. 先檢測目標螢幕 (cursor 所在的螢幕)
    QScreen *targetScreen = QGuiApplication::screenAt(QCursor::pos());
    if (!targetScreen) {
        targetScreen = QGuiApplication::primaryScreen();
    }

    qDebug() << "CaptureManager: Target screen:" << targetScreen->name()
             << "geometry:" << targetScreen->geometry()
             << "cursor pos:" << QCursor::pos();

    // 2. Capture screenshot FIRST while popup is still visible
    // This allows capturing context menus (like Snipaste)
    QWidget *popup = QApplication::activePopupWidget();
    qDebug() << "CaptureManager: Pre-capture screenshot, popup active:" << (popup != nullptr);

    QPixmap preCapture = targetScreen->grabWindow(0);
    qDebug() << "CaptureManager: Screenshot captured, size:" << preCapture.size();

    // 3. Close popup AFTER screenshot to avoid event loop conflict with RegionSelector
    if (popup) {
        qDebug() << "CaptureManager: Closing popup to avoid event loop conflict";
        popup->close();
    }

    // 4. 創建 RegionSelector
    m_regionSelector = new RegionSelector();

    // 5. 設置視窗偵測器 (if available on this platform)
    if (m_windowDetector) {
        m_windowDetector->setScreen(targetScreen);
        m_windowDetector->refreshWindowList();
        m_regionSelector->setWindowDetector(m_windowDetector);
    }

    // 6. 初始化指定螢幕 (使用預截圖)
    m_regionSelector->initializeForScreen(targetScreen, preCapture);

    connect(m_regionSelector, &RegionSelector::regionSelected,
            this, &CaptureManager::onRegionSelected);
    connect(m_regionSelector, &RegionSelector::selectionCancelled,
            this, &CaptureManager::onSelectionCancelled);
    connect(m_regionSelector, &RegionSelector::recordingRequested,
            this, &CaptureManager::recordingRequested);
    connect(m_regionSelector, &RegionSelector::scrollingCaptureRequested,
            this, &CaptureManager::scrollingCaptureRequested);

    // 5. 使用 setGeometry + show 取代 showFullScreen，確保在正確螢幕上顯示
    m_regionSelector->setGeometry(targetScreen->geometry());
    m_regionSelector->show();
    raiseWindowAboveMenuBar(m_regionSelector);

    m_regionSelector->activateWindow();
    m_regionSelector->raise();
}

void CaptureManager::onRegionSelected(const QPixmap &screenshot, const QPoint &globalPosition)
{
    qDebug() << "CaptureManager: Region selected at global position:" << globalPosition;
    // m_regionSelector will auto-null via QPointer when WA_DeleteOnClose triggers

    // 直接使用 RegionSelector 計算好的全局座標
    if (m_pinManager) {
        m_pinManager->createPinWindow(screenshot, globalPosition);
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
    if (m_windowDetector) {
        m_windowDetector->setScreen(screen);
        m_windowDetector->refreshWindowList();
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
    connect(m_regionSelector, &RegionSelector::scrollingCaptureRequested,
            this, &CaptureManager::scrollingCaptureRequested);

    m_regionSelector->setGeometry(screen->geometry());
    m_regionSelector->show();
    raiseWindowAboveMenuBar(m_regionSelector);

    m_regionSelector->activateWindow();
    m_regionSelector->raise();
}

void CaptureManager::startScrollingCapture()
{
    // Don't start if already active
    if (isActive()) {
        qDebug() << "CaptureManager: Already in capture mode, ignoring scrolling capture";
        return;
    }

    qDebug() << "CaptureManager: Starting scrolling capture";
    m_scrollingManager->start();
}

void CaptureManager::startScrollingCaptureWithRegion(const QRect &region, QScreen *screen)
{
    // Don't check isActive() here - we're transitioning from RegionSelector which will close
    // Only check if scrolling capture is already running
    if (m_scrollingManager && m_scrollingManager->isActive()) {
        qDebug() << "CaptureManager: Scrolling capture already active, ignoring";
        return;
    }

    qDebug() << "CaptureManager: Starting scrolling capture with region:" << region;
    m_scrollingManager->startWithRegion(region, screen);
}
