#ifndef REGIONCAPTURESETTINGSMANAGER_H
#define REGIONCAPTURESETTINGSMANAGER_H

#include <QRect>
#include <QString>

#include <array>
#include <optional>

class RegionCaptureSettingsManager
{
public:
    enum class CursorCompanionStyle {
        None = 0,
        Magnifier = 1,
        Beaver = 2
    };

    // What Full Screen Capture does with the image.
    enum class FullScreenCaptureAction {
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
    FullScreenCaptureAction fullScreenCaptureAction() const;
    void setFullScreenCaptureAction(FullScreenCaptureAction action);

    // Countdown length used by Delayed Capture (tray submenu and hotkey).
    // The tray offers kDelayedCapturePresetSeconds; the last choice is remembered
    // here so the hotkey reuses it.
    int delayedCaptureSeconds() const;
    void setDelayedCaptureSeconds(int seconds);
    static bool isValidDelayedCaptureSeconds(int seconds);

    // The selection of the last completed region capture, in global logical
    // coordinates, plus the name of the screen it was taken on. `snaptray gui
    // --last-region` starts from it.
    struct LastCaptureRegion {
        QRect globalRect;
        QString screenName;
    };
    std::optional<LastCaptureRegion> lastCaptureRegion() const;
    void setLastCaptureRegion(const QRect& globalRect, const QString& screenName);

    static constexpr bool kDefaultShortcutHintsEnabled = true;
    static constexpr CursorCompanionStyle kDefaultCursorCompanionStyle =
        CursorCompanionStyle::Beaver;
    static constexpr FullScreenCaptureAction kDefaultFullScreenCaptureAction =
        FullScreenCaptureAction::CopyToClipboard;
    static constexpr std::array<int, 3> kDelayedCapturePresetSeconds = {3, 5, 10};
    static constexpr int kDefaultDelayedCaptureSeconds = 3;
    static constexpr int kMinDelayedCaptureSeconds = 1;
    static constexpr int kMaxDelayedCaptureSeconds = 60;

private:
    RegionCaptureSettingsManager() = default;
    ~RegionCaptureSettingsManager() = default;
    RegionCaptureSettingsManager(const RegionCaptureSettingsManager&) = delete;
    RegionCaptureSettingsManager& operator=(const RegionCaptureSettingsManager&) = delete;

    static constexpr const char* kSettingsKeyCursorCompanionStyle =
        "regionCapture/cursorCompanionStyle";
    static constexpr const char* kSettingsKeyShowShortcutHints =
        "regionCapture/showShortcutHints";
    static constexpr const char* kSettingsKeyFullScreenCaptureAction =
        "regionCapture/fullScreenCaptureAction";
    static constexpr const char* kSettingsKeyDelayedCaptureSeconds =
        "regionCapture/delayedCaptureSeconds";
    static constexpr const char* kSettingsKeyLastCaptureRegion =
        "regionCapture/lastRegion";
    static constexpr const char* kSettingsKeyLastCaptureScreen =
        "regionCapture/lastRegionScreen";
};

#endif // REGIONCAPTURESETTINGSMANAGER_H
