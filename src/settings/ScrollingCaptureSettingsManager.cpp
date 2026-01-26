#include "settings/ScrollingCaptureSettingsManager.h"
#include "settings/Settings.h"

ScrollingCaptureSettingsManager& ScrollingCaptureSettingsManager::instance()
{
    static ScrollingCaptureSettingsManager instance;
    return instance;
}

ScrollingCaptureOptions ScrollingCaptureSettingsManager::defaultOptions()
{
    return ScrollingCaptureOptions();  // Uses struct defaults
}

ScrollingCaptureOptions ScrollingCaptureSettingsManager::loadOptions() const
{
    ScrollingCaptureOptions options;
    options.scrollMethod = loadScrollMethod();
    options.scrollDelayMs = loadScrollDelayMs();
    options.scrollAmount = loadScrollAmount();
    options.autoScrollTop = loadAutoScrollTop();
    options.autoIgnoreBottomEdge = loadAutoIgnoreBottomEdge();
    options.maxFrames = loadMaxFrames();
    options.maxHeightPx = loadMaxHeightPx();
    options.minMatchLines = loadMinMatchLines();
    options.startDelayMs = loadStartDelayMs();
    return options;
}

void ScrollingCaptureSettingsManager::saveOptions(const ScrollingCaptureOptions& options)
{
    saveScrollMethod(options.scrollMethod);
    saveScrollDelayMs(options.scrollDelayMs);
    saveScrollAmount(options.scrollAmount);
    saveAutoScrollTop(options.autoScrollTop);
    saveAutoIgnoreBottomEdge(options.autoIgnoreBottomEdge);
    saveMaxFrames(options.maxFrames);
    saveMaxHeightPx(options.maxHeightPx);
    saveMinMatchLines(options.minMatchLines);
    saveStartDelayMs(options.startDelayMs);
}

ScrollMethod ScrollingCaptureSettingsManager::loadScrollMethod() const
{
    auto settings = SnapTray::getSettings();
    int value = settings.value(kKeyScrollMethod,
                               static_cast<int>(ScrollingCaptureOptions().scrollMethod)).toInt();
    return static_cast<ScrollMethod>(value);
}

void ScrollingCaptureSettingsManager::saveScrollMethod(ScrollMethod method)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyScrollMethod, static_cast<int>(method));
}

int ScrollingCaptureSettingsManager::loadScrollDelayMs() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyScrollDelay,
                          ScrollingCaptureOptions().scrollDelayMs).toInt();
}

void ScrollingCaptureSettingsManager::saveScrollDelayMs(int delayMs)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyScrollDelay, delayMs);
}

int ScrollingCaptureSettingsManager::loadScrollAmount() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyScrollAmount,
                          ScrollingCaptureOptions().scrollAmount).toInt();
}

void ScrollingCaptureSettingsManager::saveScrollAmount(int amount)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyScrollAmount, amount);
}

bool ScrollingCaptureSettingsManager::loadAutoScrollTop() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyAutoScrollTop,
                          ScrollingCaptureOptions().autoScrollTop).toBool();
}

void ScrollingCaptureSettingsManager::saveAutoScrollTop(bool enabled)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyAutoScrollTop, enabled);
}

bool ScrollingCaptureSettingsManager::loadAutoIgnoreBottomEdge() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyAutoIgnoreBottom,
                          ScrollingCaptureOptions().autoIgnoreBottomEdge).toBool();
}

void ScrollingCaptureSettingsManager::saveAutoIgnoreBottomEdge(bool enabled)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyAutoIgnoreBottom, enabled);
}

int ScrollingCaptureSettingsManager::loadMaxFrames() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyMaxFrames,
                          ScrollingCaptureOptions().maxFrames).toInt();
}

void ScrollingCaptureSettingsManager::saveMaxFrames(int maxFrames)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyMaxFrames, maxFrames);
}

int ScrollingCaptureSettingsManager::loadMaxHeightPx() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyMaxHeight,
                          ScrollingCaptureOptions().maxHeightPx).toInt();
}

void ScrollingCaptureSettingsManager::saveMaxHeightPx(int maxHeight)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyMaxHeight, maxHeight);
}

int ScrollingCaptureSettingsManager::loadMinMatchLines() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyMinMatchLines,
                          ScrollingCaptureOptions().minMatchLines).toInt();
}

void ScrollingCaptureSettingsManager::saveMinMatchLines(int minLines)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyMinMatchLines, minLines);
}

int ScrollingCaptureSettingsManager::loadStartDelayMs() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyStartDelay,
                          ScrollingCaptureOptions().startDelayMs).toInt();
}

void ScrollingCaptureSettingsManager::saveStartDelayMs(int delayMs)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyStartDelay, delayMs);
}
