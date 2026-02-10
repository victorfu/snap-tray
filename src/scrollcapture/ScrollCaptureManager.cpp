#include "scrollcapture/ScrollCaptureManager.h"

#include "PinWindowManager.h"
#include "RegionSelector.h"
#include "ScrollCaptureControlBar.h"
#include "PlatformFeatures.h"
#include "platform/WindowLevel.h"
#include "scrollcapture/ScrollCaptureSession.h"

#include <QApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QScreen>

ScrollCaptureManager::ScrollCaptureManager(PinWindowManager* pinManager, QObject* parent)
    : QObject(parent)
    , m_pinManager(pinManager)
    , m_windowDetector(PlatformFeatures::instance().createWindowDetector(nullptr))
{
}

ScrollCaptureManager::~ScrollCaptureManager()
{
    cleanupSession();
    cleanupPicker();
}

bool ScrollCaptureManager::isActive() const
{
    return m_active;
}

void ScrollCaptureManager::beginWindowPick()
{
    if (m_active) {
        return;
    }

    m_active = true;
    m_waitingPreset = false;
    emit captureStarted();

    QScreen* targetScreen = QGuiApplication::screenAt(QCursor::pos());
    if (!targetScreen) {
        targetScreen = QGuiApplication::primaryScreen();
    }
    if (!targetScreen) {
        cleanupSession();
        cleanupPicker();
        m_active = false;
        emit captureCompleted(false);
        return;
    }

    cleanupPicker();
    m_windowPicker = new RegionSelector();
    m_windowPicker->setInteractionMode(RegionSelector::InteractionMode::ScrollWindowPick);

    if (m_windowDetector) {
        m_windowDetector->setScreen(targetScreen);
        m_windowDetector->refreshWindowListAsync();
        m_windowPicker->setWindowDetector(m_windowDetector.get());

        const auto picker = m_windowPicker;
        const auto triggerDetection = [picker]() {
            if (picker) {
                picker->refreshWindowDetectionAtCursor();
            }
        };
        if (m_windowDetector->isRefreshComplete()) {
            triggerDetection();
        } else {
            connect(m_windowDetector.get(), &WindowDetector::windowListReady,
                    m_windowPicker, [triggerDetection]() {
                        triggerDetection();
                    });
        }
    }

    connect(m_windowPicker, &RegionSelector::windowChosen,
            this, &ScrollCaptureManager::onWindowChosen);
    connect(m_windowPicker, &RegionSelector::selectionCancelled,
            this, &ScrollCaptureManager::onPickerCancelled);

    QPixmap preCapture = targetScreen->grabWindow(0);
    m_windowPicker->initializeForScreen(targetScreen, preCapture);
    m_windowPicker->setGeometry(targetScreen->geometry());
    m_windowPicker->show();
    raiseWindowAboveMenuBar(m_windowPicker);
    m_windowPicker->activateWindow();
    m_windowPicker->raise();
    m_windowPicker->ensureCrossCursor();
}

void ScrollCaptureManager::stop()
{
    if (m_session) {
        m_session->stop();
    }
}

void ScrollCaptureManager::cancel()
{
    if (m_waitingPreset) {
        cleanupSession();
        cleanupPicker();
        m_waitingPreset = false;
        m_active = false;
        emit captureCompleted(false);
        return;
    }

    if (m_session) {
        m_session->cancel();
    } else {
        onPickerCancelled();
    }
}

void ScrollCaptureManager::onWindowChosen(const DetectedElement& element)
{
    if (!element.bounds.isValid()) {
        onPickerCancelled();
        return;
    }

    m_selectedWindow = element;
    cleanupPicker();

    if (!m_controlBar) {
        m_controlBar = new ScrollCaptureControlBar();
        connect(m_controlBar, &ScrollCaptureControlBar::presetWebRequested,
                this, &ScrollCaptureManager::onPresetWebRequested);
        connect(m_controlBar, &ScrollCaptureControlBar::presetChatRequested,
                this, &ScrollCaptureManager::onPresetChatRequested);
        connect(m_controlBar, &ScrollCaptureControlBar::stopRequested,
                this, &ScrollCaptureManager::onStopRequested);
        connect(m_controlBar, &ScrollCaptureControlBar::cancelRequested,
                this, &ScrollCaptureManager::onCancelRequested);
        connect(m_controlBar, &ScrollCaptureControlBar::openSettingsRequested,
                this, &ScrollCaptureManager::onOpenSettingsRequested);
    }

    m_waitingPreset = true;
    m_controlBar->setMode(ScrollCaptureControlBar::Mode::PresetSelection);
    m_controlBar->setStatusText(tr("Choose mode"));
    m_controlBar->setProgress(0, 0);
    m_controlBar->setOpenSettingsVisible(false);
    m_controlBar->show();
    m_controlBar->positionNear(m_selectedWindow.bounds);
}

void ScrollCaptureManager::onPickerCancelled()
{
    cleanupPicker();
    cleanupSession();
    m_waitingPreset = false;
    m_active = false;
    emit captureCompleted(false);
}

void ScrollCaptureManager::onPresetWebRequested()
{
    startSession(ScrollCapturePreset::WebPage);
}

void ScrollCaptureManager::onPresetChatRequested()
{
    startSession(ScrollCapturePreset::ChatHistory);
}

void ScrollCaptureManager::onStopRequested()
{
    if (m_controlBar) {
        m_controlBar->setStatusText(tr("Stopping..."));
    }
    stop();
}

void ScrollCaptureManager::onCancelRequested()
{
    cancel();
}

void ScrollCaptureManager::onOpenSettingsRequested()
{
#ifdef Q_OS_MAC
    PlatformFeatures::openAccessibilitySettings();
#endif
}

void ScrollCaptureManager::onSessionStateChanged(ScrollCaptureState state)
{
    if (!m_controlBar) {
        return;
    }

    switch (state) {
    case ScrollCaptureState::Arming:
        m_controlBar->setStatusText(tr("Preparing..."));
        break;
    case ScrollCaptureState::Capturing:
        m_controlBar->setStatusText(tr("Capturing..."));
        break;
    case ScrollCaptureState::Stitching:
        m_controlBar->setStatusText(tr("Stitching..."));
        break;
    case ScrollCaptureState::Completed:
        m_controlBar->setStatusText(tr("Completed"));
        break;
    case ScrollCaptureState::Cancelled:
        m_controlBar->setStatusText(tr("Cancelled"));
        break;
    case ScrollCaptureState::Error:
        m_controlBar->setStatusText(tr("Error"));
        break;
    case ScrollCaptureState::PickingWindow:
    case ScrollCaptureState::Idle:
        break;
    }
}

void ScrollCaptureManager::onSessionProgress(int frameCount, int stitchedHeight, const QString& statusText)
{
    if (!m_controlBar) {
        return;
    }
    if (!statusText.isEmpty()) {
        m_controlBar->setStatusText(statusText);
    }
    m_controlBar->setProgress(frameCount, stitchedHeight);
}

void ScrollCaptureManager::onSessionWarning(const QString& warning)
{
    if (!m_controlBar || warning.isEmpty()) {
        return;
    }
    m_controlBar->setStatusText(warning);
}

void ScrollCaptureManager::onSessionFinished(const ScrollCaptureResult& result)
{
    const bool success = result.success && !result.pixmap.isNull();
    const bool needsAccessibilityRecovery =
        !success && result.reason.contains(QStringLiteral("Accessibility"), Qt::CaseInsensitive);

    if (needsAccessibilityRecovery && m_controlBar) {
        if (m_session) {
            m_session->disconnect(this);
            m_session.reset();
        }

        m_controlBar->setMode(ScrollCaptureControlBar::Mode::Error);
        m_controlBar->setStatusText(result.reason);
        m_controlBar->setOpenSettingsVisible(true);
        m_controlBar->setProgress(0, 0);
        return;
    }

    if (success && m_pinManager) {
        const QPoint position = centerPositionForPixmap(result.pixmap);
        m_pinManager->createPinWindow(result.pixmap, position);
    }

    cleanupSession();
    cleanupPicker();
    m_waitingPreset = false;
    m_active = false;
    emit captureCompleted(success);
}

void ScrollCaptureManager::cleanupPicker()
{
    if (m_windowPicker) {
        m_windowPicker->close();
        m_windowPicker = nullptr;
    }
}

void ScrollCaptureManager::cleanupSession()
{
    if (m_session) {
        m_session->disconnect(this);
        m_session.reset();
    }
    if (m_controlBar) {
        setWindowExcludedFromCapture(m_controlBar, false);
        m_controlBar->close();
        m_controlBar = nullptr;
    }
}

void ScrollCaptureManager::startSession(ScrollCapturePreset preset)
{
    if (!m_waitingPreset || !m_selectedWindow.bounds.isValid()) {
        return;
    }

    cleanupSession();
    m_waitingPreset = false;

    m_controlBar = new ScrollCaptureControlBar();
    connect(m_controlBar, &ScrollCaptureControlBar::stopRequested,
            this, &ScrollCaptureManager::onStopRequested);
    connect(m_controlBar, &ScrollCaptureControlBar::cancelRequested,
            this, &ScrollCaptureManager::onCancelRequested);
    connect(m_controlBar, &ScrollCaptureControlBar::openSettingsRequested,
            this, &ScrollCaptureManager::onOpenSettingsRequested);
    m_controlBar->setMode(ScrollCaptureControlBar::Mode::Capturing);
    m_controlBar->setStatusText(tr("Preparing..."));
    m_controlBar->setOpenSettingsVisible(false);
    m_controlBar->show();
    m_controlBar->positionNear(m_selectedWindow.bounds);
    setWindowExcludedFromCapture(m_controlBar, true);

    m_session = std::make_unique<ScrollCaptureSession>();
    connect(m_session.get(), &ScrollCaptureSession::stateChanged,
            this, &ScrollCaptureManager::onSessionStateChanged);
    connect(m_session.get(), &ScrollCaptureSession::progressChanged,
            this, &ScrollCaptureManager::onSessionProgress);
    connect(m_session.get(), &ScrollCaptureSession::warningRaised,
            this, &ScrollCaptureManager::onSessionWarning);
    connect(m_session.get(), &ScrollCaptureSession::finished,
            this, &ScrollCaptureManager::onSessionFinished);

    QList<WId> excludedWindowIds;
    if (m_controlBar) {
        excludedWindowIds.push_back(m_controlBar->winId());
    }
    m_session->setExcludedWindows(excludedWindowIds);
    m_session->configure(m_selectedWindow, preset);
    m_session->start();
}

QPoint ScrollCaptureManager::centerPositionForPixmap(const QPixmap& pixmap) const
{
    QScreen* targetScreen = QGuiApplication::screenAt(m_selectedWindow.bounds.center());
    if (!targetScreen) {
        targetScreen = QGuiApplication::primaryScreen();
    }
    if (!targetScreen) {
        return QPoint(100, 100);
    }

    const QRect screenGeometry = targetScreen->availableGeometry();
    const QSize logicalSize = pixmap.size() / pixmap.devicePixelRatio();
    return QPoint(
        screenGeometry.center().x() - logicalSize.width() / 2,
        screenGeometry.center().y() - logicalSize.height() / 2
    );
}
