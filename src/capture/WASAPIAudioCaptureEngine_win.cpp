#include "capture/WASAPIAudioCaptureEngine.h"

#ifdef Q_OS_WIN

#include <QDebug>
#include <QScopeGuard>
#include <chrono>
#include <cmath>
#include <cstring>

// Windows headers for WASAPI
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <ks.h>
#include <ksmedia.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// WASAPI buffer size in 100-nanosecond units (10ms)
static const REFERENCE_TIME BUFFER_DURATION = 100000;  // 10ms
static constexpr qint64 HUNDRED_NS_PER_SECOND = 10000000;
static constexpr qint64 NS_PER_HUNDRED_NS = 100;
static constexpr qint64 NANOSECONDS_PER_SECOND = 1000000000;
static constexpr qint64 NANOSECONDS_PER_MILLISECOND = 1000000;
static constexpr qint64 AUDIO_ADVANCE_LAG_NS = 250 * NANOSECONDS_PER_MILLISECOND;
static constexpr unsigned long DISPOSE_FLUSH_WAIT_MS = 100;

static qint64 queryPerformanceCounter100ns()
{
    LARGE_INTEGER counter{};
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceCounter(&counter)
        || !QueryPerformanceFrequency(&frequency)
        || frequency.QuadPart <= 0) {
        return 0;
    }

    const qint64 wholeSeconds = counter.QuadPart / frequency.QuadPart;
    const qint64 remainder = counter.QuadPart % frequency.QuadPart;
    return wholeSeconds * HUNDRED_NS_PER_SECOND
        + remainder * HUNDRED_NS_PER_SECOND / frequency.QuadPart;
}

// Helper to safely release COM objects
template<typename T>
void safeRelease(T*& ptr) {
    if (ptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

// Helper class to run capture loop in a thread
// ALL COM operations happen in this thread to avoid cross-apartment issues
class WASAPIAudioCaptureEngine::CaptureThread : public QThread
{
public:
    CaptureThread(WASAPIAudioCaptureEngine *engine) : m_engine(engine) {}

protected:
    void run() override {
        // Initialize COM for this thread (use MTA for audio capture)
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
            qWarning() << "CaptureThread: Failed to initialize COM:" << hr;
            m_engine->m_initSuccess = false;
            m_engine->m_initDone = true;
            return;
        }

        // ALL COM initialization happens here in the thread
        bool initOk = m_engine->initializeInThread();
        m_engine->m_initSuccess = initOk;
        m_engine->m_initDone = true;

        if (initOk) {
            m_engine->captureLoop();
        }

        // Cleanup ALL COM objects in the same thread they were created
        m_engine->cleanupInThread();
        CoUninitialize();
    }

private:
    WASAPIAudioCaptureEngine *m_engine;
};

WASAPIAudioCaptureEngine::WASAPIAudioCaptureEngine(QObject *parent)
    : IAudioCaptureEngine(parent)
{
    // Do NOT initialize COM here - it will be done in the capture thread
    m_format = SnapTray::Audio::TimestampedPcmMixer::outputFormat();
}

WASAPIAudioCaptureEngine::~WASAPIAudioCaptureEngine()
{
    // disposeAsync() is the ownership boundary for a live capture thread: it
    // retains the engine until CaptureThread::finished. Reaching the destructor
    // with a running thread would violate that contract and cannot be repaired
    // here without either blocking indefinitely or creating a use-after-free.
    Q_ASSERT(!m_captureThread || !m_captureThread->isRunning());
    releaseCaptureThreadIfFinished();
}

bool WASAPIAudioCaptureEngine::initializeCOM()
{
    // This is now a no-op - COM is initialized in the capture thread
    return true;
}

void WASAPIAudioCaptureEngine::uninitializeCOM()
{
    // This is now a no-op - COM is uninitialized in the capture thread
}

bool WASAPIAudioCaptureEngine::isAvailable() const
{
    // Always return true - we'll check availability when starting
    return true;
}

bool WASAPIAudioCaptureEngine::setAudioSource(AudioSource source)
{
    if (m_running || (m_captureThread && m_captureThread->isRunning())) {
        return false;
    }
    m_source = source;
    refreshProbedFormat();
    return true;
}

bool WASAPIAudioCaptureEngine::setDevice(const QString &deviceId)
{
    if (m_running || (m_captureThread && m_captureThread->isRunning())) {
        return false;
    }
    m_deviceId = deviceId;
    refreshProbedFormat();
    return true;
}

QList<IAudioCaptureEngine::AudioDevice> WASAPIAudioCaptureEngine::availableInputDevices() const
{
    QList<AudioDevice> devices;

    // Initialize COM temporarily for device enumeration
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool needsUninit = SUCCEEDED(hr);

    ComPtr<IMMDeviceEnumerator> enumerator;
    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        &enumerator
    );

    if (FAILED(hr) || !enumerator) {
        if (needsUninit) CoUninitialize();
        return devices;
    }

    ComPtr<IMMDeviceCollection> collection;
    hr = enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &collection);

    if (FAILED(hr) || !collection) {
        if (needsUninit) CoUninitialize();
        return devices;
    }

    // Get default device ID
    QString defaultId;
    ComPtr<IMMDevice> defaultDevice;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &defaultDevice)) && defaultDevice) {
        LPWSTR devId = nullptr;
        if (SUCCEEDED(defaultDevice->GetId(&devId))) {
            defaultId = QString::fromWCharArray(devId);
            CoTaskMemFree(devId);
        }
    }

    UINT count = 0;
    collection->GetCount(&count);

    for (UINT i = 0; i < count; i++) {
        ComPtr<IMMDevice> device;
        if (SUCCEEDED(collection->Item(i, &device))) {
            AudioDevice info;

            LPWSTR deviceId = nullptr;
            if (SUCCEEDED(device->GetId(&deviceId))) {
                info.id = QString::fromWCharArray(deviceId);
                CoTaskMemFree(deviceId);
            }

            ComPtr<IPropertyStore> props;
            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props))) {
                PROPVARIANT varName;
                PropVariantInit(&varName);
                if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &varName))) {
                    info.name = QString::fromWCharArray(varName.pwszVal);
                    PropVariantClear(&varName);
                }
            }

            info.isDefault = (info.id == defaultId);
            devices.append(info);
        }
    }

    if (needsUninit) CoUninitialize();

    return devices;
}

QString WASAPIAudioCaptureEngine::defaultInputDevice() const
{
    QString id;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool needsUninit = SUCCEEDED(hr);

    ComPtr<IMMDeviceEnumerator> enumerator;
    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        &enumerator
    );

    if (SUCCEEDED(hr) && enumerator) {
        ComPtr<IMMDevice> device;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device)) && device) {
            LPWSTR deviceId = nullptr;
            if (SUCCEEDED(device->GetId(&deviceId))) {
                id = QString::fromWCharArray(deviceId);
                CoTaskMemFree(deviceId);
            }
        }
    }

    if (needsUninit) CoUninitialize();
    return id;
}

// These methods are now called from within the capture thread
IMMDevice* WASAPIAudioCaptureEngine::getDevice(const QString &deviceId, bool forLoopback) const
{
    if (!m_deviceEnumerator) return nullptr;

    if (deviceId.isEmpty()) {
        return getDefaultDevice(forLoopback);
    }

    IMMDevice *device = nullptr;
    HRESULT hr = m_deviceEnumerator->GetDevice(
        reinterpret_cast<LPCWSTR>(deviceId.utf16()), &device);

    if (FAILED(hr)) {
        qWarning() << "WASAPIAudioCaptureEngine: Failed to get device:" << deviceId;
        return nullptr;
    }

    return device;
}

IMMDevice* WASAPIAudioCaptureEngine::getDefaultDevice(bool forLoopback) const
{
    if (!m_deviceEnumerator) return nullptr;

    IMMDevice *device = nullptr;
    EDataFlow dataFlow = forLoopback ? eRender : eCapture;

    HRESULT hr = m_deviceEnumerator->GetDefaultAudioEndpoint(dataFlow, eConsole, &device);

    if (FAILED(hr)) {
        qWarning() << "WASAPIAudioCaptureEngine: Failed to get default device";
        return nullptr;
    }

    return device;
}

bool WASAPIAudioCaptureEngine::setupAudioClient(IMMDevice *device, bool forLoopback)
{
    if (!device) return false;

    IAudioClient *audioClient = nullptr;
    HRESULT hr = device->Activate(
        __uuidof(IAudioClient),
        CLSCTX_ALL,
        nullptr,
        (void**)&audioClient
    );

    if (FAILED(hr)) {
        qWarning() << "WASAPIAudioCaptureEngine: Failed to activate audio client:" << hr;
        return false;
    }

    WAVEFORMATEX *mixFormat = nullptr;
    hr = audioClient->GetMixFormat(&mixFormat);
    if (FAILED(hr)) {
        qWarning() << "WASAPIAudioCaptureEngine: Failed to get mix format";
        audioClient->Release();
        return false;
    }

    DWORD streamFlags = forLoopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0;
    hr = audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        streamFlags,
        BUFFER_DURATION,
        0,
        mixFormat,
        nullptr
    );

    if (FAILED(hr)) {
        qWarning() << "WASAPIAudioCaptureEngine: Failed to initialize audio client:" << hr;
        CoTaskMemFree(mixFormat);
        audioClient->Release();
        return false;
    }

    IAudioCaptureClient *captureClient = nullptr;
    hr = audioClient->GetService(
        __uuidof(IAudioCaptureClient),
        (void**)&captureClient
    );

    if (FAILED(hr)) {
        qWarning() << "WASAPIAudioCaptureEngine: Failed to get capture client:" << hr;
        CoTaskMemFree(mixFormat);
        audioClient->Release();
        return false;
    }

    NativeFormatInfo parsedNativeFormat;
    AudioFormat parsedOutputFormat;
    if (!updateFormatFromWaveFormat(mixFormat, parsedNativeFormat, parsedOutputFormat)) {
        qWarning() << "WASAPIAudioCaptureEngine: Native endpoint format is unsupported";
        CoTaskMemFree(mixFormat);
        captureClient->Release();
        audioClient->Release();
        return false;
    }
    CoTaskMemFree(mixFormat);

    if (forLoopback) {
        m_loopbackAudioClient = audioClient;
        m_loopbackCaptureClient = captureClient;
        m_loopbackNativeFormat = parsedNativeFormat;
    } else {
        m_micAudioClient = audioClient;
        m_micCaptureClient = captureClient;
        m_micNativeFormat = parsedNativeFormat;
    }

    return true;
}

void WASAPIAudioCaptureEngine::cleanupAudioClient()
{
    safeRelease(m_micCaptureClient);
    safeRelease(m_micAudioClient);
    safeRelease(m_micDevice);

    safeRelease(m_loopbackCaptureClient);
    safeRelease(m_loopbackAudioClient);
    safeRelease(m_loopbackDevice);
}

void WASAPIAudioCaptureEngine::cleanupSource(SnapTray::Audio::Source source)
{
    if (source == SnapTray::Audio::Source::Microphone) {
        if (m_micAudioClient) {
            m_micAudioClient->Stop();
        }
        safeRelease(m_micCaptureClient);
        safeRelease(m_micAudioClient);
        safeRelease(m_micDevice);
    } else {
        if (m_loopbackAudioClient) {
            m_loopbackAudioClient->Stop();
        }
        safeRelease(m_loopbackCaptureClient);
        safeRelease(m_loopbackAudioClient);
        safeRelease(m_loopbackDevice);
    }
}

IAudioCaptureEngine::AudioSource WASAPIAudioCaptureEngine::activeAudioSource() const
{
    const bool microphone = m_microphoneActive.load();
    const bool systemAudio = m_systemAudioActive.load();
    if (microphone && systemAudio) {
        return AudioSource::Both;
    }
    if (microphone) {
        return AudioSource::Microphone;
    }
    if (systemAudio) {
        return AudioSource::SystemAudio;
    }
    return AudioSource::None;
}

void WASAPIAudioCaptureEngine::notifyActiveSourceChanged()
{
    const AudioSource activeSource = activeAudioSource();
    const int activeValue = static_cast<int>(activeSource);
    if (m_lastNotifiedActiveSource.exchange(activeValue) != activeValue) {
        emit activeSourceChanged(activeSource);
    }
}

qint64 WASAPIAudioCaptureEngine::currentActiveTimeNs() const
{
    const qint64 now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    QMutexLocker locker(&m_timingMutex);
    const qint64 activeNow = m_paused ? m_pauseStartTime : now;
    return qMax<qint64>(
        qint64(0),
        (activeNow - m_startTime - m_pausedDuration)
            * NANOSECONDS_PER_MILLISECOND);
}

qint64 WASAPIAudioCaptureEngine::packetTimestampNs(
    quint64 qpcPosition100ns,
    quint32 flags,
    int numFrames,
    const NativeFormatInfo& nativeFormat) const
{
    qint64 pausedDuration = 0;
    {
        QMutexLocker locker(&m_timingMutex);
        pausedDuration = m_pausedDuration;
    }

    if (!(flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)
        && m_startQpc100ns > 0
        && qpcPosition100ns >= static_cast<quint64>(m_startQpc100ns)) {
        const qint64 timestamp100ns = static_cast<qint64>(qpcPosition100ns)
            - m_startQpc100ns
            - pausedDuration * (HUNDRED_NS_PER_SECOND / 1000);
        if (timestamp100ns >= 0) {
            return timestamp100ns * NS_PER_HUNDRED_NS;
        }
    }

    const qint64 durationNs = nativeFormat.sampleRate > 0
        ? static_cast<qint64>(numFrames) * NANOSECONDS_PER_SECOND
            / nativeFormat.sampleRate
        : 0;
    return qMax<qint64>(qint64(0), currentActiveTimeNs() - durationNs);
}

void WASAPIAudioCaptureEngine::deliverMixerOutput(
    const SnapTray::Audio::TimestampedPcmMixer::ProcessResult& result)
{
    for (const auto& output : result.output) {
        if (!output.pcm.isEmpty()) {
            deliverAudioData(output.pcm, output.startFrame);
        }
    }

    if (result.code == SnapTray::Audio::TimestampedPcmMixer::ResultCode::FutureTimestamp
        && !m_reportedMixerDrop.exchange(true)) {
        emit warning("An audio source reported an invalid timestamp and was skipped.");
    } else if ((result.code == SnapTray::Audio::TimestampedPcmMixer::ResultCode::AcceptedWithDrops
         || result.code == SnapTray::Audio::TimestampedPcmMixer::ResultCode::TooLate)
        && !m_reportedMixerDrop.exchange(true)) {
        emit warning("Audio capture fell behind; a short section may be silent.");
    }
}

void WASAPIAudioCaptureEngine::processAudioPacket(
    SnapTray::Audio::Source source,
    const QByteArray& pcm,
    qint64 timestampNs,
    const NativeFormatInfo& nativeFormat)
{
    processCapturedAudioPacket(
        source,
        pcm,
        timestampNs,
        nativeFormat,
        m_pauseGeneration.load());
}

bool WASAPIAudioCaptureEngine::processCapturedAudioPacket(
    SnapTray::Audio::Source source,
    const QByteArray& pcm,
    qint64 timestampNs,
    const NativeFormatInfo& nativeFormat,
    quint64 pauseGeneration)
{
    QMutexLocker packetGateLocker(&m_packetGate);
    if (m_paused || pauseGeneration != m_pauseGeneration.load()) {
        return false;
    }
    processAudioPacketWhileGateHeld(source, pcm, timestampNs, nativeFormat);
    return true;
}

void WASAPIAudioCaptureEngine::processAudioPacketWhileGateHeld(
    SnapTray::Audio::Source source,
    const QByteArray& pcm,
    qint64 timestampNs,
    const NativeFormatInfo& nativeFormat)
{
    const qint64 activeTimeNs = currentActiveTimeNs();
    QMutexLocker mixerLocker(&m_mixerDeliveryMutex);
    if (!m_mixer || pcm.isEmpty()) {
        return;
    }

    SnapTray::Audio::Pcm16Format format;
    format.sampleRate = nativeFormat.sampleRate;
    format.channels = nativeFormat.channels;
    format.byteOrder = SnapTray::Audio::ByteOrder::LittleEndian;
    format.signedSamples = true;
    format.interleaved = true;
    deliverMixerOutput(m_mixer->push(
        source,
        {pcm, qMax<qint64>(qint64(0), timestampNs), format},
        activeTimeNs));
}

void WASAPIAudioCaptureEngine::advanceMixerTimeline(qint64 activeTimeNs)
{
    const AudioSource activeSource = activeAudioSource();
    if (activeSource == AudioSource::None) {
        return;
    }

    const qint64 lagNs = activeSource == AudioSource::Both
        ? 0
        : AUDIO_ADVANCE_LAG_NS;
    const qint64 targetTimeNs = qMax<qint64>(
        qint64(0),
        activeTimeNs - lagNs);

    QMutexLocker mixerLocker(&m_mixerDeliveryMutex);
    if (m_mixer) {
        deliverMixerOutput(m_mixer->advanceTo(targetTimeNs));
    }
}

void WASAPIAudioCaptureEngine::handleSourceFailure(
    SnapTray::Audio::Source source,
    long errorCode,
    qint64 effectiveTimeNs)
{
    std::atomic<bool>& active = source == SnapTray::Audio::Source::Microphone
        ? m_microphoneActive
        : m_systemAudioActive;
    if (!active.exchange(false)) {
        return;
    }

    qWarning() << "WASAPIAudioCaptureEngine: Disabling"
               << (source == SnapTray::Audio::Source::Microphone
                       ? "microphone" : "system audio")
               << "after WASAPI failure:" << Qt::hex << errorCode << Qt::dec;
    cleanupSource(source);
    {
        QMutexLocker mixerLocker(&m_mixerDeliveryMutex);
        if (m_mixer) {
            deliverMixerOutput(m_mixer->setSourceEnabled(
                source,
                false,
                qMax<qint64>(qint64(0), effectiveTimeNs)));
        }
    }
    emit deviceLost();
    notifyActiveSourceChanged();

    if (activeAudioSource() == AudioSource::None) {
        m_stopActiveTimeNs = qMax<qint64>(qint64(0), effectiveTimeNs);
        m_running = false;
        m_stopRequested = true;
    }
}

bool WASAPIAudioCaptureEngine::updateFormatFromWaveFormat(const void *wfxPtr,
                                                          NativeFormatInfo &nativeFormat,
                                                          AudioFormat &outputFormat) const
{
    if (!wfxPtr) return false;

    const WAVEFORMATEX *wfx = static_cast<const WAVEFORMATEX*>(wfxPtr);
    if (wfx->nChannels == 0 || wfx->nChannels > 32
        || wfx->nSamplesPerSec == 0 || wfx->nSamplesPerSec > 384000
        || wfx->nBlockAlign == 0 || wfx->wBitsPerSample == 0
        || wfx->nBlockAlign % wfx->nChannels != 0) {
        return false;
    }

    nativeFormat = {};
    nativeFormat.bitsPerSample = wfx->wBitsPerSample;
    nativeFormat.bytesPerSample = wfx->nBlockAlign / wfx->nChannels;
    nativeFormat.channels = wfx->nChannels;
    nativeFormat.sampleRate = wfx->nSamplesPerSec;

    bool isPcm = false;
    bool isFloat = false;
    if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE && wfx->cbSize >= 22) {
        const WAVEFORMATEXTENSIBLE *wfxExt = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wfx);
        isFloat = IsEqualGUID(wfxExt->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
        isPcm = IsEqualGUID(wfxExt->SubFormat, KSDATAFORMAT_SUBTYPE_PCM);
    } else if (wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        isFloat = true;
    } else if (wfx->wFormatTag == WAVE_FORMAT_PCM) {
        isPcm = true;
    }

    if (isFloat && nativeFormat.bitsPerSample == 32
        && nativeFormat.bytesPerSample == 4) {
        nativeFormat.encoding = NativeFormatInfo::Encoding::Float;
    } else if (isPcm && nativeFormat.bitsPerSample == 8
               && nativeFormat.bytesPerSample == 1) {
        nativeFormat.encoding = NativeFormatInfo::Encoding::UnsignedInteger;
    } else if (isPcm
               && ((nativeFormat.bitsPerSample == 16 && nativeFormat.bytesPerSample == 2)
                   || (nativeFormat.bitsPerSample == 24 && nativeFormat.bytesPerSample == 3)
                   || (nativeFormat.bitsPerSample == 32 && nativeFormat.bytesPerSample == 4))) {
        nativeFormat.encoding = NativeFormatInfo::Encoding::SignedInteger;
    } else {
        qWarning() << "WASAPIAudioCaptureEngine: Unsupported native format - tag"
                   << wfx->wFormatTag << "," << nativeFormat.bitsPerSample << "bit,"
                   << nativeFormat.bytesPerSample << "bytes/sample";
        return false;
    }

    outputFormat.sampleRate = wfx->nSamplesPerSec;
    outputFormat.channels = wfx->nChannels;
    outputFormat.bitsPerSample = 16;

    return true;
}

void WASAPIAudioCaptureEngine::refreshProbedFormat()
{
    // Every source is normalized by TimestampedPcmMixer before delivery, so
    // the public format is stable and independent of the selected endpoints.
    m_format = SnapTray::Audio::TimestampedPcmMixer::outputFormat();
}

// Called from capture thread to initialize COM objects
bool WASAPIAudioCaptureEngine::initializeInThread()
{
    // Create device enumerator
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        (void**)&m_deviceEnumerator
    );

    if (FAILED(hr)) {
        qWarning() << "WASAPIAudioCaptureEngine: Failed to create device enumerator:" << hr;
        return false;
    }

    const bool wantsMicrophone = m_source == AudioSource::Microphone
        || m_source == AudioSource::Both;
    const bool wantsSystemAudio = m_source == AudioSource::SystemAudio
        || m_source == AudioSource::Both;
    const auto disableMixerSource = [this](SnapTray::Audio::Source source) {
        QMutexLocker mixerLocker(&m_mixerDeliveryMutex);
        if (m_mixer) {
            deliverMixerOutput(m_mixer->setSourceEnabled(source, false, 0));
        }
    };

    if (wantsMicrophone) {
        m_micDevice = getDevice(m_deviceId, false);
        if (!m_micDevice || !setupAudioClient(m_micDevice, false)) {
            qWarning() << "WASAPIAudioCaptureEngine: Failed to initialize microphone capture";
            cleanupSource(SnapTray::Audio::Source::Microphone);
            disableMixerSource(SnapTray::Audio::Source::Microphone);
        }
    }

    if (wantsSystemAudio) {
        m_loopbackDevice = getDefaultDevice(true);
        if (!m_loopbackDevice || !setupAudioClient(m_loopbackDevice, true)) {
            qWarning() << "WASAPIAudioCaptureEngine: Failed to initialize system audio capture";
            cleanupSource(SnapTray::Audio::Source::SystemAudio);
            disableMixerSource(SnapTray::Audio::Source::SystemAudio);
        }
    }

    if (m_micAudioClient) {
        hr = m_micAudioClient->Start();
        if (FAILED(hr)) {
            qWarning() << "WASAPIAudioCaptureEngine: Failed to start microphone capture:" << hr;
            cleanupSource(SnapTray::Audio::Source::Microphone);
            disableMixerSource(SnapTray::Audio::Source::Microphone);
        } else {
            m_microphoneActive = true;
        }
    }

    if (m_loopbackAudioClient) {
        hr = m_loopbackAudioClient->Start();
        if (FAILED(hr)) {
            qWarning() << "WASAPIAudioCaptureEngine: Failed to start system audio capture:" << hr;
            cleanupSource(SnapTray::Audio::Source::SystemAudio);
            disableMixerSource(SnapTray::Audio::Source::SystemAudio);
        } else {
            m_systemAudioActive = true;
        }
    }

    if (activeAudioSource() == AudioSource::None) {
        return false;
    }

    notifyActiveSourceChanged();
    return true;
}

// Called from capture thread to clean up COM objects
void WASAPIAudioCaptureEngine::cleanupInThread()
{
    if (m_micAudioClient) {
        m_micAudioClient->Stop();
    }
    if (m_loopbackAudioClient) {
        m_loopbackAudioClient->Stop();
    }

    cleanupAudioClient();
    safeRelease(m_deviceEnumerator);
}

bool WASAPIAudioCaptureEngine::start()
{
    if (m_running) {
        return false;
    }

    // A previous initialization timeout may still be unwinding inside a COM or
    // audio-driver call. Never replace (and leak) that live thread object.
    if (m_captureThread) {
        if (!releaseCaptureThreadIfFinished()) {
            qWarning() << "WASAPIAudioCaptureEngine: Previous capture thread is still stopping";
            return false;
        }
    }

    if (m_source == AudioSource::None) {
        qWarning() << "WASAPIAudioCaptureEngine: No audio source configured";
        return false;
    }

    // Initialize timing
    m_startTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    m_startQpc100ns = queryPerformanceCounter100ns();
    m_pausedDuration = 0;

    const bool wantsMicrophone = m_source == AudioSource::Microphone
        || m_source == AudioSource::Both;
    const bool wantsSystemAudio = m_source == AudioSource::SystemAudio
        || m_source == AudioSource::Both;
    SnapTray::Audio::TimestampedPcmMixer::Config mixerConfig;
    mixerConfig.microphoneEnabled = wantsMicrophone;
    mixerConfig.systemAudioEnabled = wantsSystemAudio;
    m_mixer = std::make_unique<SnapTray::Audio::TimestampedPcmMixer>(mixerConfig);
    m_microphoneActive = false;
    m_systemAudioActive = false;
    m_lastNotifiedActiveSource = static_cast<int>(m_source);
    m_reportedMixerDrop = false;
    m_stopActiveTimeNs = -1;
    m_format = SnapTray::Audio::TimestampedPcmMixer::outputFormat();

    // Reset init flags
    m_initDone = false;
    m_initSuccess = false;
    m_stopRequested = false;
    m_running = true;
    m_paused = false;
    m_pauseGeneration = 0;
    enableDataCallbacks();

    // Start capture thread - it will do ALL COM initialization
    m_captureThread = new CaptureThread(this);
    connect(m_captureThread, &QThread::finished,
            this, &WASAPIAudioCaptureEngine::onCaptureThreadFinished,
            Qt::QueuedConnection);
    m_captureThread->start();

    // Wait for thread to initialize (with timeout)
    int waitCount = 0;
    while (!m_initDone && waitCount < 100) {  // Max 1 second wait
        QThread::msleep(10);
        waitCount++;
    }

    if (!m_initDone || !m_initSuccess) {
        qWarning() << "WASAPIAudioCaptureEngine: Thread initialization failed";
        m_stopRequested = true;
        if (m_captureThread) {
            if (!m_captureThread->wait(500)) {
                qWarning() << "WASAPIAudioCaptureEngine: Initialization is still unwinding; retaining thread ownership";
            }
            releaseCaptureThreadIfFinished();
        }
        m_running = false;
        drainDataCallbacks(false);
        return false;
    }

    return true;
}

void WASAPIAudioCaptureEngine::stop()
{
    m_stopActiveTimeNs = currentActiveTimeNs();
    m_stopRequested = true;

    if (!m_captureThread) {
        m_running = false;
        m_paused = false;
        drainDataCallbacks(false);
        m_mixer.reset();
        return;
    }

    // Keep the callback gate open while a responsive worker flushes its final
    // mixer tail. A blocked driver call still falls back to bounded teardown.
    const bool stopped = m_captureThread->wait(2000);
    if (!stopped) {
        qWarning() << "WASAPIAudioCaptureEngine: Thread did not stop in 2 seconds; cleanup remains pending";
    }
    m_running = false;
    m_paused = false;
    drainDataCallbacks(false);
    if (stopped) {
        releaseCaptureThreadIfFinished();
        m_mixer.reset();
    }
}

void WASAPIAudioCaptureEngine::disposeAsync()
{
    if (m_disposePending) {
        return;
    }
    m_disposePending = true;

    // Give a responsive worker a short window to flush the canonical mixer
    // tail before disconnecting the encoder. A blocked COM/driver call still
    // takes the asynchronous lifetime path below.
    m_stopActiveTimeNs = currentActiveTimeNs();
    m_stopRequested = true;
    const bool threadStopped = !m_captureThread
        || m_captureThread->wait(DISPOSE_FLUSH_WAIT_MS);
    m_running = false;
    drainDataCallbacks(true);
    m_paused = false;

    if (parent()) {
        setParent(nullptr);
    }

    if (!m_captureThread) {
        m_mixer.reset();
        deleteLater();
        return;
    }

    if (threadStopped) {
        releaseCaptureThreadIfFinished();
        m_mixer.reset();
        deleteLater();
        return;
    }

    // The finished connection is installed before start(), so a fast-exit
    // thread cannot race past connection setup. Queue an explicit finalization
    // as well when the signal was delivered before disposal was requested.
    if (!m_captureThread->isRunning()) {
        QMetaObject::invokeMethod(
            this, &WASAPIAudioCaptureEngine::onCaptureThreadFinished,
            Qt::QueuedConnection);
    }
}

void WASAPIAudioCaptureEngine::onCaptureThreadFinished()
{
    const bool released = releaseCaptureThreadIfFinished();
    if (released && !m_running) {
        m_mixer.reset();
    }
    if (m_disposePending && !m_captureThread) {
        deleteLater();
    }
}

bool WASAPIAudioCaptureEngine::releaseCaptureThreadIfFinished()
{
    if (!m_captureThread) {
        return true;
    }

    if (m_captureThread->isRunning()) {
        return false;
    }

    delete m_captureThread;
    m_captureThread = nullptr;
    return true;
}

void WASAPIAudioCaptureEngine::enableDataCallbacks()
{
    QMutexLocker locker(&m_dataCallbackMutex);
    Q_ASSERT(m_activeDataCallbacks == 0);
    m_acceptingDataCallbacks = true;
}

void WASAPIAudioCaptureEngine::drainDataCallbacks(bool disconnectConnections)
{
    {
        QMutexLocker locker(&m_dataCallbackMutex);
        m_acceptingDataCallbacks = false;
    }

    if (disconnectConnections) {
        // Final disposal prevents Qt from selecting another connection while
        // the capture thread is between packets. A connection already executing
        // is covered by m_activeDataCallbacks below.
        QObject::disconnect(this, nullptr, nullptr, nullptr);
    }

    QMutexLocker locker(&m_dataCallbackMutex);
    while (m_activeDataCallbacks > 0) {
        m_dataCallbacksDrained.wait(&m_dataCallbackMutex);
    }
}

void WASAPIAudioCaptureEngine::deliverAudioData(const QByteArray &data, qint64 startFrame)
{
    {
        QMutexLocker locker(&m_dataCallbackMutex);
        if (!m_acceptingDataCallbacks) {
            return;
        }
        ++m_activeDataCallbacks;
    }

    const auto callbackGuard = qScopeGuard([this]() {
        finishDataCallback();
    });
    emit audioDataReady(data, startFrame);
}

void WASAPIAudioCaptureEngine::finishDataCallback()
{
    QMutexLocker locker(&m_dataCallbackMutex);
    Q_ASSERT(m_activeDataCallbacks > 0);
    --m_activeDataCallbacks;
    if (m_activeDataCallbacks == 0) {
        m_dataCallbacksDrained.wakeAll();
    }
}

void WASAPIAudioCaptureEngine::pause()
{
    QMutexLocker packetGateLocker(&m_packetGate);
    if (!m_running || m_paused) return;

    QMutexLocker locker(&m_timingMutex);
    m_pauseStartTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    m_paused = true;
    m_pauseGeneration.fetch_add(1);
    const qint64 activeTimeNs = qMax<qint64>(
        qint64(0),
        (m_pauseStartTime - m_startTime - m_pausedDuration)
            * NANOSECONDS_PER_MILLISECOND);
    locker.unlock();
    {
        QMutexLocker mixerLocker(&m_mixerDeliveryMutex);
        if (m_mixer) {
            deliverMixerOutput(m_mixer->pause(activeTimeNs));
        }
    }
}

void WASAPIAudioCaptureEngine::resume()
{
    QMutexLocker packetGateLocker(&m_packetGate);
    if (!m_running || !m_paused) return;

    QMutexLocker locker(&m_timingMutex);
    qint64 now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    m_pausedDuration += (now - m_pauseStartTime);
    const qint64 activeTimeNs = qMax<qint64>(
        qint64(0),
        (now - m_startTime - m_pausedDuration)
            * NANOSECONDS_PER_MILLISECOND);
    locker.unlock();
    {
        QMutexLocker mixerLocker(&m_mixerDeliveryMutex);
        if (m_mixer) {
            m_mixer->resume(activeTimeNs);
        }
    }
    m_pauseGeneration.fetch_add(1);
    m_paused = false;
}

QByteArray WASAPIAudioCaptureEngine::convertToInt16PCM(const unsigned char *data, int numFrames,
                                                       const NativeFormatInfo &nativeFormat) const
{
    // Validate input parameters
    if (!data || numFrames <= 0 || nativeFormat.channels <= 0 || nativeFormat.bitsPerSample <= 0) {
        qWarning() << "WASAPIAudioCaptureEngine: Invalid parameters in convertToInt16PCM";
        return QByteArray();
    }

    // Output: 16-bit PCM, same channels as input
    int outputSize = numFrames * nativeFormat.channels * sizeof(int16_t);
    QByteArray output(outputSize, 0);
    int16_t *outPtr = reinterpret_cast<int16_t*>(output.data());

    if (nativeFormat.encoding == NativeFormatInfo::Encoding::Float
        && nativeFormat.bitsPerSample == 32
        && nativeFormat.bytesPerSample == 4) {
        // Convert 32-bit float [-1.0, 1.0] to 16-bit signed int [-32768, 32767]
        const float *inPtr = reinterpret_cast<const float*>(data);
        int totalSamples = numFrames * nativeFormat.channels;
        for (int i = 0; i < totalSamples; i++) {
            float sample = inPtr[i];
            if (!std::isfinite(sample)) sample = 0.0f;
            // Clamp to [-1.0, 1.0]
            if (sample > 1.0f) sample = 1.0f;
            else if (sample < -1.0f) sample = -1.0f;
            outPtr[i] = static_cast<int16_t>(sample * 32767.0f);
        }
    } else if (nativeFormat.encoding == NativeFormatInfo::Encoding::SignedInteger
               && nativeFormat.bitsPerSample == 32
               && nativeFormat.bytesPerSample == 4) {
        // Convert 32-bit int to 16-bit int (shift right by 16)
        const int32_t *inPtr = reinterpret_cast<const int32_t*>(data);
        int totalSamples = numFrames * nativeFormat.channels;
        for (int i = 0; i < totalSamples; i++) {
            outPtr[i] = static_cast<int16_t>(inPtr[i] >> 16);
        }
    } else if (nativeFormat.encoding == NativeFormatInfo::Encoding::SignedInteger
               && nativeFormat.bitsPerSample == 24
               && nativeFormat.bytesPerSample == 3) {
        // Convert 24-bit int (packed) to 16-bit int
        const unsigned char *inPtr = data;
        int totalSamples = numFrames * nativeFormat.channels;
        for (int i = 0; i < totalSamples; i++) {
            // 24-bit samples are stored as 3 bytes, little-endian
            int32_t sample = (static_cast<int32_t>(inPtr[2]) << 24) |
                            (static_cast<int32_t>(inPtr[1]) << 16) |
                            (static_cast<int32_t>(inPtr[0]) << 8);
            // sample is now sign-extended 24-bit in upper 24 bits
            outPtr[i] = static_cast<int16_t>(sample >> 16);
            inPtr += 3;
        }
    } else if (nativeFormat.encoding == NativeFormatInfo::Encoding::SignedInteger
               && nativeFormat.bitsPerSample == 16
               && nativeFormat.bytesPerSample == 2) {
        // Already 16-bit PCM, just copy
        memcpy(output.data(), data, outputSize);
    } else if (nativeFormat.encoding == NativeFormatInfo::Encoding::UnsignedInteger
               && nativeFormat.bitsPerSample == 8
               && nativeFormat.bytesPerSample == 1) {
        const auto *inPtr = reinterpret_cast<const uint8_t*>(data);
        const int totalSamples = numFrames * nativeFormat.channels;
        for (int i = 0; i < totalSamples; ++i) {
            outPtr[i] = static_cast<int16_t>(
                (static_cast<int>(inPtr[i]) - 128) << 8);
        }
    } else {
        qWarning() << "WASAPIAudioCaptureEngine: Unsupported audio format -"
                   << nativeFormat.bitsPerSample << "bit"
                   << static_cast<int>(nativeFormat.encoding);
        return {};
    }

    return output;
}

void WASAPIAudioCaptureEngine::captureLoop()
{
    auto drainSource = [this](
                           SnapTray::Audio::Source source,
                           IAudioCaptureClient *&captureClient,
                           const NativeFormatInfo& nativeFormat) {
        if (!captureClient) {
            return false;
        }

        bool gotData = false;
        while (!m_stopRequested && captureClient) {
            const quint64 pauseGeneration = m_pauseGeneration.load();
            const bool discardForPause = m_paused.load();

            UINT32 packetLength = 0;
            HRESULT hr = captureClient->GetNextPacketSize(&packetLength);
            if (FAILED(hr)) {
                handleSourceFailure(source, hr, currentActiveTimeNs());
                break;
            }
            if (packetLength == 0) {
                break;
            }

            BYTE *data = nullptr;
            UINT32 numFrames = 0;
            DWORD flags = 0;
            UINT64 qpcPosition100ns = 0;
            hr = captureClient->GetBuffer(
                &data,
                &numFrames,
                &flags,
                nullptr,
                &qpcPosition100ns);
            if (FAILED(hr)) {
                handleSourceFailure(source, hr, currentActiveTimeNs());
                break;
            }
            if (hr == AUDCLNT_S_BUFFER_EMPTY || numFrames == 0) {
                packetLength = 0;
                break;
            }

            QByteArray audioData;
            qint64 timestampNs = 0;
            if (!discardForPause && numFrames > 0) {
                timestampNs = packetTimestampNs(
                    qpcPosition100ns,
                    flags,
                    static_cast<int>(numFrames),
                    nativeFormat);
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    audioData.resize(
                        static_cast<qsizetype>(numFrames)
                        * nativeFormat.channels
                        * static_cast<qsizetype>(sizeof(int16_t)));
                    audioData.fill(0);
                } else if (data) {
                    audioData = convertToInt16PCM(
                        data,
                        static_cast<int>(numFrames),
                        nativeFormat);
                }
            }

            const HRESULT releaseHr = captureClient->ReleaseBuffer(numFrames);
            gotData = true;
            if (FAILED(releaseHr)) {
                handleSourceFailure(source, releaseHr, currentActiveTimeNs());
                break;
            }

            if (!discardForPause && numFrames > 0) {
                if (audioData.isEmpty()) {
                    handleSourceFailure(source, E_INVALIDARG, currentActiveTimeNs());
                    break;
                }

                if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) {
                    qWarning() << "WASAPIAudioCaptureEngine: Audio discontinuity from"
                               << (source == SnapTray::Audio::Source::Microphone
                                       ? "microphone" : "system audio");
                }

                processCapturedAudioPacket(
                    source,
                    audioData,
                    timestampNs,
                    nativeFormat,
                    pauseGeneration);
            }
        }
        return gotData;
    };

    while (!m_stopRequested) {
        bool gotData = false;
        gotData = drainSource(
            SnapTray::Audio::Source::Microphone,
            m_micCaptureClient,
            m_micNativeFormat) || gotData;
        gotData = drainSource(
            SnapTray::Audio::Source::SystemAudio,
            m_loopbackCaptureClient,
            m_loopbackNativeFormat) || gotData;

        if (!m_paused.load()) {
            advanceMixerTimeline(currentActiveTimeNs());
        }

        if (!gotData) {
            QThread::msleep(5);
        }
    }

    bool deliverFinalTail = false;
    {
        QMutexLocker callbackLocker(&m_dataCallbackMutex);
        deliverFinalTail = m_acceptingDataCallbacks;
    }
    {
        QMutexLocker mixerLocker(&m_mixerDeliveryMutex);
        if (m_mixer) {
            qint64 endTimeNs = m_stopActiveTimeNs.load();
            if (endTimeNs < 0) {
                endTimeNs = currentActiveTimeNs();
            }
            deliverMixerOutput(m_mixer->flush(
                deliverFinalTail ? endTimeNs : qint64(0)));
        }
    }
    m_microphoneActive = false;
    m_systemAudioActive = false;
}

#endif // Q_OS_WIN
