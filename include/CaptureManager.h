#ifndef CAPTUREMANAGER_H
#define CAPTUREMANAGER_H

#include <QObject>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QPointer>
#include <QString>

#include <functional>
#include <memory>

#include "settings/RegionCaptureSettingsManager.h"

class RegionSelector;
class RegionExportManager;
namespace snaptray::capture {
class OwnWindowCaptureHider;
}
namespace SnapTray {
class QmlDelayedCaptureIndicator;
}
class QScreen;
class QTimer;
class PinWindowManager;
class WindowDetector;

class CaptureManager : public QObject
{
    Q_OBJECT
    friend class tst_FullScreenCapture;
    friend class tst_DelayedCapture;
    friend class tst_MainApplicationTrayMenu;

public:
    explicit CaptureManager(PinWindowManager *pinManager, QObject *parent = nullptr);
    ~CaptureManager();

    bool isActive() const;
    bool hasCompleteSelection() const;
    void triggerFinishSelection();
    void cancelCapture();
    void cycleOrSwitchCaptureScreenByCursor();
    bool startHistoryReplay(const QString& entryId);
    bool isFullScreenCaptureInProgress() const;

    // Delayed capture: a visible countdown, then a normal region capture of the
    // screen under the cursor. Nothing is grabbed until the countdown ends, so
    // menus and hover states opened meanwhile are what gets captured.
    bool isDelayedCapturePending() const;
    int delayedCaptureRemainingSeconds() const;
    void cancelDelayedCapture();

public slots:
    void startRegionCapture(bool showShortcutHintsOnEntry = false);
    void startDelayedRegionCapture(int delaySeconds, bool showShortcutHintsOnEntry = true);
    void startQuickPinCapture();
    void startRegionCaptureWithPreset(const QRect &region, QScreen *screen);
    // Captures the screen under the cursor without region selection.
    void startFullScreenCapture();

signals:
    void captureStarted();
    void captureCompleted(const QPixmap &screenshot);
    void captureCancelled();
    void saveCompleted(const QPixmap &screenshot, const QString &filePath);
    void saveFailed(const QString &filePath, const QString &error);
    void copyCompleted(const QPixmap &screenshot);
    void copyFailed();
    // The full screen capture no longer owns the screen (grabbed and exported,
    // handed to the editor, failed, or cancelled).
    void fullScreenCaptureFinished();
    // A delayed capture countdown started (pending) or ended (finished or cancelled).
    void delayedCaptureStateChanged(bool pending);
    // Seconds left in the delayed capture countdown; emitted on every tick.
    void delayedCaptureCountdownChanged(int remainingSeconds);

private slots:
    void prewarmWindowDetector();
    void onRegionSelected(const QPixmap &screenshot, const QPoint &globalPosition, const QRect &globalRect);
    void onSelectionCancelled();

private:
    enum class CaptureEntryMode {
        Region,
        QuickPin
    };

    void startCaptureInternal(CaptureEntryMode mode, bool showShortcutHintsOnEntry);
    void initializeRegionSelector(QScreen *targetScreen,
                                  const QPixmap &preCapture,
                                  bool quickPinMode,
                                  bool showShortcutHintsOnEntry);
    void showPreparedRegionSelector(QScreen *targetScreen);
    void refreshWindowDetectorForCapture(QScreen *screen);
    void refreshWindowDetectorAsync(QScreen *screen);
    RegionSelector *createRegionSelector(bool showShortcutHintsOnEntry);
    void startPresetRegionCapture(QScreen *screen, const QRect &region, const QPixmap &preCapture);
    void startFullScreenCaptureOnScreen(QScreen *screen);
    void scheduleFullScreenGrab(QScreen *screen, quint64 generation, int settleRound);
    void captureFullScreenAfterHidingOwnWindows(QScreen *screen);
    void abortPendingFullScreenCapture();
    void exportFullScreenCapture(QScreen *screen,
                                 const QPixmap &snapshot,
                                 RegionCaptureSettingsManager::FullScreenCaptureAction action);
    void finishFullScreenExport();
    QPixmap captureSnapshot(QScreen *screen) const;
    void tickDelayedCapture();
    void finishDelayedCaptureCountdown();
    void clearDelayedCaptureState();

    QPointer<RegionSelector> m_regionSelector;
    QPointer<RegionExportManager> m_fullScreenExport;
    std::unique_ptr<snaptray::capture::OwnWindowCaptureHider> m_ownWindowHider;
    bool m_fullScreenCapturePending = false;
    quint64 m_fullScreenCaptureGeneration = 0;
    std::unique_ptr<SnapTray::QmlDelayedCaptureIndicator> m_delayedCaptureIndicator;
    QTimer* m_delayedCaptureTimer = nullptr;
    bool m_delayedCapturePending = false;
    bool m_delayedCaptureShowShortcutHints = true;
    int m_delayedCaptureRemainingSeconds = 0;
    quint64 m_delayedCaptureGeneration = 0;
    // Test hook: shorten the one-second countdown tick.
    int m_delayedCaptureTickIntervalMs = 1000;
    // Test hooks: replace the platform screen grab and clipboard write.
    std::function<QPixmap(QScreen*)> m_snapshotOverride;
    std::function<void(const QImage&, std::function<void(bool)>)> m_clipboardWriterOverride;
    PinWindowManager *m_pinManager;
    WindowDetector *m_windowDetector;
};

#endif // CAPTUREMANAGER_H
