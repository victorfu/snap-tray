#pragma once
#include "IVideoEncoder.h"
#include "encoding/EncoderFactory.h"
#include <atomic>
#include <memory>

struct AudioEncoderTestState {
    bool supportsAudio = true;
    bool acceptsAudio = true;
    bool starts = true;
    std::atomic<int> created{0};
    std::atomic<int> destroyed{0};
    std::atomic<int> audioWrites{0};
};

class FakeAudioEncoder final : public IVideoEncoder
{
public:
    FakeAudioEncoder(std::shared_ptr<AudioEncoderTestState> state, QObject* parent)
        : IVideoEncoder(parent), m_state(std::move(state)) { ++m_state->created; }
    ~FakeAudioEncoder() override { ++m_state->destroyed; }
    bool isAvailable() const override { return true; }
    QString encoderName() const override { return "test"; }
    bool start(const QString& path, const QSize&, int) override {
        m_path = path; m_running = m_state->starts; return m_running;
    }
    void writeFrame(const QImage&, qint64) override {}
    void finish() override { m_running = false; emit finished(true, m_path); }
    void abort() override { m_running = false; }
    bool isRunning() const override { return m_running; }
    QString lastError() const override { return "injected startup failure"; }
    qint64 framesWritten() const override { return 0; }
    QString outputPath() const override { return m_path; }
    void setAudioFormat(int, int, int) override { m_audioRequested = true; }
    bool isAudioSupported() const override { return m_state->supportsAudio; }
    bool isAudioEnabled() const override { return m_running && m_audioRequested && m_state->acceptsAudio; }
    void writeAudioSamples(const QByteArray&, qint64) override { ++m_state->audioWrites; }
private:
    std::shared_ptr<AudioEncoderTestState> m_state;
    bool m_running = false;
    bool m_audioRequested = false;
    QString m_path;
};

struct EncoderFactoryTestAccess {
    static EncoderFactory::EncoderResult create(const EncoderFactory::EncoderConfig& config,
                                                QObject* parent,
                                                std::shared_ptr<AudioEncoderTestState> state)
    {
        return EncoderFactory::createWithNativeFactory(config, parent,
            [state](QObject* owner) { return new FakeAudioEncoder(state, owner); });
    }
};
