#ifndef PINWINDOWSETTINGSMANAGER_H
#define PINWINDOWSETTINGSMANAGER_H

#include <QtGlobal>

/**
 * @brief Singleton class for managing Pin Window behavior settings.
 *
 * This class provides centralized access to Pin Window settings
 * such as default opacity and adjustment step sizes.
 */
class PinWindowSettingsManager
{
public:
    static PinWindowSettingsManager& instance();

    // Default opacity (0.1 - 1.0)
    qreal loadDefaultOpacity() const;
    void saveDefaultOpacity(qreal opacity);

    // Opacity step (0.01 - 0.20)
    qreal loadOpacityStep() const;
    void saveOpacityStep(qreal step);

    // Zoom step (0.01 - 0.20)
    qreal loadZoomStep() const;
    void saveZoomStep(qreal step);

    // Shadow visibility
    bool loadShadowEnabled() const;
    void saveShadowEnabled(bool enabled);

    // Default values
    static constexpr qreal kDefaultOpacity = 1.0;
    static constexpr qreal kDefaultOpacityStep = 0.05;
    static constexpr qreal kDefaultZoomStep = 0.05;
    static constexpr bool kDefaultShadowEnabled = true;

private:
    PinWindowSettingsManager() = default;
    PinWindowSettingsManager(const PinWindowSettingsManager&) = delete;
    PinWindowSettingsManager& operator=(const PinWindowSettingsManager&) = delete;

    static constexpr const char* kSettingsKeyDefaultOpacity = "pinWindow/defaultOpacity";
    static constexpr const char* kSettingsKeyOpacityStep = "pinWindow/opacityStep";
    static constexpr const char* kSettingsKeyZoomStep = "pinWindow/zoomStep";
    static constexpr const char* kSettingsKeyShadowEnabled = "pinWindow/shadowEnabled";
};

#endif // PINWINDOWSETTINGSMANAGER_H
