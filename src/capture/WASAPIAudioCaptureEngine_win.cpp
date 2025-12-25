#include "capture/WASAPIAudioCaptureEngine.h"

#ifdef Q_OS_WIN

#include <QDebug>
#include <QElapsedTimer>
#include <QWaitCondition>
#include <QCoreApplication>
#include <chrono>

// Windows headers for WASAPI
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <ks.h>
#include <ksmedia.h>

// WASAPI buffer size in 100-nanosecond units (10ms)
static const REFERENCE_TIME BUFFER_DURATION = 100000;  // 10ms

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
        qDebug() << "CaptureThread::run() START";

        // Initialize COM for this thread (use MTA for audio capture)
        qDebug() << "CaptureThread: Initializing COM...";
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
            qWarning() << "CaptureThread: Failed to initialize COM:" << hr;
            m_engine->m_initSuccess = false;
            m_engine->m_initDone = true;
            return;
        }
        qDebug() << "CaptureThread: COM initialized";

        // ALL COM initialization happens here in the thread
        qDebug() << "CaptureThread: Calling initializeInThread...";
        bool initOk = m_engine->initializeInThread();
        qDebug() << "CaptureThread: initializeInThread returned" << initOk;
        m_engine->m_initSuccess = initOk;
        m_engine->m_initDone = true;

        if (initOk) {
            qDebug() << "CaptureThread: Entering captureLoop...";
            m_engine->captureLoop();
            qDebug() << "CaptureThread: captureLoop returned";
        }

        // Cleanup ALL COM objects in the same thread they were created
        qDebug() << "CaptureThread: Calling cleanupInThread...";
        m_engine->cleanupInThread();
        qDebug() << "CaptureThread: cleanupInThread returned";

        qDebug() << "CaptureThread: Uninitializing COM...";
        CoUninitialize();
        qDebug() << "CaptureThread::run() END";
    }

private:
    WASAPIAudioCaptureEngine *m_engine;
};

WASAPIAudioCaptureEngine::WASAPIAudioCaptureEngine(QObject *parent)
    : IAudioCaptureEngine(parent)
{
    // Do NOT initialize COM here - it will be done in the capture thread
}

WASAPIAudioCaptureEngine::~WASAPIAudioCaptureEngine()
{
    stop();
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
    if (m_running) {
        qWarning() << "WASAPIAudioCaptureEngine: Cannot change source while running";
        return false;
    }
    m_source = source;
    refreshProbedFormat();
    return true;
}

bool WASAPIAudioCaptureEngine::setDevice(const QString &deviceId)
{
    if (m_running) {
        qWarning() << "WASAPIAudioCaptureEngine: Cannot change device while running";
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

    IMMDeviceEnumerator *enumerator = nullptr;
    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        (void**)&enumerator
    );

    if (FAILED(hr) || !enumerator) {
        if (needsUninit) CoUninitialize();
        return devices;
    }

    IMMDeviceCollection *collection = nullptr;
    hr = enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &collection);

    if (FAILED(hr) || !collection) {
        enumerator->Release();
        if (needsUninit) CoUninitialize();
        return devices;
    }

    // Get default device ID
    QString defaultId;
    IMMDevice *defaultDevice = nullptr;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &defaultDevice)) && defaultDevice) {
        LPWSTR devId = nullptr;
        if (SUCCEEDED(defaultDevice->GetId(&devId))) {
            defaultId = QString::fromWCharArray(devId);
            CoTaskMemFree(devId);
        }
        defaultDevice->Release();
    }

    UINT count = 0;
    collection->GetCount(&count);

    for (UINT i = 0; i < count; i++) {
        IMMDevice *device = nullptr;
        if (SUCCEEDED(collection->Item(i, &device))) {
            AudioDevice info;

            LPWSTR deviceId = nullptr;
            if (SUCCEEDED(device->GetId(&deviceId))) {
                info.id = QString::fromWCharArray(deviceId);
                CoTaskMemFree(deviceId);
            }

            IPropertyStore *props = nullptr;
            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props))) {
                PROPVARIANT varName;
                PropVariantInit(&varName);
                if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &varName))) {
                    info.name = QString::fromWCharArray(varName.pwszVal);
                    PropVariantClear(&varName);
                }
                props->Release();
            }

            info.isDefault = (info.id == defaultId);
            devices.append(info);
            device->Release();
        }
    }

    collection->Release();
    enumerator->Release();
    if (needsUninit) CoUninitialize();

    return devices;
}

QString WASAPIAudioCaptureEngine::defaultInputDevice() const
{
    QString id;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool needsUninit = SUCCEEDED(hr);

    IMMDeviceEnumerator *enumerator = nullptr;
    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        (void**)&enumerator
    );

    if (SUCCEEDED(hr) && enumerator) {
        IMMDevice *device = nullptr;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device)) && device) {
            LPWSTR deviceId = nullptr;
            if (SUCCEEDED(device->GetId(&deviceId))) {
                id = QString::fromWCharArray(deviceId);
                CoTaskMemFree(deviceId);
            }
            device->Release();
        }
        enumerator->Release();
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

    if (forLoopback) {
        m_loopbackAudioClient = audioClient;
        m_loopbackCaptureClient = captureClient;
    } else {
        m_micAudioClient = audioClient;
        m_micCaptureClient = captureClient;
    }

    if (forLoopback) {
        updateFormatFromWaveFormat(mixFormat, m_loopbackNativeFormat, m_loopbackFormat);
    } else {
        updateFormatFromWaveFormat(mixFormat, m_micNativeFormat, m_micFormat);
    }
    CoTaskMemFree(mixFormat);

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

bool WASAPIAudioCaptureEngine::updateFormatFromWaveFormat(const void *wfxPtr,
                                                          NativeFormatInfo &nativeFormat,
                                                          AudioFormat &outputFormat) const
{
    if (!wfxPtr) return false;

    const WAVEFORMATEX *wfx = static_cast<const WAVEFORMATEX*>(wfxPtr);

    // Store native format info for conversion
    nativeFormat.bitsPerSample = wfx->wBitsPerSample;
    nativeFormat.channels = wfx->nChannels;
    nativeFormat.sampleRate = wfx->nSamplesPerSec;
    nativeFormat.isFloat = false;

    // Check for WAVEFORMATEXTENSIBLE which contains subformat info
    if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE && wfx->cbSize >= 22) {
        const WAVEFORMATEXTENSIBLE *wfxExt = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wfx);
        if (IsEqualGUID(wfxExt->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
            nativeFormat.isFloat = true;
            qDebug() << "WASAPIAudioCaptureEngine: Native format is IEEE float";
        } else if (IsEqualGUID(wfxExt->SubFormat, KSDATAFORMAT_SUBTYPE_PCM)) {
            qDebug() << "WASAPIAudioCaptureEngine: Native format is PCM";
        }
    } else if (wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        nativeFormat.isFloat = true;
        qDebug() << "WASAPIAudioCaptureEngine: Native format is IEEE float (legacy tag)";
    }

    // Store native sample rate, but we will output 16-bit PCM
    outputFormat.sampleRate = wfx->nSamplesPerSec;
    outputFormat.channels = wfx->nChannels;
    outputFormat.bitsPerSample = 16;  // Always output 16-bit PCM

    qDebug() << "WASAPIAudioCaptureEngine: Native format -"
             << wfx->nSamplesPerSec << "Hz,"
             << wfx->nChannels << "ch,"
             << nativeFormat.bitsPerSample << "bit"
             << (nativeFormat.isFloat ? "(float)" : "(int)");
    qDebug() << "WASAPIAudioCaptureEngine: Output format -"
             << outputFormat.sampleRate << "Hz,"
             << outputFormat.channels << "ch,"
             << outputFormat.bitsPerSample << "bit (PCM)";
    return true;
}

bool WASAPIAudioCaptureEngine::probeFormat(bool forLoopback, const QString &deviceId,
                                           NativeFormatInfo &nativeFormat,
                                           AudioFormat &outputFormat) const
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool needsUninit = SUCCEEDED(hr);

    IMMDeviceEnumerator *enumerator = nullptr;
    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        (void**)&enumerator
    );

    if (FAILED(hr) || !enumerator) {
        if (needsUninit) CoUninitialize();
        return false;
    }

    IMMDevice *device = nullptr;
    if (forLoopback) {
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    } else if (!deviceId.isEmpty()) {
        hr = enumerator->GetDevice(reinterpret_cast<LPCWSTR>(deviceId.utf16()), &device);
    } else {
        hr = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
    }

    if (FAILED(hr) || !device) {
        enumerator->Release();
        if (needsUninit) CoUninitialize();
        return false;
    }

    IAudioClient *audioClient = nullptr;
    hr = device->Activate(
        __uuidof(IAudioClient),
        CLSCTX_ALL,
        nullptr,
        (void**)&audioClient
    );

    if (FAILED(hr) || !audioClient) {
        device->Release();
        enumerator->Release();
        if (needsUninit) CoUninitialize();
        return false;
    }

    WAVEFORMATEX *mixFormat = nullptr;
    hr = audioClient->GetMixFormat(&mixFormat);
    bool ok = SUCCEEDED(hr) && mixFormat;
    if (ok) {
        ok = updateFormatFromWaveFormat(mixFormat, nativeFormat, outputFormat);
        CoTaskMemFree(mixFormat);
    }

    audioClient->Release();
    device->Release();
    enumerator->Release();
    if (needsUninit) CoUninitialize();
    return ok;
}

void WASAPIAudioCaptureEngine::refreshProbedFormat()
{
    if (m_source == AudioSource::None) {
        return;
    }

    bool micOk = false;
    bool loopOk = false;

    if (m_source == AudioSource::Microphone || m_source == AudioSource::Both) {
        micOk = probeFormat(false, m_deviceId, m_micNativeFormat, m_micFormat);
    }
    if (m_source == AudioSource::SystemAudio || m_source == AudioSource::Both) {
        loopOk = probeFormat(true, QString(), m_loopbackNativeFormat, m_loopbackFormat);
    }

    if (m_source == AudioSource::Both && micOk && loopOk) {
        if (m_micFormat.sampleRate == m_loopbackFormat.sampleRate &&
            m_micFormat.channels == m_loopbackFormat.channels) {
            m_format = m_micFormat;
        } else {
            qWarning() << "WASAPIAudioCaptureEngine: Mismatched mic/loopback formats during probe -"
                       << "mic:" << m_micFormat.sampleRate << "Hz," << m_micFormat.channels << "ch"
                       << "loopback:" << m_loopbackFormat.sampleRate << "Hz," << m_loopbackFormat.channels << "ch";
            m_format = m_micFormat;
        }
        return;
    }

    if (micOk) {
        m_format = m_micFormat;
    } else if (loopOk) {
        m_format = m_loopbackFormat;
    }
}

// Called from capture thread to initialize COM objects
bool WASAPIAudioCaptureEngine::initializeInThread()
{
    qDebug() << "WASAPIAudioCaptureEngine: Initializing in thread";

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

    // Set up microphone capture if needed
    if (m_source == AudioSource::Microphone || m_source == AudioSource::Both) {
        m_micDevice = getDevice(m_deviceId, false);
        if (!m_micDevice || !setupAudioClient(m_micDevice, false)) {
            qWarning() << "WASAPIAudioCaptureEngine: Failed to initialize microphone capture";
            return false;
        }
    }

    // Set up loopback capture if needed
    if (m_source == AudioSource::SystemAudio || m_source == AudioSource::Both) {
        m_loopbackDevice = getDefaultDevice(true);
        if (!m_loopbackDevice || !setupAudioClient(m_loopbackDevice, true)) {
            qWarning() << "WASAPIAudioCaptureEngine: Failed to initialize system audio capture";
            return false;
        }
    }

    if (m_micCaptureClient && m_loopbackCaptureClient) {
        if (m_micFormat.sampleRate != m_loopbackFormat.sampleRate ||
            m_micFormat.channels != m_loopbackFormat.channels) {
            qWarning() << "WASAPIAudioCaptureEngine: Mic/loopback formats differ, disabling loopback."
                       << "mic:" << m_micFormat.sampleRate << "Hz," << m_micFormat.channels << "ch"
                       << "loopback:" << m_loopbackFormat.sampleRate << "Hz," << m_loopbackFormat.channels << "ch";
            emit warning("Mic and system audio formats differ; system audio disabled.");
            safeRelease(m_loopbackCaptureClient);
            safeRelease(m_loopbackAudioClient);
            safeRelease(m_loopbackDevice);
            m_format = m_micFormat;
        } else {
            m_format = m_micFormat;
        }
    } else if (m_micCaptureClient) {
        m_format = m_micFormat;
    } else if (m_loopbackCaptureClient) {
        m_format = m_loopbackFormat;
    }

    // Start the audio clients
    if (m_micAudioClient) {
        hr = m_micAudioClient->Start();
        if (FAILED(hr)) {
            qWarning() << "WASAPIAudioCaptureEngine: Failed to start microphone capture:" << hr;
            return false;
        }
    }

    if (m_loopbackAudioClient) {
        hr = m_loopbackAudioClient->Start();
        if (FAILED(hr)) {
            qWarning() << "WASAPIAudioCaptureEngine: Failed to start system audio capture:" << hr;
            return false;
        }
    }

    qDebug() << "WASAPIAudioCaptureEngine: Initialized successfully in thread";
    return true;
}

// Called from capture thread to clean up COM objects
void WASAPIAudioCaptureEngine::cleanupInThread()
{
    qDebug() << "WASAPIAudioCaptureEngine::cleanupInThread() START";

    qDebug() << "WASAPIAudioCaptureEngine: Stopping mic audio client...";
    if (m_micAudioClient) {
        m_micAudioClient->Stop();
    }
    qDebug() << "WASAPIAudioCaptureEngine: Stopping loopback audio client...";
    if (m_loopbackAudioClient) {
        m_loopbackAudioClient->Stop();
    }

    qDebug() << "WASAPIAudioCaptureEngine: Cleaning up audio clients...";
    cleanupAudioClient();
    qDebug() << "WASAPIAudioCaptureEngine: Releasing device enumerator...";
    safeRelease(m_deviceEnumerator);
    qDebug() << "WASAPIAudioCaptureEngine::cleanupInThread() END";
}

bool WASAPIAudioCaptureEngine::start()
{
    if (m_running) {
        qWarning() << "WASAPIAudioCaptureEngine: Already running";
        return false;
    }

    if (m_source == AudioSource::None) {
        qWarning() << "WASAPIAudioCaptureEngine: No audio source configured";
        return false;
    }

    // Initialize timing
    m_startTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    m_pausedDuration = 0;

    // Reset init flags
    m_initDone = false;
    m_initSuccess = false;
    m_stopRequested = false;
    m_running = true;
    m_paused = false;
    m_micPending.clear();
    m_loopbackPending.clear();

    // Start capture thread - it will do ALL COM initialization
    m_captureThread = new CaptureThread(this);
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
            m_captureThread->wait(500);
            delete m_captureThread;
            m_captureThread = nullptr;
        }
        m_running = false;
        return false;
    }

    qDebug() << "WASAPIAudioCaptureEngine: Started with source" << static_cast<int>(m_source);
    return true;
}

void WASAPIAudioCaptureEngine::stop()
{
    qDebug() << "WASAPIAudioCaptureEngine::stop() called, m_running=" << m_running.load();

    if (!m_running) {
        qDebug() << "WASAPIAudioCaptureEngine::stop() - not running, returning early";
        return;
    }

    qDebug() << "WASAPIAudioCaptureEngine: Setting stop flags...";
    m_stopRequested = true;
    m_running = false;
    m_paused = false;

    qDebug() << "WASAPIAudioCaptureEngine: Checking capture thread...";
    if (m_captureThread) {
        qDebug() << "WASAPIAudioCaptureEngine: Thread exists, isRunning=" << m_captureThread->isRunning();

        // Use QThread::wait() with timeout - this is the proper way to wait for a thread
        // Don't use processEvents() as it can cause re-entrancy issues with queued signals
        if (!m_captureThread->wait(2000)) {
            qWarning() << "WASAPIAudioCaptureEngine: Thread did not stop in 2 seconds, terminating";
            m_captureThread->terminate();
            m_captureThread->wait(500);
        }

        qDebug() << "WASAPIAudioCaptureEngine: Deleting thread...";
        delete m_captureThread;
        m_captureThread = nullptr;
        qDebug() << "WASAPIAudioCaptureEngine: Thread deleted";
    } else {
        qDebug() << "WASAPIAudioCaptureEngine: No capture thread to stop";
    }

    qDebug() << "WASAPIAudioCaptureEngine: Stopped successfully";
}

void WASAPIAudioCaptureEngine::pause()
{
    if (!m_running || m_paused) return;

    QMutexLocker locker(&m_timingMutex);
    m_pauseStartTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    m_paused = true;

    qDebug() << "WASAPIAudioCaptureEngine: Paused";
}

void WASAPIAudioCaptureEngine::resume()
{
    if (!m_running || !m_paused) return;

    QMutexLocker locker(&m_timingMutex);
    qint64 now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    m_pausedDuration += (now - m_pauseStartTime);
    m_paused = false;

    qDebug() << "WASAPIAudioCaptureEngine: Resumed";
}

QByteArray WASAPIAudioCaptureEngine::convertToInt16PCM(const unsigned char *data, int numFrames,
                                                       const NativeFormatInfo &nativeFormat) const
{
    // Output: 16-bit PCM, same channels as input
    int outputSize = numFrames * nativeFormat.channels * sizeof(int16_t);
    QByteArray output(outputSize, 0);
    int16_t *outPtr = reinterpret_cast<int16_t*>(output.data());

    if (nativeFormat.isFloat && nativeFormat.bitsPerSample == 32) {
        // Convert 32-bit float [-1.0, 1.0] to 16-bit signed int [-32768, 32767]
        const float *inPtr = reinterpret_cast<const float*>(data);
        int totalSamples = numFrames * nativeFormat.channels;
        for (int i = 0; i < totalSamples; i++) {
            float sample = inPtr[i];
            // Clamp to [-1.0, 1.0]
            if (sample > 1.0f) sample = 1.0f;
            else if (sample < -1.0f) sample = -1.0f;
            outPtr[i] = static_cast<int16_t>(sample * 32767.0f);
        }
    } else if (!nativeFormat.isFloat && nativeFormat.bitsPerSample == 32) {
        // Convert 32-bit int to 16-bit int (shift right by 16)
        const int32_t *inPtr = reinterpret_cast<const int32_t*>(data);
        int totalSamples = numFrames * nativeFormat.channels;
        for (int i = 0; i < totalSamples; i++) {
            outPtr[i] = static_cast<int16_t>(inPtr[i] >> 16);
        }
    } else if (!nativeFormat.isFloat && nativeFormat.bitsPerSample == 24) {
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
    } else if (!nativeFormat.isFloat && nativeFormat.bitsPerSample == 16) {
        // Already 16-bit PCM, just copy
        memcpy(output.data(), data, outputSize);
    } else {
        // Unsupported format - fill with silence and log warning
        qWarning() << "WASAPIAudioCaptureEngine: Unsupported audio format -"
                   << nativeFormat.bitsPerSample << "bit"
                   << (nativeFormat.isFloat ? "float" : "int");
        output.fill(0);
    }

    return output;
}

void WASAPIAudioCaptureEngine::captureLoop()
{
    qDebug() << "WASAPIAudioCaptureEngine::captureLoop() START";

    int loopIterations = 0;
    while (!m_stopRequested) {
        loopIterations++;
        if (loopIterations % 100 == 0) {
            qDebug() << "WASAPIAudioCaptureEngine: captureLoop iteration" << loopIterations << ", m_stopRequested=" << m_stopRequested.load();
        }
        // If paused, just sleep and continue
        if (m_paused) {
            QThread::msleep(10);
            continue;
        }

        bool gotData = false;

        bool useMixing = (m_micCaptureClient && m_loopbackCaptureClient);

        // Capture from microphone
        if (m_micCaptureClient) {
            UINT32 packetLength = 0;
            HRESULT hr = m_micCaptureClient->GetNextPacketSize(&packetLength);

            while (SUCCEEDED(hr) && packetLength > 0 && !m_stopRequested) {
                BYTE *data = nullptr;
                UINT32 numFrames = 0;
                DWORD flags = 0;

                hr = m_micCaptureClient->GetBuffer(&data, &numFrames, &flags, nullptr, nullptr);
                if (SUCCEEDED(hr) && numFrames > 0) {
                    QByteArray audioData;

                    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                        // Output silence in 16-bit PCM format
                        int outputSize = numFrames * m_micNativeFormat.channels * sizeof(int16_t);
                        audioData.resize(outputSize);
                        audioData.fill(0);
                    } else {
                        // Convert from native format to 16-bit PCM
                        audioData = convertToInt16PCM(data, numFrames, m_micNativeFormat);
                    }

                    if (useMixing) {
                        m_micPending.append(audioData);
                    } else {
                        qint64 now = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()).count();

                        QMutexLocker locker(&m_timingMutex);
                        qint64 timestamp = now - m_startTime - m_pausedDuration;
                        locker.unlock();

                        emit audioDataReady(audioData, timestamp);
                    }
                    gotData = true;

                    m_micCaptureClient->ReleaseBuffer(numFrames);
                }

                hr = m_micCaptureClient->GetNextPacketSize(&packetLength);
            }
        }

        // Capture from loopback
        if (m_loopbackCaptureClient) {
            UINT32 packetLength = 0;
            HRESULT hr = m_loopbackCaptureClient->GetNextPacketSize(&packetLength);

            while (SUCCEEDED(hr) && packetLength > 0 && !m_stopRequested) {
                BYTE *data = nullptr;
                UINT32 numFrames = 0;
                DWORD flags = 0;

                hr = m_loopbackCaptureClient->GetBuffer(&data, &numFrames, &flags, nullptr, nullptr);
                if (SUCCEEDED(hr) && numFrames > 0) {
                    QByteArray audioData;

                    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                        // Output silence in 16-bit PCM format
                        int outputSize = numFrames * m_loopbackNativeFormat.channels * sizeof(int16_t);
                        audioData.resize(outputSize);
                        audioData.fill(0);
                    } else {
                        // Convert from native format to 16-bit PCM
                        audioData = convertToInt16PCM(data, numFrames, m_loopbackNativeFormat);
                    }

                    if (useMixing) {
                        m_loopbackPending.append(audioData);
                    } else {
                        qint64 now = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()).count();

                        QMutexLocker locker(&m_timingMutex);
                        qint64 timestamp = now - m_startTime - m_pausedDuration;
                        locker.unlock();

                        emit audioDataReady(audioData, timestamp);
                    }
                    gotData = true;

                    m_loopbackCaptureClient->ReleaseBuffer(numFrames);
                }

                hr = m_loopbackCaptureClient->GetNextPacketSize(&packetLength);
            }
        }

        if (useMixing && !m_micPending.isEmpty() && !m_loopbackPending.isEmpty()) {
            int bytesPerFrame = m_format.channels * sizeof(int16_t);
            int mixBytes = qMin(m_micPending.size(), m_loopbackPending.size());
            mixBytes = (mixBytes / bytesPerFrame) * bytesPerFrame;

            if (mixBytes > 0) {
                QByteArray mixed(mixBytes, 0);
                const int16_t *micPtr = reinterpret_cast<const int16_t*>(m_micPending.constData());
                const int16_t *loopPtr = reinterpret_cast<const int16_t*>(m_loopbackPending.constData());
                int16_t *outPtr = reinterpret_cast<int16_t*>(mixed.data());
                int totalSamples = mixBytes / sizeof(int16_t);

                for (int i = 0; i < totalSamples; i++) {
                    int sample = static_cast<int>(micPtr[i]) + static_cast<int>(loopPtr[i]);
                    if (sample > 32767) sample = 32767;
                    else if (sample < -32768) sample = -32768;
                    outPtr[i] = static_cast<int16_t>(sample);
                }

                m_micPending.remove(0, mixBytes);
                m_loopbackPending.remove(0, mixBytes);

                qint64 now = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                QMutexLocker locker(&m_timingMutex);
                qint64 timestamp = now - m_startTime - m_pausedDuration;
                locker.unlock();

                emit audioDataReady(mixed, timestamp);
                gotData = true;
            }
        }

        // If no data was available, sleep briefly
        if (!gotData) {
            QThread::msleep(5);
        }
    }

    qDebug() << "WASAPIAudioCaptureEngine::captureLoop() END, total iterations=" << loopIterations << ", m_stopRequested=" << m_stopRequested.load();
}

#endif // Q_OS_WIN
