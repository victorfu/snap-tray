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

    // What the full-screen and active-window capture actions do with the image.
    enum class QuickCaptureAction {
        CopyToClipboard = 0,
        Save = 1,
        Edit = 2
    };

    static RegionCaptureSettingsManager& instance();

    CursorCompanionStyle cursorCompanionStyle() const;
    void setCursorCompanionStyle(CursorCompanionStyle style);

    bool isMagnifierEnabled() const;
    void setMagnifierEnabled(bool enabled);
    bool isShortcutHintsEnabled() const;
    void setShortcutHintsEnabled(bool enabled);
    QuickCaptureAction quickCaptureAction() const;
    void setQuickCaptureAction(QuickCaptureAction action);

    static constexpr bool kDefaultShortcutHintsEnabled = true;
    static constexpr CursorCompanionStyle kDefaultCursorCompanionStyle =
        CursorCompanionStyle::Beaver;
    static constexpr QuickCaptureAction kDefaultQuickCaptureAction =
        QuickCaptureAction::CopyToClipboard;

private:
    RegionCaptureSettingsManager() = default;
    ~RegionCaptureSettingsManager() = default;
    RegionCaptureSettingsManager(const RegionCaptureSettingsManager&) = delete;
    RegionCaptureSettingsManager& operator=(const RegionCaptureSettingsManager&) = delete;

    static constexpr const char* kSettingsKeyCursorCompanionStyle =
        "regionCapture/cursorCompanionStyle";
    static constexpr const char* kSettingsKeyShowShortcutHints =
        "regionCapture/showShortcutHints";
    static constexpr const char* kSettingsKeyQuickCaptureAction =
        "regionCapture/quickCaptureAction";
};

#endif // REGIONCAPTURESETTINGSMANAGER_H
