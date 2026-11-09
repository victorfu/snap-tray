#pragma once
#include "capture/ICaptureEngine.h"
#include <memory>

// Owns a separate XCB connection and captures off the GUI thread. The consumer
// takes the newest completed frame; stale frames are never enqueued repeatedly.
class X11CaptureEngine final : public ICaptureEngine
{
public:
    explicit X11CaptureEngine(QObject* parent = nullptr);
    ~X11CaptureEngine() override;
    bool setRegion(const QRect&, QScreen*) override;
    bool setRegion(const QRect&, const CaptureScreenInfo&) override;
    void setFrameRate(int fps) override;
    bool start() override;
    void stop() override;
    bool isRunning() const override;
    QImage captureFrame() override;
    bool deliversFrames() const override { return true; }
    QString engineName() const override { return QStringLiteral("X11 shared memory"); }
private:
    struct State;
    std::unique_ptr<State> d;
    CaptureScreenInfo m_info;
};
