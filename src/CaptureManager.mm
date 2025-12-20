#include "CaptureManager.h"
#include "RegionSelector.h"
#include "PinWindowManager.h"
#ifdef Q_OS_MACOS
#include "WindowDetector.h"
#import <Cocoa/Cocoa.h>
#endif

#include <QDebug>
#include <QGuiApplication>
#include <QScreen>
#include <QCursor>

CaptureManager::CaptureManager(PinWindowManager *pinManager, QObject *parent)
    : QObject(parent)
    , m_regionSelector(nullptr)
    , m_pinManager(pinManager)
#ifdef Q_OS_MACOS
    , m_windowDetector(new WindowDetector(this))
#endif
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

#ifdef Q_OS_MACOS
    // 3. 設置視窗偵測器
    m_windowDetector->setScreen(targetScreen);
    m_windowDetector->refreshWindowList();
    m_regionSelector->setWindowDetector(m_windowDetector);
#endif

    // 4. 初始化指定螢幕 (包含截圖)
    m_regionSelector->initializeForScreen(targetScreen);

    connect(m_regionSelector, &RegionSelector::regionSelected,
            this, &CaptureManager::onRegionSelected);
    connect(m_regionSelector, &RegionSelector::selectionCancelled,
            this, &CaptureManager::onSelectionCancelled);

    // 5. 使用 setGeometry + show 取代 showFullScreen，確保在正確螢幕上顯示
    m_regionSelector->setGeometry(targetScreen->geometry());
    m_regionSelector->show();

#ifdef Q_OS_MACOS
    // 設定視窗層級高於 menu bar (menu bar 在 layer 25)
    NSView* view = reinterpret_cast<NSView*>(m_regionSelector->winId());
    if (view) {
        NSWindow* window = [view window];
        [window setLevel:kCGScreenSaverWindowLevel];  // level 1000
    }
#endif

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
