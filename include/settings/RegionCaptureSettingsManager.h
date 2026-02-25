#ifndef REGIONCAPTURESETTINGSMANAGER_H
#define REGIONCAPTURESETTINGSMANAGER_H

class RegionCaptureSettingsManager
{
public:
    static RegionCaptureSettingsManager& instance();

    bool isShortcutHintsEnabled() const;
    void setShortcutHintsEnabled(bool enabled);

    static constexpr bool kDefaultShortcutHintsEnabled = true;

private:
    RegionCaptureSettingsManager() = default;
    ~RegionCaptureSettingsManager() = default;
    RegionCaptureSettingsManager(const RegionCaptureSettingsManager&) = delete;
    RegionCaptureSettingsManager& operator=(const RegionCaptureSettingsManager&) = delete;

    static constexpr const char* kSettingsKeyShowShortcutHints =
        "regionCapture/showShortcutHints";
};

#endif // REGIONCAPTURESETTINGSMANAGER_H
