#ifndef PLATFORMFEATURES_H
#define PLATFORMFEATURES_H

#include <QObject>
#include <QString>
#include <QIcon>
#include <QImage>
#include <QPixmap>

#include <atomic>
#include <functional>
#include <optional>
#include <QUrl>

#include "platform/PlatformCapabilities.h"

namespace SnapTray { struct ClipboardImageFile; }

class OCRManager;
class QKeySequence;
class QObject;
class WindowDetector;

class PlatformFeatures
{
public:
    enum class ClipboardCopyResult {
        Success,
        Failed,
        Incomplete, // Publication started and any file already offered must remain available.
        Superseded,
    };

    static QString clipboardErrorMessage(ClipboardCopyResult result);

    using ClipboardCopyCompletion = std::function<void(ClipboardCopyResult)>;

    static PlatformFeatures& instance();

    const SnapTray::PlatformCapabilities& capabilities() const;
    bool isOCRAvailable() const;

    OCRManager* createOCRManager(QObject* parent = nullptr) const;
    WindowDetector* createWindowDetector(QObject* parent = nullptr) const;

    // Base tray / menu bar artwork; MainApplication badges it with the recording state.
    QPixmap createTrayPixmap() const;

    // Copy in the configured image/file mode for use after exit. File preparation
    // can fail at the store quota. File-mode failures fill error when supplied.
    bool copyImageToClipboardPersistently(const QImage& image, QString* error = nullptr) const;

    // Copy image to clipboard for interactive GUI flows.
    bool copyImageToClipboardForGui(const QImage& image) const;
    // Clipboard requests are numbered in the order they are made. A copy whose
    // picture is still being rendered off the GUI thread reserves its number
    // when the user asks for it and passes it here once the picture is ready.
    // If the clipboard was asked for or changed meanwhile, by another image
    // copy or any other change (a text copy, another app), it is dropped and
    // completes as Superseded, so a slow copy never overwrites a newer one
    // (#353).
    static quint64 reserveClipboardRequest();
    static bool isLatestClipboardRequest(quint64 request);
    // File-mode preparation can also be superseded after this call reserves its own request.
    void copyImageToClipboardForGuiAsync(
        const QImage& image,
        QObject* context = nullptr,
        ClipboardCopyCompletion completion = {},
        quint64 reservedRequest = 0) const;

    // Prune expired snapshots at startup and before file-mode copies.
    static void cleanClipboardImageFiles();

    // True when the only picture on the clipboard is a file type icon that a
    // file manager put next to copied files (Finder's com.apple.icns), not
    // pixels of the files. Pasting then loads the files instead (#414).
    static bool clipboardImageIsFileIcon();

    // True when the desktop acts on this shortcut before any application can,
    // so it never works as a global hotkey: X11 switches the virtual
    // terminal on Ctrl+Alt+F1 to F12 (#425).
    static bool isShortcutReservedBySystem(const QKeySequence& sequence);

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
    bool copyImageOnlyPersistently(const QImage& image) const;
    ClipboardCopyResult publishClipboardImageFile(const QImage& image,
        SnapTray::ClipboardImageFile& file, bool includePixels, bool persistent) const;
    static quint64 nativeClipboardRevision(); // X11 uses change notifications and returns zero.
    static bool clipboardIsDefinitelyEmpty();
    static std::optional<QList<QUrl>> clipboardFileReferences();
    static inline quint64 s_clipboardRevision = 0;

    PlatformFeatures();
    ~PlatformFeatures();

    PlatformFeatures(const PlatformFeatures&) = delete;
    PlatformFeatures& operator=(const PlatformFeatures&) = delete;

    SnapTray::PlatformCapabilities m_capabilities;
    bool m_ocrAvailable;
    bool m_windowDetectionAvailable;
    // Counts any clipboard change but the image copies written here, which
    // are ordered by their requests instead.
    static void trackClipboardChanges();
    static inline std::atomic<quint64> s_lastClipboardRequest{0};
    static inline bool s_writingImageCopy = false;
};

#endif // PLATFORMFEATURES_H
