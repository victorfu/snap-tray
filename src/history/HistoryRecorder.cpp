#include "history/HistoryRecorder.h"

#include <QDebug>
#include <utility>

namespace SnapTray {

HistoryRecorder& HistoryRecorder::instance()
{
    static HistoryRecorder recorder;
    return recorder;
}

HistoryRecorder::HistoryRecorder()
{
    m_pool.setMaxThreadCount(1);
    m_pool.setExpiryTimeout(-1);
}

HistoryRecorder::~HistoryRecorder()
{
    m_pool.waitForDone();
}

void HistoryRecorder::submitCaptureSession(CaptureSessionWriteRequest request)
{
    m_pool.start([request = std::move(request)]() mutable {
        if (!HistoryStore::writeCaptureSession(request).has_value()) {
            qWarning() << "HistoryRecorder: Failed to persist capture session";
        }
    });
}

bool HistoryRecorder::waitForIdleForTests(int timeoutMs)
{
    return m_pool.waitForDone(timeoutMs);
}

} // namespace SnapTray
