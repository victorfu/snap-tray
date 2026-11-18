#ifndef PLATFORMFEATURES_H
#define PLATFORMFEATURES_H

#include <QObject>
#include <QString>
#include <QIcon>
#include <QImage>
#include <QPixmap>

#include <atomic>
#include <functional>

#include "platform/PlatformCapabilities.h"

class OCRManager;
class QObject;
class WindowDetector;

class PlatformFeatures
{
public:
    enum class ClipboardCopyResult {
        Success,
        Failed,
        Superseded,
    };

    using ClipboardCopyCompletion = std::function<void(ClipboardCopyResult)>;

    static PlatformFeatures& instance();

    const SnapTray::PlatformCapabilities& capabilities() const;
    bool isOCRAvailable() const;

    OCRManager* createOCRManager(QObject* parent = nullptr) const;
    WindowDetector* createWindowDetector(QObject* parent = nullptr) const;

    // Base tray / menu bar artwork; MainApplication badges it with the recording state.
    QPixmap createTrayPixmap() const;

    // Copy image to clipboard using native APIs for flows that must survive process exit,
    // such as the CLI capture command.
    bool copyImageToClipboardPersistently(const QImage& image) const;

    // Copy image to clipboard for interactive GUI flows.
    bool copyImageToClipboardForGui(const QImage& image) const;
    // Image copies are numbered in the order they are requested. A copy whose
    // picture is still being rendered off the GUI thread reserves its number
    // when the user asks for it and passes it here once the picture is ready;
    // if another copy was requested meanwhile it is dropped and completes as
    // Superseded, so a slow copy never overwrites a newer one (#353).
    static quint64 reserveClipboardRequest() { return ++s_lastClipboardRequest; }
    static bool isLatestClipboardRequest(quint64 request)
    {
        return request == s_lastClipboardRequest.load();
    }
    void copyImageToClipboardForGuiAsync(
        const QImage& image,
        QObject* context = nullptr,
        ClipboardCopyCompletion completion = {},
        quint64 reservedRequest = 0) const;

    // CLI installation
    bool isCLIInstalled() const;
    bool installCLI() const;
    bool uninstallCLI() const;
    QString getAppExecutablePath() const;

#ifdef Q_OS_MACOS
    // macOS permission management
    static bool hasScreenRecordingPermission();
    static bool hasAccessibilityPermission();
    static void openScreenRecordingSettings();
    static void openAccessibilitySettings();

    /// Bring the application to the foreground (activateIgnoringOtherApps).
    static void activateApp();
#endif

private:
    PlatformFeatures();
    ~PlatformFeatures();

    PlatformFeatures(const PlatformFeatures&) = delete;
    PlatformFeatures& operator=(const PlatformFeatures&) = delete;

    SnapTray::PlatformCapabilities m_capabilities;
    bool m_ocrAvailable;
    bool m_windowDetectionAvailable;
    static inline std::atomic<quint64> s_lastClipboardRequest{0};
};

#endif // PLATFORMFEATURES_H
