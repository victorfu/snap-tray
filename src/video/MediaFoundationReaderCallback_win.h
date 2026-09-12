#pragma once

#include "SourceReaderMailbox.h"
#include <windows.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <atomic>

// Owned through COM references by the player and Source Reader. No callback
// refers to a player, QWidget, or worker thread that may already be destroyed.
class MediaFoundationReaderCallback final : public IMFSourceReaderCallback
{
public:
    struct Result {
        HRESULT status = S_OK;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        Microsoft::WRL::ComPtr<IMFSample> sample;
    };

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override
    {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IMFSourceReaderCallback)) {
            *result = static_cast<IMFSourceReaderCallback*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_references; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG remaining = --m_references;
        if (remaining == 0) delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE OnReadSample(HRESULT status, DWORD, DWORD flags,
                                           LONGLONG timestamp, IMFSample* sample) override
    {
        Result result;
        result.status = status;
        result.flags = flags;
        result.timestamp = timestamp;
        result.sample = sample;
        m_mailbox.deliver(std::move(result));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnEvent(DWORD, IMFMediaEvent*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnFlush(DWORD) override { return S_OK; }

    std::optional<Result> wait() { return m_mailbox.wait(); }
    void cancel() { m_mailbox.cancel(); }

private:
    ~MediaFoundationReaderCallback() = default;
    std::atomic<ULONG> m_references{1};
    SourceReaderMailbox<Result> m_mailbox;
};
