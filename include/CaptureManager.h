#ifndef CAPTUREMANAGER_H
#define CAPTUREMANAGER_H

#include <QObject>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QPointer>
#include <QString>

#include "capture/QuickCaptureTarget.h"
#include "settings/RegionCaptureSettingsManager.h"

class RegionSelector;
class RegionExportManager;
class QScreen;
class PinWindowManager;
class WindowDetector;

class CaptureManager : public QObject
{
    Q_OBJECT

public:
    explicit CaptureManager(PinWindowManager *pinManager, QObject *parent = nullptr);
    ~CaptureManager();

    bool isActive() const;
    bool hasCompleteSelection() const;
    void triggerFinishSelection();
    void cancelCapture();
    void cycleOrSwitchCaptureScreenByCursor();
    bool startHistoryReplay(const QString& entryId);
    bool isQuickCaptureInProgress() const;

public slots:
    void startRegionCapture(bool showShortcutHintsOnEntry = false);
    void startQuickPinCapture();
    void startRegionCaptureWithPreset(const QRect &region, QScreen *screen);
    // Full-screen / active-window capture that skips region selection.
    void startQuickCapture(snaptray::capture::QuickCaptureTarget target);

signals:
    void captureStarted();
    void captureCompleted(const QPixmap &screenshot);
    void captureCancelled();
    void saveCompleted(const QPixmap &screenshot, const QString &filePath);
    void saveFailed(const QString &filePath, const QString &error);
    void copyCompleted(const QPixmap &screenshot);
    void copyFailed();

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
    void startPresetRegionCapture(QScreen *screen,
                                  const QRect &region,
                                  const QPixmap &preCapture,
                                  const QString &windowTitle,
                                  const QString &ownerApp);
    std::optional<DetectedElement> detectFrontmostWindow(QScreen *screen);
    void exportQuickCapture(QScreen *screen,
                            const QPixmap &snapshot,
                            const snaptray::capture::QuickCaptureRegion &region,
                            RegionCaptureSettingsManager::QuickCaptureAction action);
    void finishQuickCapture();

    QPointer<RegionSelector> m_regionSelector;
    QPointer<RegionExportManager> m_quickCaptureExport;
    PinWindowManager *m_pinManager;
    WindowDetector *m_windowDetector;
};

#endif // CAPTUREMANAGER_H
