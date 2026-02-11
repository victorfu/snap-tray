#ifndef PLATFORMFEATURES_H
#define PLATFORMFEATURES_H

#include <QObject>
#include <QString>
#include <QIcon>
#include <QImage>

class OCRManager;
class WindowDetector;

class PlatformFeatures
{
public:
    static PlatformFeatures& instance();

    bool isOCRAvailable() const;

    OCRManager* createOCRManager(QObject* parent = nullptr) const;
    WindowDetector* createWindowDetector(QObject* parent = nullptr) const;

    QIcon createTrayIcon() const;

    // Copy image to clipboard using native APIs (for CLI mode where Qt clipboard may not persist)
    bool copyImageToClipboard(const QImage& image) const;

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
#endif

private:
    PlatformFeatures();
    ~PlatformFeatures();

    PlatformFeatures(const PlatformFeatures&) = delete;
    PlatformFeatures& operator=(const PlatformFeatures&) = delete;

    bool m_ocrAvailable;
    bool m_windowDetectionAvailable;
};

#endif // PLATFORMFEATURES_H
