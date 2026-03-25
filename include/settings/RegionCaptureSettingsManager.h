#ifndef REGIONCAPTURESETTINGSMANAGER_H
#define REGIONCAPTURESETTINGSMANAGER_H

class RegionCaptureSettingsManager
{
public:
    enum class CursorCompanionStyle {
        None = 0,
        Magnifier = 1,
        Beaver = 2
    };

    static RegionCaptureSettingsManager& instance();

    CursorCompanionStyle cursorCompanionStyle() const;
    void setCursorCompanionStyle(CursorCompanionStyle style);

    bool isMagnifierEnabled() const;
    void setMagnifierEnabled(bool enabled);
    bool isShortcutHintsEnabled() const;
    void setShortcutHintsEnabled(bool enabled);

    static constexpr bool kDefaultMagnifierEnabled = false;
    static constexpr bool kDefaultShortcutHintsEnabled = true;
    static constexpr CursorCompanionStyle kDefaultCursorCompanionStyle =
        CursorCompanionStyle::Beaver;

private:
    RegionCaptureSettingsManager() = default;
    ~RegionCaptureSettingsManager() = default;
    RegionCaptureSettingsManager(const RegionCaptureSettingsManager&) = delete;
    RegionCaptureSettingsManager& operator=(const RegionCaptureSettingsManager&) = delete;

    static constexpr const char* kSettingsKeyCursorCompanionStyle =
        "regionCapture/cursorCompanionStyle";
    static constexpr const char* kSettingsKeyShowMagnifier =
        "regionCapture/showMagnifier";
    static constexpr const char* kSettingsKeyShowShortcutHints =
        "regionCapture/showShortcutHints";
};

#endif // REGIONCAPTURESETTINGSMANAGER_H
