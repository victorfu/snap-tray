#ifndef REGIONCAPTURESETTINGSMANAGER_H
#define REGIONCAPTURESETTINGSMANAGER_H

class RegionCaptureSettingsManager
{
public:
    enum class ScrollAutoStartMode {
        ManualFirst = 0,
        AutoProbe = 1
    };

    static RegionCaptureSettingsManager& instance();

    bool isShortcutHintsEnabled() const;
    void setShortcutHintsEnabled(bool enabled);

    bool isScrollAutomationEnabled() const;
    void setScrollAutomationEnabled(bool enabled);

    double loadScrollGoodThreshold() const;
    void saveScrollGoodThreshold(double threshold);

    double loadScrollPartialThreshold() const;
    void saveScrollPartialThreshold(double threshold);

    int loadScrollMinScrollAmount() const;
    void saveScrollMinScrollAmount(int amount);

    int loadScrollAutoStepDelayMs() const;
    void saveScrollAutoStepDelayMs(int delayMs);

    int loadScrollMaxFrames() const;
    void saveScrollMaxFrames(int maxFrames);

    int loadScrollMaxOutputPixels() const;
    void saveScrollMaxOutputPixels(int maxOutputPixels);

    bool loadScrollAutoIgnoreBottomEdge() const;
    void saveScrollAutoIgnoreBottomEdge(bool enabled);

    int loadScrollSettleStableFrames() const;
    void saveScrollSettleStableFrames(int frames);

    int loadScrollSettleTimeoutMs() const;
    void saveScrollSettleTimeoutMs(int timeoutMs);

    int loadScrollProbeGridDensity() const;
    void saveScrollProbeGridDensity(int density);

    ScrollAutoStartMode loadScrollAutoStartMode() const;
    void saveScrollAutoStartMode(ScrollAutoStartMode mode);

    bool loadScrollInlinePreviewCollapsed() const;
    void saveScrollInlinePreviewCollapsed(bool collapsed);

    static constexpr bool kDefaultShortcutHintsEnabled = true;
    static constexpr bool kDefaultScrollAutomationEnabled = true;
    static constexpr double kDefaultScrollGoodThreshold = 0.86;
    static constexpr double kDefaultScrollPartialThreshold = 0.72;
    static constexpr int kDefaultScrollMinScrollAmount = 12;
    static constexpr int kDefaultScrollAutoStepDelayMs = 140;
    static constexpr int kDefaultScrollMaxFrames = 300;
    static constexpr int kDefaultScrollMaxOutputPixels = 120000000;
    static constexpr bool kDefaultScrollAutoIgnoreBottomEdge = true;
    static constexpr int kDefaultScrollSettleStableFrames = 2;
    static constexpr int kDefaultScrollSettleTimeoutMs = 220;
    static constexpr int kDefaultScrollProbeGridDensity = 3;
    static constexpr ScrollAutoStartMode kDefaultScrollAutoStartMode =
        ScrollAutoStartMode::ManualFirst;
    static constexpr bool kDefaultScrollInlinePreviewCollapsed = true;

private:
    RegionCaptureSettingsManager() = default;
    ~RegionCaptureSettingsManager() = default;
    RegionCaptureSettingsManager(const RegionCaptureSettingsManager&) = delete;
    RegionCaptureSettingsManager& operator=(const RegionCaptureSettingsManager&) = delete;

    static constexpr const char* kSettingsKeyShowShortcutHints =
        "regionCapture/showShortcutHints";
    static constexpr const char* kSettingsKeyScrollAutomationEnabled =
        "regionCapture/scrollAutomationEnabled";
    static constexpr const char* kSettingsKeyScrollGoodThreshold =
        "regionCapture/scrollGoodThreshold";
    static constexpr const char* kSettingsKeyScrollPartialThreshold =
        "regionCapture/scrollPartialThreshold";
    static constexpr const char* kSettingsKeyScrollMinScrollAmount =
        "regionCapture/scrollMinScrollAmount";
    static constexpr const char* kSettingsKeyScrollAutoStepDelayMs =
        "regionCapture/scrollAutoStepDelayMs";
    static constexpr const char* kSettingsKeyScrollMaxFrames =
        "regionCapture/scrollMaxFrames";
    static constexpr const char* kSettingsKeyScrollMaxOutputPixels =
        "regionCapture/scrollMaxOutputPixels";
    static constexpr const char* kSettingsKeyScrollAutoIgnoreBottomEdge =
        "regionCapture/scrollAutoIgnoreBottomEdge";
    static constexpr const char* kSettingsKeyScrollSettleStableFrames =
        "regionCapture/scrollSettleStableFrames";
    static constexpr const char* kSettingsKeyScrollSettleTimeoutMs =
        "regionCapture/scrollSettleTimeoutMs";
    static constexpr const char* kSettingsKeyScrollProbeGridDensity =
        "regionCapture/scrollProbeGridDensity";
    static constexpr const char* kSettingsKeyScrollAutoStartMode =
        "regionCapture/scrollAutoStartMode";
    static constexpr const char* kSettingsKeyScrollInlinePreviewCollapsed =
        "regionCapture/scrollInlinePreviewCollapsed";
};

#endif // REGIONCAPTURESETTINGSMANAGER_H
