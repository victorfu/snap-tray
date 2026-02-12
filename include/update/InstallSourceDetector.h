#ifndef INSTALLSOURCEDETECTOR_H
#define INSTALLSOURCEDETECTOR_H

#include <QString>

/**
 * @brief Enum representing the installation source of the application.
 */
enum class InstallSource {
    MicrosoftStore,   // Windows Store MSIX
    MacAppStore,      // macOS App Store (future)
    DirectDownload,   // .exe / .dmg direct download
    Homebrew,         // macOS Homebrew (future)
    Development,      // Debug build
    Unknown
};

/**
 * @brief Utility class for detecting the installation source of the application.
 *
 * This class provides static methods to detect how the application was installed,
 * which determines the appropriate update strategy.
 */
class InstallSourceDetector
{
public:
    /**
     * @brief Detect the installation source of the application.
     * @return The detected InstallSource.
     */
    static InstallSource detect();

    /**
     * @brief Check if the application was installed from a store (Microsoft Store or Mac App Store).
     * @return true if installed from a store, false otherwise.
     */
    static bool isStoreInstall();

    /**
     * @brief Get the display name of the store (e.g., "Microsoft Store", "Mac App Store").
     * @return The store name, or empty string if not a store install.
     */
    static QString getStoreName();

    /**
     * @brief Get a human-readable display name for the given install source.
     * @param source The InstallSource enum value.
     * @return A display-friendly string representation.
     */
    static QString getSourceDisplayName(InstallSource source);

    /**
     * @brief Check if this is a development/debug build.
     * @return true if running in debug mode, false otherwise.
     */
    static bool isDevelopmentBuild();

private:
    InstallSourceDetector() = default;

    // Cached detection result
    static InstallSource s_cachedSource;
    static bool s_detected;
};

#endif // INSTALLSOURCEDETECTOR_H
