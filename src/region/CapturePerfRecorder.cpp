#include "region/CapturePerfRecorder.h"

#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QRegion>

namespace snaptray::region {

namespace {

bool capturePerfEnabled()
{
#if defined(Q_OS_WIN) && !defined(QT_NO_DEBUG_OUTPUT)
    static const bool enabled = qEnvironmentVariableIntValue("SNAPTRAY_CAPTURE_PERF") > 0;
    return enabled;
#else
    return false;
#endif
}

QString rectSummary(const QRect& rect)
{
    if (!rect.isValid() || rect.isEmpty()) {
        return QStringLiteral("rect=none");
    }

    return QStringLiteral("rect=%1,%2 %3x%4 area=%5")
        .arg(rect.x())
        .arg(rect.y())
        .arg(rect.width())
        .arg(rect.height())
        .arg(rect.width() * rect.height());
}

} // namespace

bool CapturePerfRecorder::enabled()
{
    return capturePerfEnabled();
}

void CapturePerfRecorder::recordEvent(const char* eventName,
                                      const QRect& rect,
                                      int rectCount,
                                      const char* detail)
{
    if (!capturePerfEnabled()) {
        return;
    }

    QString message = QStringLiteral("CapturePerf %1 %2")
        .arg(QString::fromUtf8(eventName), rectSummary(rect));
    if (rectCount >= 0) {
        message += QStringLiteral(" rectCount=%1").arg(rectCount);
    }
    if (detail && *detail) {
        message += QStringLiteral(" detail=%1").arg(QString::fromUtf8(detail));
    }
    qDebug().noquote() << message;
}

void CapturePerfRecorder::recordRegion(const char* eventName,
                                       const QRegion& region,
                                       const char* detail)
{
    recordEvent(eventName, region.boundingRect(), region.rectCount(), detail);
}

void CapturePerfRecorder::recordValue(const char* eventName,
                                      const QString& value)
{
    if (!capturePerfEnabled()) {
        return;
    }

    qDebug().noquote() << QStringLiteral("CapturePerf %1 %2")
        .arg(QString::fromUtf8(eventName), value);
}

CapturePerfScope::CapturePerfScope(const char* scopeName, const char* detail)
    : m_scopeName(scopeName)
    , m_detail(detail)
    , m_enabled(capturePerfEnabled())
{
    if (!m_enabled) {
        return;
    }

    m_startedMs = QDateTime::currentMSecsSinceEpoch();
}

CapturePerfScope::~CapturePerfScope()
{
    if (!m_enabled) {
        return;
    }

    const qint64 elapsedMs = QDateTime::currentMSecsSinceEpoch() - m_startedMs;
    QString message = QStringLiteral("CapturePerfScope %1 elapsedMs=%2")
        .arg(QString::fromUtf8(m_scopeName))
        .arg(elapsedMs);
    if (m_detail && *m_detail) {
        message += QStringLiteral(" detail=%1").arg(QString::fromUtf8(m_detail));
    }
    qDebug().noquote() << message;
}

} // namespace snaptray::region
