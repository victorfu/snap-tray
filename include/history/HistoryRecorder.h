#pragma once

#include "history/HistoryStore.h"

#include <QObject>
#include <QThreadPool>

namespace SnapTray {

class HistoryRecorder : public QObject
{
    Q_OBJECT
public:
    static HistoryRecorder& instance();

    void submitCaptureSession(CaptureSessionWriteRequest request);

    // Test helper for draining the background queue deterministically.
    bool waitForIdleForTests(int timeoutMs = 5000);

signals:
    void captureSessionWritten(const QString& id, bool success);

private:
    HistoryRecorder();
    ~HistoryRecorder();

    QThreadPool m_pool;
};

} // namespace SnapTray
