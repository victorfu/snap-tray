#include "settings/RegionCaptureSettingsManager.h"
#include "settings/Settings.h"

#include <QtGlobal>

namespace {

double clampGoodThreshold(double threshold)
{
    return qBound(0.50, threshold, 0.99);
}

double clampPartialThreshold(double threshold, double goodThreshold)
{
    return qBound(0.40, threshold, goodThreshold);
}

int clampMinScrollAmount(int amount)
{
    return qBound(1, amount, 2000);
}

int clampAutoStepDelayMs(int delayMs)
{
    return qBound(40, delayMs, 3000);
}

int clampMaxFrames(int maxFrames)
{
    return qBound(10, maxFrames, 10000);
}

int clampMaxOutputPixels(int maxOutputPixels)
{
    return qBound(1'000'000, maxOutputPixels, 500'000'000);
}

int clampSettleStableFrames(int frames)
{
    return qBound(1, frames, 6);
}

int clampSettleTimeoutMs(int timeoutMs)
{
    return qBound(80, timeoutMs, 5000);
}

int clampProbeGridDensity(int density)
{
    return qBound(2, density, 5);
}

RegionCaptureSettingsManager::ScrollAutoStartMode clampAutoStartMode(int rawMode)
{
    const int bounded = qBound(0, rawMode, 1);
    return static_cast<RegionCaptureSettingsManager::ScrollAutoStartMode>(bounded);
}

} // namespace

RegionCaptureSettingsManager& RegionCaptureSettingsManager::instance()
{
    static RegionCaptureSettingsManager instance;
    return instance;
}

bool RegionCaptureSettingsManager::isShortcutHintsEnabled() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kSettingsKeyShowShortcutHints, kDefaultShortcutHintsEnabled).toBool();
}

void RegionCaptureSettingsManager::setShortcutHintsEnabled(bool enabled)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyShowShortcutHints, enabled);
}

bool RegionCaptureSettingsManager::isScrollAutomationEnabled() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kSettingsKeyScrollAutomationEnabled, kDefaultScrollAutomationEnabled).toBool();
}

void RegionCaptureSettingsManager::setScrollAutomationEnabled(bool enabled)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyScrollAutomationEnabled, enabled);
}

double RegionCaptureSettingsManager::loadScrollGoodThreshold() const
{
    auto settings = SnapTray::getSettings();
    const double stored = settings.value(kSettingsKeyScrollGoodThreshold, kDefaultScrollGoodThreshold).toDouble();
    return clampGoodThreshold(stored);
}

void RegionCaptureSettingsManager::saveScrollGoodThreshold(double threshold)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyScrollGoodThreshold, clampGoodThreshold(threshold));
}

double RegionCaptureSettingsManager::loadScrollPartialThreshold() const
{
    auto settings = SnapTray::getSettings();
    const double good = loadScrollGoodThreshold();
    const double stored = settings.value(kSettingsKeyScrollPartialThreshold, kDefaultScrollPartialThreshold).toDouble();
    return clampPartialThreshold(stored, good);
}

void RegionCaptureSettingsManager::saveScrollPartialThreshold(double threshold)
{
    auto settings = SnapTray::getSettings();
    const double good = loadScrollGoodThreshold();
    settings.setValue(kSettingsKeyScrollPartialThreshold, clampPartialThreshold(threshold, good));
}

int RegionCaptureSettingsManager::loadScrollMinScrollAmount() const
{
    auto settings = SnapTray::getSettings();
    const int stored = settings.value(kSettingsKeyScrollMinScrollAmount, kDefaultScrollMinScrollAmount).toInt();
    return clampMinScrollAmount(stored);
}

void RegionCaptureSettingsManager::saveScrollMinScrollAmount(int amount)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyScrollMinScrollAmount, clampMinScrollAmount(amount));
}

int RegionCaptureSettingsManager::loadScrollAutoStepDelayMs() const
{
    auto settings = SnapTray::getSettings();
    const int stored = settings.value(kSettingsKeyScrollAutoStepDelayMs, kDefaultScrollAutoStepDelayMs).toInt();
    return clampAutoStepDelayMs(stored);
}

void RegionCaptureSettingsManager::saveScrollAutoStepDelayMs(int delayMs)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyScrollAutoStepDelayMs, clampAutoStepDelayMs(delayMs));
}

int RegionCaptureSettingsManager::loadScrollMaxFrames() const
{
    auto settings = SnapTray::getSettings();
    const int stored = settings.value(kSettingsKeyScrollMaxFrames, kDefaultScrollMaxFrames).toInt();
    return clampMaxFrames(stored);
}

void RegionCaptureSettingsManager::saveScrollMaxFrames(int maxFrames)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyScrollMaxFrames, clampMaxFrames(maxFrames));
}

int RegionCaptureSettingsManager::loadScrollMaxOutputPixels() const
{
    auto settings = SnapTray::getSettings();
    const int stored = settings.value(kSettingsKeyScrollMaxOutputPixels, kDefaultScrollMaxOutputPixels).toInt();
    return clampMaxOutputPixels(stored);
}

void RegionCaptureSettingsManager::saveScrollMaxOutputPixels(int maxOutputPixels)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyScrollMaxOutputPixels, clampMaxOutputPixels(maxOutputPixels));
}

bool RegionCaptureSettingsManager::loadScrollAutoIgnoreBottomEdge() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kSettingsKeyScrollAutoIgnoreBottomEdge, kDefaultScrollAutoIgnoreBottomEdge).toBool();
}

void RegionCaptureSettingsManager::saveScrollAutoIgnoreBottomEdge(bool enabled)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyScrollAutoIgnoreBottomEdge, enabled);
}

int RegionCaptureSettingsManager::loadScrollSettleStableFrames() const
{
    auto settings = SnapTray::getSettings();
    const int stored = settings.value(kSettingsKeyScrollSettleStableFrames,
                                      kDefaultScrollSettleStableFrames).toInt();
    return clampSettleStableFrames(stored);
}

void RegionCaptureSettingsManager::saveScrollSettleStableFrames(int frames)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyScrollSettleStableFrames, clampSettleStableFrames(frames));
}

int RegionCaptureSettingsManager::loadScrollSettleTimeoutMs() const
{
    auto settings = SnapTray::getSettings();
    const int stored = settings.value(kSettingsKeyScrollSettleTimeoutMs,
                                      kDefaultScrollSettleTimeoutMs).toInt();
    return clampSettleTimeoutMs(stored);
}

void RegionCaptureSettingsManager::saveScrollSettleTimeoutMs(int timeoutMs)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyScrollSettleTimeoutMs, clampSettleTimeoutMs(timeoutMs));
}

int RegionCaptureSettingsManager::loadScrollProbeGridDensity() const
{
    auto settings = SnapTray::getSettings();
    const int stored = settings.value(kSettingsKeyScrollProbeGridDensity,
                                      kDefaultScrollProbeGridDensity).toInt();
    return clampProbeGridDensity(stored);
}

void RegionCaptureSettingsManager::saveScrollProbeGridDensity(int density)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyScrollProbeGridDensity, clampProbeGridDensity(density));
}

RegionCaptureSettingsManager::ScrollAutoStartMode
RegionCaptureSettingsManager::loadScrollAutoStartMode() const
{
    auto settings = SnapTray::getSettings();
    const int stored = settings.value(kSettingsKeyScrollAutoStartMode,
                                      static_cast<int>(kDefaultScrollAutoStartMode)).toInt();
    return clampAutoStartMode(stored);
}

void RegionCaptureSettingsManager::saveScrollAutoStartMode(ScrollAutoStartMode mode)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyScrollAutoStartMode, static_cast<int>(mode));
}

bool RegionCaptureSettingsManager::loadScrollInlinePreviewCollapsed() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kSettingsKeyScrollInlinePreviewCollapsed,
                          kDefaultScrollInlinePreviewCollapsed).toBool();
}

void RegionCaptureSettingsManager::saveScrollInlinePreviewCollapsed(bool collapsed)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyScrollInlinePreviewCollapsed, collapsed);
}
