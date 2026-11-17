#pragma once

#include "recording/RecordingProject.h"
#include <QObject>
#include <atomic>
#include <functional>
#include <memory>

class IVideoFrameReader;
namespace SnapTray {
// Decode and compose a committed snapshot off-thread. A null image means that
// no safe preview is available; callers must never substitute a raw frame.
QImage renderProjectThumbnail(const RecordingProject& project, IVideoFrameReader& reader,
                              const std::atomic_bool* cancelled = nullptr);

// Application-thread owner with one dedicated worker, cancellable decoding,
// revision coalescing and at most three attempts for transient failures.
class RecordingProjectThumbnailQueue : public QObject {
public:
    using Renderer = std::function<QImage(const RecordingProject&, const std::atomic_bool&)>;
    explicit RecordingProjectThumbnailQueue(QObject* parent = nullptr, Renderer renderer = {});
    ~RecordingProjectThumbnailQueue() override;
    void request(const RecordingProject& project);
    void shutdown();
private:
    struct State;
    std::unique_ptr<State> m_state;
    void startNext();
};
void requestProjectThumbnail(const RecordingProject& project);
} // namespace SnapTray
