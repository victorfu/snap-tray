#ifndef CAPTUREPERFRECORDER_H
#define CAPTUREPERFRECORDER_H

#include <QRect>
#include <QString>

class QRegion;

namespace snaptray::region {

class CapturePerfRecorder
{
public:
    static bool enabled();
    static void recordEvent(const char* eventName,
                            const QRect& rect = QRect(),
                            int rectCount = -1,
                            const char* detail = nullptr);
    static void recordRegion(const char* eventName,
                             const QRegion& region,
                             const char* detail = nullptr);
    static void recordValue(const char* eventName,
                            const QString& value);
};

class CapturePerfScope
{
public:
    explicit CapturePerfScope(const char* scopeName, const char* detail = nullptr);
    ~CapturePerfScope();

private:
    const char* m_scopeName = nullptr;
    const char* m_detail = nullptr;
    qint64 m_startedMs = 0;
    bool m_enabled = false;
};

} // namespace snaptray::region

#endif // CAPTUREPERFRECORDER_H
