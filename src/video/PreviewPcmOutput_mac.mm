#include "video/RemixedAudioPreview.h"
#import <AudioToolbox/AudioToolbox.h>
#include <array>
#include <atomic>
#include <cstring>

namespace SnapTray {
namespace {
class QueuePreviewOutput final : public PreviewPcmOutput {
public:
    ~QueuePreviewOutput() override { close(); }
    bool open() override {
        close();
        AudioStreamBasicDescription format{};
        format.mSampleRate = 48000; format.mFormatID = kAudioFormatLinearPCM;
        format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
        format.mBytesPerPacket = format.mBytesPerFrame = 4; format.mFramesPerPacket = 1;
        format.mChannelsPerFrame = 2; format.mBitsPerChannel = 16;
        if (AudioQueueNewOutput(&format, completed, this, nullptr, nullptr, 0, &m_queue) != noErr) return false;
        for (int i = 0; i < kBufferCount; ++i) {
            m_busy[i] = false;
            if (AudioQueueAllocateBuffer(m_queue, kChunkFrames * 4, &m_buffers[i]) != noErr) { close(); return false; }
        }
        return true;
    }
    void reset() override {
        if (!m_queue) return;
        AudioQueueStop(m_queue, true);
        AudioQueueReset(m_queue);
        for (auto& busy : m_busy) busy = false;
        m_started = false;
    }
    int queuedFrames() const override {
        int count = 0;
        for (const auto& busy : m_busy) if (busy.load()) count += kChunkFrames;
        return count;
    }
    bool write(const QByteArray& pcm) override {
        for (int i = 0; i < kBufferCount; ++i) if (!m_busy[i].exchange(true)) {
            auto* buffer = m_buffers[i];
            std::memcpy(buffer->mAudioData, pcm.constData(), size_t(pcm.size()));
            buffer->mAudioDataByteSize = UInt32(pcm.size());
            if (AudioQueueEnqueueBuffer(m_queue, buffer, 0, nullptr) != noErr) { m_busy[i] = false; return false; }
            if (!m_started) {
                if (AudioQueueStart(m_queue, nullptr) != noErr) return false;
                m_started = true;
            }
            return true;
        }
        return false;
    }
private:
    static void completed(void* context, AudioQueueRef, AudioQueueBufferRef buffer) {
        auto* self = static_cast<QueuePreviewOutput*>(context);
        for (int i = 0; i < kBufferCount; ++i) if (self->m_buffers[i] == buffer) self->m_busy[i] = false;
    }
    void close() {
        if (m_queue) AudioQueueDispose(m_queue, true);
        m_queue = nullptr; m_buffers.fill(nullptr); m_started = false;
        for (auto& busy : m_busy) busy = false;
    }
    AudioQueueRef m_queue = nullptr;
    std::array<AudioQueueBufferRef, kBufferCount> m_buffers{};
    std::array<std::atomic<bool>, kBufferCount> m_busy{};
    bool m_started = false;
};
}
std::unique_ptr<PreviewPcmOutput> createPreviewPcmOutput() { return std::make_unique<QueuePreviewOutput>(); }
} // namespace SnapTray
