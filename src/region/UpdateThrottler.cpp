#include "region/UpdateThrottler.h"

UpdateThrottler::UpdateThrottler()
{
}

void UpdateThrottler::startAll()
{
    m_selectionTimer.start();
    m_annotationTimer.start();
    m_hoverTimer.start();
    m_magnifierTimer.start();
}

bool UpdateThrottler::shouldUpdate(ThrottleType type) const
{
    const QElapsedTimer& timer = getTimer(type);
    return timer.elapsed() >= getInterval(type);
}

void UpdateThrottler::reset(ThrottleType type)
{
    getTimer(type).restart();
}

int UpdateThrottler::getInterval(ThrottleType type) const
{
    switch (type) {
    case ThrottleType::Selection:
        return kSelectionMs;
    case ThrottleType::Annotation:
        return kAnnotationMs;
    case ThrottleType::Hover:
        return kHoverMs;
    case ThrottleType::Magnifier:
        return kMagnifierMs;
    }
    return kSelectionMs;  // Default fallback
}

QElapsedTimer& UpdateThrottler::getTimer(ThrottleType type)
{
    switch (type) {
    case ThrottleType::Selection:
        return m_selectionTimer;
    case ThrottleType::Annotation:
        return m_annotationTimer;
    case ThrottleType::Hover:
        return m_hoverTimer;
    case ThrottleType::Magnifier:
        return m_magnifierTimer;
    }
    return m_selectionTimer;  // Default fallback
}

const QElapsedTimer& UpdateThrottler::getTimer(ThrottleType type) const
{
    switch (type) {
    case ThrottleType::Selection:
        return m_selectionTimer;
    case ThrottleType::Annotation:
        return m_annotationTimer;
    case ThrottleType::Hover:
        return m_hoverTimer;
    case ThrottleType::Magnifier:
        return m_magnifierTimer;
    }
    return m_selectionTimer;  // Default fallback
}
