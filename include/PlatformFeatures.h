#ifndef PLATFORMFEATURES_H
#define PLATFORMFEATURES_H

#include <QObject>
#include <QString>
#include <QIcon>
#include <QImage>

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

    QIcon createTrayIcon() const;

    // Copy image to clipboard using native APIs for flows that must survive process exit,
    // such as the CLI capture command.
    bool copyImageToClipboardPersistently(const QImage& image) const;

    // Copy image to clipboard for interactive GUI flows.
    bool copyImageToClipboardForGui(const QImage& image) const;
    void copyImageToClipboardForGuiAsync(
        const QImage& image,
        QObject* context = nullptr,
        ClipboardCopyCompletion completion = {}) const;

    // CLI installation
    bool isCLIInstalled() const;
    bool installCLI() const;
    bool uninstallCLI() const;
    QString getAppExecutablePath() const;

#ifdef Q_OS_MAC
    // macOS permission management
    static bool hasScreenRecordingPermission();
    static bool hasAccessibilityPermission();
    static void openScreenRecordingSettings();
    static void openAccessibilitySettings();

    /// Bring the application to the foreground (activateIgnoringOtherApps).
    static void activateApp();

    /// Temporarily show the app in the Dock (NSApplicationActivationPolicyRegular).
    /// Use when a persistent window (e.g., Settings) is open so it survives
    /// app-deactivation in LSUIElement mode.
    static void setActivationPolicyRegular();

    /// Revert to accessory / LSUIElement mode (no Dock icon).
    static void setActivationPolicyAccessory();
#endif

private:
    PlatformFeatures();
    ~PlatformFeatures();

    PlatformFeatures(const PlatformFeatures&) = delete;
    PlatformFeatures& operator=(const PlatformFeatures&) = delete;

    SnapTray::PlatformCapabilities m_capabilities;
    bool m_ocrAvailable;
    bool m_windowDetectionAvailable;
};

#endif // PLATFORMFEATURES_H
