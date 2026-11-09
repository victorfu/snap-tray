#include "recording/WindowTimelineRecorder.h"

#include <QDebug>
#include <QHash>
#include <QScreen>

#include <algorithm>
#include <memory>
#include <utility>

namespace SnapTray {

namespace {

bool isSnapTarget(ElementType type)
{
    return type == ElementType::Window || type == ElementType::Dialog;
}

} // namespace

WindowTimelineRecorder::WindowTimelineRecorder(Enumerator enumerator, Clock clock, WindowFrameMapping mapping,
                                               QObject* parent)
    : QObject(parent)
    , m_enumerator(std::move(enumerator))
    , m_clock(std::move(clock))
    , m_mapping(mapping)
{
    m_timeline.setFrameSize(m_mapping.physicalRegion.size());
    m_timer.setInterval(kSampleIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &WindowTimelineRecorder::sampleNow);
}

WindowTimelineRecorder::Enumerator WindowTimelineRecorder::detectorEnumerator(QScreen* screen)
{
    auto detector = std::make_shared<WindowDetector>();
    detector->setScreen(screen);
    detector->setEnabled(true);
    return [detector, appNames = QHash<qint64, QString>()]() mutable {
        // Top level only: no titles, no child controls, a few milliseconds.
        detector->refreshWindowList(WindowDetector::QueryMode::TopLevelOnly);
        auto windows = detector->topLevelWindowsSnapshot();
        QHash<qint64, QString> currentAppNames;
        for (auto& window : windows) {
            if (!isSnapTarget(window.elementType) || window.ownerPid <= 0) continue;
            if (window.ownerApp.isEmpty()) {
                window.ownerApp = currentAppNames.value(window.ownerPid, appNames.value(window.ownerPid));
                if (window.ownerApp.isEmpty()) {
                    WindowDetector::populateWindowMetadata(window, false);
                }
            }
            currentAppNames.insert(window.ownerPid, window.ownerApp);
        }
        // Drop processes no longer present instead of accumulating stale PIDs.
        appNames = std::move(currentAppNames);
        return windows;
    };
}

void WindowTimelineRecorder::start()
{
    if (m_running || m_stopped) {
        return;
    }
    if (!m_mapping.isValid()) {
        qWarning() << "WindowTimelineRecorder: started with an invalid frame mapping; no windows will be recorded";
    }
    m_running = true;
    sampleNow();
    m_timer.start();
}

void WindowTimelineRecorder::pause()
{
    if (!m_running) {
        return;
    }
    m_running = false;
    m_timer.stop();
}

void WindowTimelineRecorder::resume()
{
    if (m_running || m_stopped) {
        return;
    }
    m_running = true;
    sampleNow();
    m_timer.start();
}

void WindowTimelineRecorder::stop()
{
    m_running = false;
    m_stopped = true;
    m_timer.stop();
}

void WindowTimelineRecorder::sampleNow()
{
    if (m_stopped || !m_enumerator || !m_clock || !m_mapping.isValid()) {
        return;
    }
    const qint64 tMs = m_clock();
    std::vector<DetectedElement> elements = m_enumerator();

    // Topmost first: higher layers win, then the enumeration order (already
    // front to back on both platforms).
    std::stable_sort(elements.begin(), elements.end(),
                     [](const DetectedElement& a, const DetectedElement& b) { return a.windowLayer > b.windowLayer; });

    std::vector<WindowSample> windows;
    windows.reserve(elements.size());
    for (const DetectedElement& element : elements) {
        if (!isSnapTarget(element.elementType)) {
            continue;
        }
        const QRect rect = element.nativePhysicalBounds.has_value() && !m_mapping.physicalScreen.isEmpty()
            ? m_mapping.toVideoRectFromPhysical(*element.nativePhysicalBounds)
            : m_mapping.toVideoRect(element.bounds);
        if (rect.isEmpty()) {
            continue;
        }
        WindowSample window;
        window.windowId = element.windowId;
        window.rect = rect;
        window.z = static_cast<int>(windows.size());
        window.ownerApp = element.ownerApp; // never the title
        window.type = element.elementType;
        windows.push_back(std::move(window));
    }
    m_timeline.append(tMs, std::move(windows));
}

} // namespace SnapTray
