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

int clampMaxFrames(int maxFrames)
{
    return qBound(10, maxFrames, 20000);
}

int clampMaxOutputPixels(int maxOutputPixels)
{
    return qBound(1'000'000, maxOutputPixels, 500'000'000);
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
