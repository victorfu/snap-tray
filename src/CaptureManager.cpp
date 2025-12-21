#include "CaptureManager.h"
#include "RegionSelector.h"
#include "PinWindowManager.h"
#include "platform/WindowLevel.h"
#include "WindowDetector.h"
#include "PlatformFeatures.h"

#include <QDebug>
#include <QGuiApplication>
#include <QScreen>
#include <QCursor>

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

    // 2. 創建 RegionSelector
    m_regionSelector = new RegionSelector();

    // 3. 設置視窗偵測器 (if available on this platform)
    if (m_windowDetector) {
        m_windowDetector->setScreen(targetScreen);
        m_windowDetector->refreshWindowList();
        m_regionSelector->setWindowDetector(m_windowDetector);
    }

    // 4. 初始化指定螢幕 (包含截圖)
    m_regionSelector->initializeForScreen(targetScreen);

    connect(m_regionSelector, &RegionSelector::regionSelected,
            this, &CaptureManager::onRegionSelected);
    connect(m_regionSelector, &RegionSelector::selectionCancelled,
            this, &CaptureManager::onSelectionCancelled);

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
