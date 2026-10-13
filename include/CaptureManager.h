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
class QScreen;
class PinWindowManager;
class WindowDetector;

class CaptureManager : public QObject
{
    Q_OBJECT
    friend class tst_FullScreenCapture;
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

public slots:
    void startRegionCapture(bool showShortcutHintsOnEntry = false);
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

    QPointer<RegionSelector> m_regionSelector;
    QPointer<RegionExportManager> m_fullScreenExport;
    std::unique_ptr<snaptray::capture::OwnWindowCaptureHider> m_ownWindowHider;
    bool m_fullScreenCapturePending = false;
    quint64 m_fullScreenCaptureGeneration = 0;
    // Test hooks: replace the platform screen grab and clipboard write.
    std::function<QPixmap(QScreen*)> m_snapshotOverride;
    std::function<void(const QImage&, std::function<void(bool)>)> m_clipboardWriterOverride;
    PinWindowManager *m_pinManager;
    WindowDetector *m_windowDetector;
};

#endif // CAPTUREMANAGER_H
