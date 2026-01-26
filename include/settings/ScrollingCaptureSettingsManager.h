#ifndef SCROLLINGCAPTURESETTINGSMANAGER_H
#define SCROLLINGCAPTURESETTINGSMANAGER_H

#include "scrolling/ScrollingCaptureOptions.h"

/**
 * @brief Singleton class for managing scrolling capture settings
 *
 * Follows the FileSettingsManager pattern for consistency.
 */
class ScrollingCaptureSettingsManager
{
public:
    static ScrollingCaptureSettingsManager& instance();

    // Load all options
    ScrollingCaptureOptions loadOptions() const;
    void saveOptions(const ScrollingCaptureOptions& options);

    // Individual settings
    ScrollMethod loadScrollMethod() const;
    void saveScrollMethod(ScrollMethod method);

    int loadScrollDelayMs() const;
    void saveScrollDelayMs(int delayMs);

    int loadScrollAmount() const;
    void saveScrollAmount(int amount);

    bool loadAutoScrollTop() const;
    void saveAutoScrollTop(bool enabled);

    bool loadAutoIgnoreBottomEdge() const;
    void saveAutoIgnoreBottomEdge(bool enabled);

    int loadMaxFrames() const;
    void saveMaxFrames(int maxFrames);

    int loadMaxHeightPx() const;
    void saveMaxHeightPx(int maxHeight);

    int loadMinMatchLines() const;
    void saveMinMatchLines(int minLines);

    int loadStartDelayMs() const;
    void saveStartDelayMs(int delayMs);

    // Default values
    static ScrollingCaptureOptions defaultOptions();

private:
    ScrollingCaptureSettingsManager() = default;
    ScrollingCaptureSettingsManager(const ScrollingCaptureSettingsManager&) = delete;
    ScrollingCaptureSettingsManager& operator=(const ScrollingCaptureSettingsManager&) = delete;

    static constexpr const char* kKeyScrollMethod = "scrollingCapture/scrollMethod";
    static constexpr const char* kKeyScrollDelay = "scrollingCapture/scrollDelayMs";
    static constexpr const char* kKeyScrollAmount = "scrollingCapture/scrollAmount";
    static constexpr const char* kKeyAutoScrollTop = "scrollingCapture/autoScrollTop";
    static constexpr const char* kKeyAutoIgnoreBottom = "scrollingCapture/autoIgnoreBottom";
    static constexpr const char* kKeyMaxFrames = "scrollingCapture/maxFrames";
    static constexpr const char* kKeyMaxHeight = "scrollingCapture/maxHeightPx";
    static constexpr const char* kKeyMinMatchLines = "scrollingCapture/minMatchLines";
    static constexpr const char* kKeyStartDelay = "scrollingCapture/startDelayMs";
};

#endif // SCROLLINGCAPTURESETTINGSMANAGER_H
