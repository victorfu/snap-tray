#include "WindowDetector.h"

#include <QScreen>

WindowDetector::WindowDetector(QObject* parent)
    : QObject(parent)
    , m_enabled(false)
{
}

WindowDetector::~WindowDetector() = default;

bool WindowDetector::hasAccessibilityPermission(bool promptIfMissing)
{
    Q_UNUSED(promptIfMissing);
    return false;
}

void WindowDetector::setScreen(QScreen* screen)
{
    m_currentScreen = screen;
}

void WindowDetector::setEnabled(bool enabled)
{
    Q_UNUSED(enabled);
    m_enabled = false;
}

bool WindowDetector::isEnabled() const
{
    return false;
}

void WindowDetector::refreshWindowList(QueryMode queryMode)
{
    Q_UNUSED(queryMode);
    m_cacheReady = true;
    m_refreshComplete.store(true);
    emit windowListReady();
}

void WindowDetector::refreshWindowListAsync(QueryMode queryMode)
{
    refreshWindowList(queryMode);
}

bool WindowDetector::isRefreshComplete() const
{
    return true;
}

bool WindowDetector::isWindowCacheReady(QueryMode queryMode) const
{
    Q_UNUSED(queryMode);
    return true;
}

std::optional<DetectedElement> WindowDetector::detectWindowAt(const QPoint& screenPos,
                                                              QueryMode queryMode) const
{
    Q_UNUSED(screenPos);
    Q_UNUSED(queryMode);
    return std::nullopt;
}

DetectionFlags WindowDetector::detectionFlags() const
{
    return DetectionFlags(DetectionFlag::None);
}

std::optional<DetectedElement> WindowDetector::detectChildElementAt(
    const QPoint& screenPos,
    const DetectedElement& topWindow,
    size_t topLevelIndex) const
{
    Q_UNUSED(screenPos);
    Q_UNUSED(topWindow);
    Q_UNUSED(topLevelIndex);
    return std::nullopt;
}

void WindowDetector::enumerateWindows()
{
}

void WindowDetector::enumerateWindowsInternal(std::vector<DetectedElement>& cache,
                                              qreal dpr,
                                              DetectionFlags flags)
{
    Q_UNUSED(dpr);
    Q_UNUSED(flags);
    cache.clear();
}
