#ifndef ISCROLLAUTOMATIONDRIVER_H
#define ISCROLLAUTOMATIONDRIVER_H

#include <QObject>
#include <QPoint>
#include <QString>

class QScreen;

enum class ScrollAutomationMode {
    Auto,
    AutoSynthetic,
    ManualOnly
};

enum class ScrollStepStatus {
    Stepped,
    EndReached,
    Failed
};

struct ScrollProbeResult {
    ScrollAutomationMode mode = ScrollAutomationMode::ManualOnly;
    QString reason;
    QPoint anchorPoint;
    bool focusRecommended = false;
};

struct ScrollStepResult {
    ScrollStepStatus status = ScrollStepStatus::Failed;
    int estimatedDeltaY = 0;
    QString error;
    bool inputInjected = false;
    bool targetLocked = false;
};

class IScrollAutomationDriver : public QObject
{
public:
    explicit IScrollAutomationDriver(QObject *parent = nullptr)
        : QObject(parent) {}
    ~IScrollAutomationDriver() override = default;

    virtual ScrollProbeResult probeAt(const QPoint &globalPoint, QScreen *screen) = 0;
    virtual ScrollStepResult step() = 0;
    virtual bool focusTarget() { return false; }
    virtual bool forceSyntheticFallback() { return false; }
    virtual void reset() = 0;
};

#endif // ISCROLLAUTOMATIONDRIVER_H
