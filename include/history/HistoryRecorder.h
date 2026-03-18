#pragma once

#include "history/HistoryStore.h"

#include <QThreadPool>

namespace SnapTray {

class HistoryRecorder
{
public:
    static HistoryRecorder& instance();

    void submitCaptureSession(CaptureSessionWriteRequest request);

    // Test helper for draining the background queue deterministically.
    bool waitForIdleForTests(int timeoutMs = 5000);

private:
    HistoryRecorder();
    ~HistoryRecorder();

    QThreadPool m_pool;
};

} // namespace SnapTray
