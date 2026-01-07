#include "video/IVideoPlayer.h"

#ifdef Q_OS_WIN

#include <QDebug>
#include <QDir>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QTimer>
#include <QUrl>

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <Mfobjects.h>
#include <propvarutil.h>
#include <evr.h>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "propsys.lib")
#pragma comment(lib, "strmiids.lib")

// Forward declaration
class MediaFoundationPlayer;

// Sample grabber callback for extracting video frames
class SampleGrabberCallback : public IMFSampleGrabberSinkCallback
{
public:
    SampleGrabberCallback(MediaFoundationPlayer *player)
        : m_refCount(1)
        , m_player(player)
        , m_width(0)
        , m_height(0)
    {}

    // IUnknown methods
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;

        if (riid == IID_IUnknown) {
            *ppv = static_cast<IUnknown*>(this);
        } else if (riid == IID_IMFClockStateSink) {
            *ppv = static_cast<IMFClockStateSink*>(this);
        } else if (riid == IID_IMFSampleGrabberSinkCallback) {
            *ppv = static_cast<IMFSampleGrabberSinkCallback*>(this);
        } else {
            *ppv = nullptr;
            return E_NOINTERFACE;
        }

        AddRef();
        return S_OK;
    }

    STDMETHODIMP_(ULONG) AddRef() override
    {
        return InterlockedIncrement(&m_refCount);
    }

    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG count = InterlockedDecrement(&m_refCount);
        if (count == 0) {
            delete this;
        }
        return count;
    }

    // IMFClockStateSink methods
    STDMETHODIMP OnClockStart(MFTIME, LONGLONG) override { return S_OK; }
    STDMETHODIMP OnClockStop(MFTIME) override { return S_OK; }
    STDMETHODIMP OnClockPause(MFTIME) override { return S_OK; }
    STDMETHODIMP OnClockRestart(MFTIME) override { return S_OK; }
    STDMETHODIMP OnClockSetRate(MFTIME, float) override { return S_OK; }

    // IMFSampleGrabberSinkCallback methods
    STDMETHODIMP OnSetPresentationClock(IMFPresentationClock*) override { return S_OK; }
    STDMETHODIMP OnShutdown() override { return S_OK; }

    STDMETHODIMP OnProcessSample(REFGUID majorType, DWORD dwSampleFlags,
        LONGLONG llSampleTime, LONGLONG llSampleDuration,
        const BYTE* pSampleBuffer, DWORD dwSampleSize) override;

    void setVideoSize(int width, int height)
    {
        m_width = width;
        m_height = height;
    }

private:
    long m_refCount;
    MediaFoundationPlayer *m_player;
    int m_width;
    int m_height;
};

class MediaFoundationPlayer : public IVideoPlayer
{
    Q_OBJECT

public:
    explicit MediaFoundationPlayer(QObject *parent = nullptr);
    ~MediaFoundationPlayer() override;

    // IVideoPlayer interface
    bool load(const QString &filePath) override;
    void play() override;
    void pause() override;
    void stop() override;
    void seek(qint64 positionMs) override;

    State state() const override { return m_state; }
    qint64 duration() const override { return m_duration; }
    qint64 position() const override { return m_position; }
    QSize videoSize() const override { return m_videoSize; }
    bool hasVideo() const override { return m_hasVideo; }

    void setVolume(float volume) override;
    float volume() const override { return m_volume; }
    void setMuted(bool muted) override;
    bool isMuted() const override { return m_muted; }

    void setLooping(bool loop) override { m_looping = loop; }
    bool isLooping() const override { return m_looping; }

    void setPlaybackRate(float rate) override;
    float playbackRate() const override { return m_playbackRate; }

    // Called from SampleGrabberCallback
    void onFrameReceived(const QImage &frame, qint64 timestampMs);

private:
    void cleanup();
    bool createMediaSource(const QString &filePath);
    bool createTopology();
    bool addSourceNode(IMFTopology *topology, IMFPresentationDescriptor *pd,
                       IMFStreamDescriptor *sd, IMFTopologyNode **ppNode);
    bool addVideoOutputNode(IMFTopology *topology, IMFTopologyNode **ppNode);
    bool addAudioOutputNode(IMFTopology *topology, IMFTopologyNode **ppNode);
    void updatePosition();
    void handleEndOfStream();
    void setState(State newState);

    // Media Foundation objects
    IMFMediaSession *m_session = nullptr;
    IMFMediaSource *m_source = nullptr;
    IMFSimpleAudioVolume *m_audioVolume = nullptr;
    SampleGrabberCallback *m_grabberCallback = nullptr;

    QTimer *m_positionTimer;

    State m_state = State::Stopped;
    qint64 m_duration = 0;
    qint64 m_position = 0;
    QSize m_videoSize;
    bool m_hasVideo = false;
    float m_volume = 1.0f;
    bool m_muted = false;
    bool m_looping = false;
    float m_playbackRate = 1.0f;
    bool m_pendingSeek = false;
    qint64 m_seekPosition = 0;
    bool m_mfInitialized = false;

    QMutex m_mutex;
};

// SampleGrabberCallback implementation
STDMETHODIMP SampleGrabberCallback::OnProcessSample(REFGUID majorType, DWORD dwSampleFlags,
    LONGLONG llSampleTime, LONGLONG llSampleDuration,
    const BYTE* pSampleBuffer, DWORD dwSampleSize)
{
    Q_UNUSED(majorType)
    Q_UNUSED(dwSampleFlags)
    Q_UNUSED(llSampleDuration)

    qDebug() << "SampleGrabberCallback::OnProcessSample - time:" << (llSampleTime / 10000)
             << "ms, size:" << dwSampleSize << "bytes";

    if (!m_player || m_width <= 0 || m_height <= 0 || !pSampleBuffer) {
        qDebug() << "OnProcessSample: Invalid params - player:" << (void*)m_player
                 << "w:" << m_width << "h:" << m_height << "buf:" << (void*)pSampleBuffer;
        return S_OK;
    }

    // Create QImage from RGB32 buffer
    // Media Foundation outputs bottom-up, so we need to flip
    int stride = m_width * 4;
    QImage frame(m_width, m_height, QImage::Format_RGB32);

    for (int y = 0; y < m_height; y++) {
        const BYTE *srcRow = pSampleBuffer + (m_height - 1 - y) * stride;
        memcpy(frame.scanLine(y), srcRow, stride);
    }

    qint64 timestampMs = llSampleTime / 10000; // 100ns -> ms
    m_player->onFrameReceived(frame, timestampMs);

    return S_OK;
}

// MediaFoundationPlayer implementation
MediaFoundationPlayer::MediaFoundationPlayer(QObject *parent)
    : IVideoPlayer(parent)
    , m_positionTimer(new QTimer(this))
{
    HRESULT hr = MFStartup(MF_VERSION);
    m_mfInitialized = SUCCEEDED(hr);
    if (!m_mfInitialized) {
        qWarning() << "MediaFoundationPlayer: Failed to initialize Media Foundation:" << Qt::hex << hr;
    }

    connect(m_positionTimer, &QTimer::timeout, this, &MediaFoundationPlayer::updatePosition);
    m_positionTimer->setInterval(100); // 10 Hz position updates
}

MediaFoundationPlayer::~MediaFoundationPlayer()
{
    m_positionTimer->stop();
    cleanup();
    if (m_mfInitialized) {
        MFShutdown();
    }
}

void MediaFoundationPlayer::cleanup()
{
    QMutexLocker locker(&m_mutex);

    if (m_session) {
        m_session->Stop();
        m_session->Close();
        m_session->Release();
        m_session = nullptr;
    }

    if (m_source) {
        m_source->Shutdown();
        m_source->Release();
        m_source = nullptr;
    }

    if (m_audioVolume) {
        m_audioVolume->Release();
        m_audioVolume = nullptr;
    }

    if (m_grabberCallback) {
        m_grabberCallback->Release();
        m_grabberCallback = nullptr;
    }

    m_hasVideo = false;
    m_duration = 0;
    m_position = 0;
    m_videoSize = QSize();
}

bool MediaFoundationPlayer::load(const QString &filePath)
{
    cleanup();

    if (!m_mfInitialized) {
        emit error("Media Foundation not initialized");
        return false;
    }

    qDebug() << "MediaFoundationPlayer: Loading" << filePath;

    // Create media source
    if (!createMediaSource(filePath)) {
        emit error("Failed to create media source");
        return false;
    }

    // Create media session
    HRESULT hr = MFCreateMediaSession(nullptr, &m_session);
    if (FAILED(hr)) {
        qWarning() << "MediaFoundationPlayer: Failed to create media session:" << Qt::hex << hr;
        emit error("Failed to create media session");
        cleanup();
        return false;
    }

    // Create and set topology
    if (!createTopology()) {
        emit error("Failed to create playback topology");
        cleanup();
        return false;
    }

    qDebug() << "MediaFoundationPlayer: Media loaded, duration:" << m_duration
             << "size:" << m_videoSize;

    // Start playback briefly to get the first frame, then pause
    if (m_hasVideo && m_session) {
        qDebug() << "MediaFoundationPlayer: Starting briefly to get first frame...";
        PROPVARIANT varStart;
        PropVariantInit(&varStart);
        varStart.vt = VT_I8;
        varStart.hVal.QuadPart = 0;  // Start from beginning

        HRESULT startHr = m_session->Start(&GUID_NULL, &varStart);
        PropVariantClear(&varStart);

        if (SUCCEEDED(startHr)) {
            // Give time for first frame to be decoded and delivered
            Sleep(150);
            m_session->Pause();
            qDebug() << "MediaFoundationPlayer: Paused after getting first frame";
        }
    }

    emit mediaLoaded();
    return true;
}

bool MediaFoundationPlayer::createMediaSource(const QString &filePath)
{
    QString resolvedPath = filePath;
    if (filePath.startsWith("file://", Qt::CaseInsensitive)) {
        QUrl url(filePath);
        if (url.isLocalFile()) {
            resolvedPath = url.toLocalFile();
        }
    }
    resolvedPath = QDir::toNativeSeparators(resolvedPath);

    IMFSourceResolver *resolver = nullptr;
    MF_OBJECT_TYPE objectType = MF_OBJECT_INVALID;
    IUnknown *sourceUnk = nullptr;

    HRESULT hr = MFCreateSourceResolver(&resolver);
    if (FAILED(hr)) {
        qWarning() << "MediaFoundationPlayer: Failed to create source resolver:" << Qt::hex << hr;
        return false;
    }

    // Pass file path directly - MF expects native Windows paths, not file:// URLs
    hr = resolver->CreateObjectFromURL(
        reinterpret_cast<LPCWSTR>(resolvedPath.utf16()),
        MF_RESOLUTION_MEDIASOURCE,
        nullptr,
        &objectType,
        &sourceUnk
    );

    resolver->Release();

    if (FAILED(hr)) {
        qWarning() << "MediaFoundationPlayer: Failed to create media source from URL:" << Qt::hex << hr;
        return false;
    }

    hr = sourceUnk->QueryInterface(IID_PPV_ARGS(&m_source));
    sourceUnk->Release();

    if (FAILED(hr)) {
        qWarning() << "MediaFoundationPlayer: Failed to get IMFMediaSource:" << Qt::hex << hr;
        return false;
    }

    // Get presentation descriptor for duration and video info
    IMFPresentationDescriptor *pd = nullptr;
    hr = m_source->CreatePresentationDescriptor(&pd);
    if (SUCCEEDED(hr)) {
        // Get duration
        UINT64 duration = 0;
        hr = pd->GetUINT64(MF_PD_DURATION, &duration);
        if (SUCCEEDED(hr)) {
            m_duration = duration / 10000; // 100ns -> ms
        }

        // Iterate streams to find video info
        DWORD streamCount = 0;
        pd->GetStreamDescriptorCount(&streamCount);

        for (DWORD i = 0; i < streamCount; i++) {
            BOOL selected = FALSE;
            IMFStreamDescriptor *sd = nullptr;

            hr = pd->GetStreamDescriptorByIndex(i, &selected, &sd);
            if (SUCCEEDED(hr)) {
                IMFMediaTypeHandler *handler = nullptr;
                hr = sd->GetMediaTypeHandler(&handler);
                if (SUCCEEDED(hr)) {
                    GUID majorType;
                    hr = handler->GetMajorType(&majorType);
                    if (SUCCEEDED(hr) && majorType == MFMediaType_Video) {
                        m_hasVideo = true;

                        // Get video dimensions (fallback to first media type if current is unset).
                        IMFMediaType *mediaType = nullptr;
                        hr = handler->GetCurrentMediaType(&mediaType);
                        if (FAILED(hr)) {
                            hr = handler->GetMediaTypeByIndex(0, &mediaType);
                        }
                        if (SUCCEEDED(hr) && mediaType) {
                            UINT32 width = 0, height = 0;
                            if (SUCCEEDED(MFGetAttributeSize(mediaType, MF_MT_FRAME_SIZE, &width, &height)) &&
                                width > 0 && height > 0) {
                                m_videoSize = QSize(width, height);
                            }
                            mediaType->Release();
                        }
                    }
                    handler->Release();
                }
                sd->Release();
            }
        }

        pd->Release();
    }

    return true;
}

bool MediaFoundationPlayer::createTopology()
{
    if (!m_session || !m_source) return false;

    HRESULT hr = S_OK;
    IMFTopology *topology = nullptr;
    IMFPresentationDescriptor *pd = nullptr;

    // Create topology
    hr = MFCreateTopology(&topology);
    if (FAILED(hr)) {
        qWarning() << "MediaFoundationPlayer: Failed to create topology:" << Qt::hex << hr;
        return false;
    }

    // Get presentation descriptor
    hr = m_source->CreatePresentationDescriptor(&pd);
    if (FAILED(hr)) {
        topology->Release();
        qWarning() << "MediaFoundationPlayer: Failed to get presentation descriptor:" << Qt::hex << hr;
        return false;
    }

    // Add streams to topology
    DWORD streamCount = 0;
    pd->GetStreamDescriptorCount(&streamCount);
    qDebug() << "MediaFoundationPlayer: Creating topology, streams:" << streamCount;
    bool videoNodeAdded = false;

    for (DWORD i = 0; i < streamCount; i++) {
        BOOL selected = FALSE;
        IMFStreamDescriptor *sd = nullptr;

        hr = pd->GetStreamDescriptorByIndex(i, &selected, &sd);
        if (FAILED(hr)) {
            if (sd) sd->Release();
            continue;
        }
        if (!selected) {
            hr = pd->SelectStream(i);
            if (FAILED(hr)) {
                qWarning() << "MediaFoundationPlayer: Failed to select stream" << i << ":" << Qt::hex << hr;
                sd->Release();
                continue;
            }
        }

        IMFMediaTypeHandler *handler = nullptr;
        hr = sd->GetMediaTypeHandler(&handler);
        if (FAILED(hr)) {
            sd->Release();
            continue;
        }

        GUID majorType;
        hr = handler->GetMajorType(&majorType);
        if (FAILED(hr)) {
            handler->Release();
            sd->Release();
            continue;
        }

        if (majorType == MFMediaType_Video) {
            m_hasVideo = true;
            if (m_videoSize.isEmpty()) {
                IMFMediaType *mediaType = nullptr;
                HRESULT typeHr = handler->GetCurrentMediaType(&mediaType);
                if (FAILED(typeHr)) {
                    typeHr = handler->GetMediaTypeByIndex(0, &mediaType);
                }
                if (SUCCEEDED(typeHr) && mediaType) {
                    UINT32 width = 0;
                    UINT32 height = 0;
                    if (SUCCEEDED(MFGetAttributeSize(mediaType, MF_MT_FRAME_SIZE, &width, &height)) &&
                        width > 0 && height > 0) {
                        m_videoSize = QSize(width, height);
                    }
                    mediaType->Release();
                }
            }
        }
        handler->Release();

        IMFTopologyNode *sourceNode = nullptr;
        IMFTopologyNode *outputNode = nullptr;

        // Create source node
        if (!addSourceNode(topology, pd, sd, &sourceNode)) {
            sd->Release();
            continue;
        }

        // Create output node based on stream type
        bool nodeCreated = false;
        if (majorType == MFMediaType_Video) {
            nodeCreated = addVideoOutputNode(topology, &outputNode);
            if (nodeCreated) {
                videoNodeAdded = true;
            }
        } else if (majorType == MFMediaType_Audio) {
            nodeCreated = addAudioOutputNode(topology, &outputNode);
        }

        if (nodeCreated && outputNode) {
            // Connect source to output
            sourceNode->ConnectOutput(0, outputNode, 0);
            outputNode->Release();
        }

        sourceNode->Release();
        sd->Release();
    }

    if (!videoNodeAdded) {
        qWarning() << "MediaFoundationPlayer: No video stream available for playback";
        pd->Release();
        topology->Release();
        return false;
    }

    // Set topology on session
    hr = m_session->SetTopology(0, topology);

    pd->Release();
    topology->Release();

    if (FAILED(hr)) {
        qWarning() << "MediaFoundationPlayer: Failed to set topology:" << Qt::hex << hr;
        return false;
    }

    // Wait for topology to be ready with timeout (non-blocking polling)
    qDebug() << "MediaFoundationPlayer: Waiting for topology to be ready...";
    IMFMediaEvent *event = nullptr;
    bool topologyReady = false;

    const int maxWaitMs = 5000;  // 5 second timeout
    const int pollIntervalMs = 50;
    int elapsedMs = 0;

    while (!topologyReady && elapsedMs < maxWaitMs) {
        // Use non-blocking event retrieval
        hr = m_session->GetEvent(MF_EVENT_FLAG_NO_WAIT, &event);

        if (hr == MF_E_NO_EVENTS_AVAILABLE) {
            // No event yet, wait a bit and try again
            Sleep(pollIntervalMs);
            elapsedMs += pollIntervalMs;
            continue;
        }

        if (FAILED(hr)) {
            qWarning() << "MediaFoundationPlayer: GetEvent failed:" << Qt::hex << hr;
            break;
        }

        MediaEventType eventType;
        event->GetType(&eventType);
        qDebug() << "MediaFoundationPlayer: Received event type:" << eventType;

        if (eventType == MESessionTopologyStatus) {
            MF_TOPOSTATUS status;
            hr = event->GetUINT32(MF_EVENT_TOPOLOGY_STATUS, (UINT32*)&status);
            qDebug() << "MediaFoundationPlayer: Topology status:" << status;
            if (SUCCEEDED(hr) && status == MF_TOPOSTATUS_READY) {
                topologyReady = true;
                qDebug() << "MediaFoundationPlayer: Topology ready!";

                // Get audio volume interface
                hr = MFGetService(m_session, MR_POLICY_VOLUME_SERVICE,
                                  IID_PPV_ARGS(&m_audioVolume));
                if (FAILED(hr)) {
                    qDebug() << "MediaFoundationPlayer: Audio volume control not available";
                    m_audioVolume = nullptr;
                }
            }
        } else if (eventType == MESessionClosed) {
            qWarning() << "MediaFoundationPlayer: Session closed event";
            event->Release();
            break;
        } else if (eventType == MEError) {
            HRESULT errorCode = S_OK;
            event->GetStatus(&errorCode);
            qWarning() << "MediaFoundationPlayer: Session error:" << Qt::hex << errorCode;
            event->Release();
            break;
        }

        event->Release();
    }

    if (!topologyReady && elapsedMs >= maxWaitMs) {
        qWarning() << "MediaFoundationPlayer: Topology timeout after" << elapsedMs << "ms";
    }

    qDebug() << "MediaFoundationPlayer: createTopology returning:" << topologyReady;
    return topologyReady;
}

bool MediaFoundationPlayer::addSourceNode(IMFTopology *topology, IMFPresentationDescriptor *pd,
                                          IMFStreamDescriptor *sd, IMFTopologyNode **ppNode)
{
    IMFTopologyNode *node = nullptr;
    HRESULT hr = MFCreateTopologyNode(MF_TOPOLOGY_SOURCESTREAM_NODE, &node);
    if (FAILED(hr)) return false;

    hr = node->SetUnknown(MF_TOPONODE_SOURCE, m_source);
    if (FAILED(hr)) { node->Release(); return false; }

    hr = node->SetUnknown(MF_TOPONODE_PRESENTATION_DESCRIPTOR, pd);
    if (FAILED(hr)) { node->Release(); return false; }

    hr = node->SetUnknown(MF_TOPONODE_STREAM_DESCRIPTOR, sd);
    if (FAILED(hr)) { node->Release(); return false; }

    hr = topology->AddNode(node);
    if (FAILED(hr)) { node->Release(); return false; }

    *ppNode = node;
    return true;
}

bool MediaFoundationPlayer::addVideoOutputNode(IMFTopology *topology, IMFTopologyNode **ppNode)
{
    qDebug() << "MediaFoundationPlayer: Adding video output node, size:" << m_videoSize;
    if (m_videoSize.isEmpty()) {
        qWarning() << "MediaFoundationPlayer: Video size unavailable for sample grabber";
        return false;
    }

    // Create sample grabber sink for video frame extraction
    IMFTopologyNode *node = nullptr;
    IMFMediaType *mediaType = nullptr;
    IMFActivate *activate = nullptr;

    HRESULT hr = MFCreateTopologyNode(MF_TOPOLOGY_OUTPUT_NODE, &node);
    if (FAILED(hr)) return false;

    // Create media type for RGB32 output
    hr = MFCreateMediaType(&mediaType);
    if (FAILED(hr)) { node->Release(); return false; }

    hr = mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (FAILED(hr)) { mediaType->Release(); node->Release(); return false; }

    hr = mediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (FAILED(hr)) { mediaType->Release(); node->Release(); return false; }

    // Must specify frame size for proper format negotiation
    hr = MFSetAttributeSize(mediaType, MF_MT_FRAME_SIZE, m_videoSize.width(), m_videoSize.height());
    if (FAILED(hr)) { mediaType->Release(); node->Release(); return false; }

    // Create sample grabber callback
    m_grabberCallback = new SampleGrabberCallback(this);
    m_grabberCallback->setVideoSize(m_videoSize.width(), m_videoSize.height());

    // Create sample grabber sink activate
    hr = MFCreateSampleGrabberSinkActivate(mediaType, m_grabberCallback, &activate);
    mediaType->Release();

    if (FAILED(hr)) {
        qWarning() << "MediaFoundationPlayer: Failed to create sample grabber:" << Qt::hex << hr;
        node->Release();
        return false;
    }

    // Configure for video processing
    hr = activate->SetUINT32(MF_SAMPLEGRABBERSINK_IGNORE_CLOCK, FALSE);

    hr = node->SetObject(activate);
    activate->Release();

    if (FAILED(hr)) { node->Release(); return false; }

    hr = topology->AddNode(node);
    if (FAILED(hr)) { node->Release(); return false; }

    *ppNode = node;
    return true;
}

bool MediaFoundationPlayer::addAudioOutputNode(IMFTopology *topology, IMFTopologyNode **ppNode)
{
    IMFTopologyNode *node = nullptr;
    IMFActivate *activate = nullptr;

    HRESULT hr = MFCreateTopologyNode(MF_TOPOLOGY_OUTPUT_NODE, &node);
    if (FAILED(hr)) return false;

    // Create SAR (Streaming Audio Renderer) activate
    hr = MFCreateAudioRendererActivate(&activate);
    if (FAILED(hr)) {
        qWarning() << "MediaFoundationPlayer: Failed to create audio renderer:" << Qt::hex << hr;
        node->Release();
        return false;
    }

    hr = node->SetObject(activate);
    activate->Release();

    if (FAILED(hr)) { node->Release(); return false; }

    hr = topology->AddNode(node);
    if (FAILED(hr)) { node->Release(); return false; }

    *ppNode = node;
    return true;
}

void MediaFoundationPlayer::play()
{
    qDebug() << "MediaFoundationPlayer::play() - session:" << (void*)m_session
             << "state:" << (int)m_state;

    if (!m_session) return;

    PROPVARIANT varStart;
    PropVariantInit(&varStart);

    if (m_pendingSeek) {
        varStart.vt = VT_I8;
        varStart.hVal.QuadPart = m_seekPosition * 10000; // ms -> 100ns
        m_pendingSeek = false;
        qDebug() << "MediaFoundationPlayer: Starting from seek position:" << m_seekPosition;
    }

    HRESULT hr = m_session->Start(&GUID_NULL, &varStart);
    PropVariantClear(&varStart);

    qDebug() << "MediaFoundationPlayer: Start() returned:" << Qt::hex << hr;

    if (SUCCEEDED(hr)) {
        setState(State::Playing);
        m_positionTimer->start();
    } else {
        qWarning() << "MediaFoundationPlayer: Failed to start:" << Qt::hex << hr;
    }
}

void MediaFoundationPlayer::pause()
{
    if (!m_session) return;

    HRESULT hr = m_session->Pause();
    if (SUCCEEDED(hr)) {
        setState(State::Paused);
        m_positionTimer->stop();
    }
}

void MediaFoundationPlayer::stop()
{
    if (!m_session) return;

    HRESULT hr = m_session->Stop();
    if (SUCCEEDED(hr)) {
        m_position = 0;
        emit positionChanged(0);
        setState(State::Stopped);
        m_positionTimer->stop();
    }
}

void MediaFoundationPlayer::seek(qint64 positionMs)
{
    if (!m_session) return;

    positionMs = qBound(0LL, positionMs, m_duration);

    if (m_state == State::Playing) {
        // Seek while playing
        PROPVARIANT varStart;
        PropVariantInit(&varStart);
        varStart.vt = VT_I8;
        varStart.hVal.QuadPart = positionMs * 10000; // ms -> 100ns

        HRESULT hr = m_session->Start(&GUID_NULL, &varStart);
        PropVariantClear(&varStart);

        if (FAILED(hr)) {
            qWarning() << "MediaFoundationPlayer: Seek failed:" << Qt::hex << hr;
        }
    } else {
        // Store seek position for next play
        m_pendingSeek = true;
        m_seekPosition = positionMs;
        m_position = positionMs;
        emit positionChanged(positionMs);
    }
}

void MediaFoundationPlayer::setVolume(float volume)
{
    m_volume = qBound(0.0f, volume, 1.0f);
    if (m_audioVolume && !m_muted) {
        m_audioVolume->SetMasterVolume(m_volume);
    }
}

void MediaFoundationPlayer::setMuted(bool muted)
{
    m_muted = muted;
    if (m_audioVolume) {
        m_audioVolume->SetMute(muted);
    }
}

void MediaFoundationPlayer::setPlaybackRate(float rate)
{
    float newRate = qBound(0.25f, rate, 2.0f);
    if (m_playbackRate == newRate) return;

    if (m_session) {
        IMFRateControl *rateControl = nullptr;
        HRESULT hr = MFGetService(m_session, MF_RATE_CONTROL_SERVICE,
                                  IID_PPV_ARGS(&rateControl));
        if (SUCCEEDED(hr)) {
            hr = rateControl->SetRate(FALSE, newRate);
            rateControl->Release();

            if (SUCCEEDED(hr)) {
                m_playbackRate = newRate;
                emit playbackRateChanged(newRate);
            }
        }
    }
}

void MediaFoundationPlayer::updatePosition()
{
    if (!m_session || m_state != State::Playing) return;

    IMFClock *clock = nullptr;
    HRESULT hr = m_session->GetClock(&clock);
    if (FAILED(hr)) return;

    IMFPresentationClock *presentationClock = nullptr;
    hr = clock->QueryInterface(IID_PPV_ARGS(&presentationClock));
    clock->Release();

    if (FAILED(hr)) return;

    MFTIME time = 0;
    hr = presentationClock->GetTime(&time);
    presentationClock->Release();

    if (SUCCEEDED(hr)) {
        qint64 newPosition = time / 10000; // 100ns -> ms
        if (newPosition != m_position) {
            m_position = newPosition;
            emit positionChanged(m_position);
        }

        // Check for end of stream
        if (m_position >= m_duration && m_duration > 0) {
            handleEndOfStream();
        }
    }
}

void MediaFoundationPlayer::handleEndOfStream()
{
    if (m_looping) {
        seek(0);
        play();
    } else {
        stop();
        emit playbackFinished();
    }
}

void MediaFoundationPlayer::onFrameReceived(const QImage &frame, qint64 timestampMs)
{
    Q_UNUSED(timestampMs)
    // Marshal to main thread since this is called from MF worker thread
    QMetaObject::invokeMethod(this, [this, frame]() {
        emit frameReady(frame);
    }, Qt::QueuedConnection);
}

void MediaFoundationPlayer::setState(State newState)
{
    if (m_state != newState) {
        m_state = newState;
        emit stateChanged(newState);
    }
}

// Factory function implementations
IVideoPlayer* createMediaFoundationPlayer(QObject *parent)
{
    return new MediaFoundationPlayer(parent);
}

bool isMediaFoundationAvailable()
{
    // Test if Media Foundation can be initialized
    HRESULT hr = MFStartup(MF_VERSION);
    if (SUCCEEDED(hr)) {
        MFShutdown();
        return true;
    }
    return false;
}

#include "MediaFoundationPlayer_win.moc"

#endif // Q_OS_WIN
