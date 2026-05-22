#include "WindowDetector.h"

#include <QMutexLocker>
#include <QScreen>

namespace {

bool shouldPreferTopLevelBoundsForElementType(ElementType elementType)
{
    switch (elementType) {
    case ElementType::ContextMenu:
    case ElementType::PopupMenu:
    case ElementType::StatusBarItem:
        return true;
    case ElementType::Window:
    case ElementType::Dialog:
    case ElementType::Unknown:
        return false;
    }

    return false;
}

} // namespace

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
    m_enabled = enabled;
}

bool WindowDetector::isEnabled() const
{
    return m_enabled;
}

void WindowDetector::refreshWindowList(QueryMode queryMode)
{
    {
        QMutexLocker locker(&m_cacheMutex);
        m_windowCache.clear();
        m_cacheScreen = m_currentScreen.data();
        m_cacheQueryMode = queryMode;
        m_cacheReady = true;
    }
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
    QMutexLocker locker(&m_cacheMutex);
    return m_cacheReady &&
           m_cacheScreen == m_currentScreen.data() &&
           static_cast<int>(m_cacheQueryMode) >= static_cast<int>(queryMode);
}

std::optional<DetectedElement> WindowDetector::detectWindowAt(const QPoint& screenPos,
                                                              QueryMode queryMode) const
{
    if (!m_enabled) {
        return std::nullopt;
    }

    const QRect screenGeometry = m_currentScreen ? m_currentScreen->geometry() : QRect();
    if (!screenGeometry.isNull() && !screenGeometry.contains(screenPos)) {
        return std::nullopt;
    }

    QMutexLocker locker(&m_cacheMutex);
    if (!m_cacheReady || m_cacheScreen != m_currentScreen.data()) {
        return std::nullopt;
    }

    const bool detectChildren =
        queryMode == QueryMode::IncludeChildControls &&
        m_cacheQueryMode == QueryMode::IncludeChildControls &&
        m_detectionFlags.testFlag(DetectionFlag::ChildControls);

    for (size_t i = 0; i < m_windowCache.size(); ++i) {
        const auto& element = m_windowCache[i];
        if (element.windowLayer == 1) {
            continue;
        }

        if (!element.bounds.contains(screenPos)) {
            continue;
        }

        if (!screenGeometry.isNull()) {
            const QRect clipped = element.bounds.intersected(screenGeometry);
            if (!clipped.contains(screenPos)) {
                continue;
            }
        }

        if (shouldPreferTopLevelBoundsForElementType(element.elementType)) {
            return element;
        }

        if (detectChildren) {
            const auto childElement = detectChildElementAt(screenPos, element, i);
            if (childElement.has_value()) {
                return childElement;
            }
        }

        return element;
    }

    return std::nullopt;
}

DetectionFlags WindowDetector::detectionFlags() const
{
    return m_detectionFlags;
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
