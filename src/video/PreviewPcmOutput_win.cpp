#include "video/RemixedAudioPreview.h"
#include <windows.h>
#include <mmsystem.h>
#include <array>
#include <cstring>

namespace SnapTray {
namespace {
class WavePreviewOutput final : public PreviewPcmOutput {
public:
    ~WavePreviewOutput() override { close(); }
    bool open() override {
        close();
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM; format.nChannels = 2;
        format.nSamplesPerSec = 48000; format.wBitsPerSample = 16;
        format.nBlockAlign = 4; format.nAvgBytesPerSec = 192000;
        if (waveOutOpen(&m_output, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) return false;
        for (int i = 0; i < kBufferCount; ++i) {
            m_data[i].resize(kChunkFrames * 4);
            m_headers[i] = {};
            m_headers[i].lpData = m_data[i].data();
            m_headers[i].dwBufferLength = DWORD(m_data[i].size());
            if (waveOutPrepareHeader(m_output, &m_headers[i], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
                close(); return false;
            }
        }
        return true;
    }
    void reset() override { if (m_output) waveOutReset(m_output); }
    int queuedFrames() const override {
        int count = 0;
        for (const auto& h : m_headers) if (h.dwFlags & WHDR_INQUEUE) count += kChunkFrames;
        return count;
    }
    bool write(const QByteArray& pcm) override {
        for (int i = 0; i < kBufferCount; ++i) if (!(m_headers[i].dwFlags & WHDR_INQUEUE)) {
            std::memcpy(m_data[i].data(), pcm.constData(), size_t(pcm.size()));
            return waveOutWrite(m_output, &m_headers[i], sizeof(WAVEHDR)) == MMSYSERR_NOERROR;
        }
        return false;
    }
private:
    void close() {
        if (!m_output) return;
        reset();
        for (auto& h : m_headers) if (h.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(m_output, &h, sizeof(WAVEHDR));
        waveOutClose(m_output); m_output = nullptr;
    }
    HWAVEOUT m_output = nullptr;
    std::array<WAVEHDR, kBufferCount> m_headers{};
    std::array<QByteArray, kBufferCount> m_data;
};
}
std::unique_ptr<PreviewPcmOutput> createPreviewPcmOutput() { return std::make_unique<WavePreviewOutput>(); }
} // namespace SnapTray
