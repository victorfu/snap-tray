#pragma once

#include <QMutex>
#include <QMutexLocker>
#include <QWaitCondition>
#include <optional>
#include <utility>

// One outstanding async Source Reader request at a time. Cancellation is
// permanent for this reader generation and never waits for the native callback.
template<typename Result>
class SourceReaderMailbox
{
public:
    void deliver(Result result)
    {
        QMutexLocker locker(&m_mutex);
        if (m_cancelled) return;
        m_result = std::move(result);
        m_ready.wakeAll();
    }

    std::optional<Result> wait()
    {
        QMutexLocker locker(&m_mutex);
        while (!m_cancelled && !m_result) m_ready.wait(&m_mutex);
        if (m_cancelled) return std::nullopt;
        auto result = std::move(m_result);
        m_result.reset();
        return result;
    }

    void cancel()
    {
        QMutexLocker locker(&m_mutex);
        m_cancelled = true;
        m_result.reset();
        m_ready.wakeAll();
    }

private:
    QMutex m_mutex;
    QWaitCondition m_ready;
    std::optional<Result> m_result;
    bool m_cancelled = false;
};
