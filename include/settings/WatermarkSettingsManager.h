#ifndef WATERMARKSETTINGSMANAGER_H
#define WATERMARKSETTINGSMANAGER_H

#include <QString>

/**
 * @brief Singleton class for managing watermark settings.
 *
 * This class provides centralized access to watermark-related settings,
 * following the established SettingsManager pattern.
 */
class WatermarkSettingsManager
{
public:
    /**
     * @brief Watermark position on the image.
     */
    enum Position {
        TopLeft = 0,
        TopRight = 1,
        BottomLeft = 2,
        BottomRight = 3
    };

    /**
     * @brief Watermark settings.
     */
    struct Settings {
        bool enabled = false;
        QString imagePath;
        qreal opacity = 0.5;
        Position position = BottomRight;
        int imageScale = 100;  // 10-200%
        int margin = 12;       // 0-100 pixels
        bool applyToRecording = false;
    };

    static WatermarkSettingsManager& instance();

    // Bulk load/save
    Settings load() const;
    void save(const Settings& settings);

    // Individual accessors
    bool loadEnabled() const;
    void saveEnabled(bool enabled);

    QString loadImagePath() const;
    void saveImagePath(const QString& path);

    qreal loadOpacity() const;
    void saveOpacity(qreal opacity);

    Position loadPosition() const;
    void savePosition(Position position);

    int loadImageScale() const;
    void saveImageScale(int scale);

    int loadMargin() const;
    void saveMargin(int margin);

    bool loadApplyToRecording() const;
    void saveApplyToRecording(bool apply);

    // Default values
    static constexpr bool kDefaultEnabled = false;
    static constexpr qreal kDefaultOpacity = 0.5;
    static constexpr Position kDefaultPosition = BottomRight;
    static constexpr int kDefaultImageScale = 100;
    static constexpr int kDefaultMargin = 12;
    static constexpr bool kDefaultApplyToRecording = false;

private:
    WatermarkSettingsManager() = default;
    WatermarkSettingsManager(const WatermarkSettingsManager&) = delete;
    WatermarkSettingsManager& operator=(const WatermarkSettingsManager&) = delete;

    // Settings keys - preserving existing keys for migration
    static constexpr const char* kKeyEnabled = "watermarkEnabled";
    static constexpr const char* kKeyImagePath = "watermarkImagePath";
    static constexpr const char* kKeyOpacity = "watermarkOpacity";
    static constexpr const char* kKeyPosition = "watermarkPosition";
    static constexpr const char* kKeyImageScale = "watermarkImageScale";
    static constexpr const char* kKeyMargin = "watermarkMargin";
    static constexpr const char* kKeyApplyToRecording = "watermarkApplyToRecording";
};

#endif // WATERMARKSETTINGSMANAGER_H
