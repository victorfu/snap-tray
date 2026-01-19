#ifndef AUTOBLURSETTINGSMANAGER_H
#define AUTOBLURSETTINGSMANAGER_H

/**
 * @brief Singleton class for managing auto-blur detection settings.
 *
 * This class provides centralized access to auto-blur detection settings,
 * following the established SettingsManager pattern.
 */
class AutoBlurSettingsManager
{
public:
    /**
     * @brief Blur effect type.
     */
    enum class BlurType {
        Gaussian,
        Pixelate
    };

    /**
     * @brief Detection options.
     */
    struct Options {
        bool enabled = true;
        bool detectFaces = true;
        int blurIntensity = 50;  // 1-100
        BlurType blurType = BlurType::Pixelate;
    };

    static AutoBlurSettingsManager& instance();

    // Bulk load/save
    Options load() const;
    void save(const Options& options);

    // Individual accessors
    bool loadEnabled() const;
    void saveEnabled(bool enabled);

    bool loadDetectFaces() const;
    void saveDetectFaces(bool detect);

    int loadBlurIntensity() const;
    void saveBlurIntensity(int intensity);

    BlurType loadBlurType() const;
    void saveBlurType(BlurType type);

    // Default values
    static constexpr bool kDefaultEnabled = true;
    static constexpr bool kDefaultDetectFaces = true;
    static constexpr int kDefaultBlurIntensity = 50;
    static constexpr BlurType kDefaultBlurType = BlurType::Pixelate;

private:
    AutoBlurSettingsManager() = default;
    AutoBlurSettingsManager(const AutoBlurSettingsManager&) = delete;
    AutoBlurSettingsManager& operator=(const AutoBlurSettingsManager&) = delete;

    // Settings keys - preserving existing keys for migration
    static constexpr const char* kKeyEnabled = "detection/autoBlurEnabled";
    static constexpr const char* kKeyDetectFaces = "detection/detectFaces";
    static constexpr const char* kKeyBlurIntensity = "detection/blurIntensity";
    static constexpr const char* kKeyBlurType = "detection/blurType";
};

#endif // AUTOBLURSETTINGSMANAGER_H
