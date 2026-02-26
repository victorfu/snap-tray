#ifndef REGIONCAPTURESETTINGSMANAGER_H
#define REGIONCAPTURESETTINGSMANAGER_H

class RegionCaptureSettingsManager
{
public:
    static RegionCaptureSettingsManager& instance();

    bool isShortcutHintsEnabled() const;
    void setShortcutHintsEnabled(bool enabled);

    double loadScrollGoodThreshold() const;
    void saveScrollGoodThreshold(double threshold);

    double loadScrollPartialThreshold() const;
    void saveScrollPartialThreshold(double threshold);

    int loadScrollMinScrollAmount() const;
    void saveScrollMinScrollAmount(int amount);

    int loadScrollMaxFrames() const;
    void saveScrollMaxFrames(int maxFrames);

    int loadScrollMaxOutputPixels() const;
    void saveScrollMaxOutputPixels(int maxOutputPixels);

    bool loadScrollAutoIgnoreBottomEdge() const;
    void saveScrollAutoIgnoreBottomEdge(bool enabled);

    static constexpr bool kDefaultShortcutHintsEnabled = true;
    static constexpr double kDefaultScrollGoodThreshold = 0.86;
    static constexpr double kDefaultScrollPartialThreshold = 0.72;
    static constexpr int kDefaultScrollMinScrollAmount = 12;
    static constexpr int kDefaultScrollMaxFrames = 1200;
    static constexpr int kDefaultScrollMaxOutputPixels = 120000000;
    static constexpr bool kDefaultScrollAutoIgnoreBottomEdge = true;

private:
    RegionCaptureSettingsManager() = default;
    ~RegionCaptureSettingsManager() = default;
    RegionCaptureSettingsManager(const RegionCaptureSettingsManager&) = delete;
    RegionCaptureSettingsManager& operator=(const RegionCaptureSettingsManager&) = delete;

    static constexpr const char* kSettingsKeyShowShortcutHints =
        "regionCapture/showShortcutHints";
    static constexpr const char* kSettingsKeyScrollGoodThreshold =
        "regionCapture/scrollGoodThreshold";
    static constexpr const char* kSettingsKeyScrollPartialThreshold =
        "regionCapture/scrollPartialThreshold";
    static constexpr const char* kSettingsKeyScrollMinScrollAmount =
        "regionCapture/scrollMinScrollAmount";
    static constexpr const char* kSettingsKeyScrollMaxFrames =
        "regionCapture/scrollMaxFrames";
    static constexpr const char* kSettingsKeyScrollMaxOutputPixels =
        "regionCapture/scrollMaxOutputPixels";
    static constexpr const char* kSettingsKeyScrollAutoIgnoreBottomEdge =
        "regionCapture/scrollAutoIgnoreBottomEdge";
};

#endif // REGIONCAPTURESETTINGSMANAGER_H
