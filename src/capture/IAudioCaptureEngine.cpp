#include "capture/IAudioCaptureEngine.h"

#ifdef Q_OS_MAC
#include "capture/CoreAudioCaptureEngine.h"
// Forward declarations for Objective-C implementations
extern IAudioCaptureEngine::MicrophonePermission checkMicrophonePermissionMac();
extern void requestMicrophonePermissionMac(std::function<void(bool)> callback);
#endif

#ifdef Q_OS_WIN
#include "capture/WASAPIAudioCaptureEngine.h"
#endif

#include <QDebug>

IAudioCaptureEngine* IAudioCaptureEngine::createBestEngine(QObject *parent)
{
#ifdef Q_OS_WIN
    auto engine = new WASAPIAudioCaptureEngine(parent);
    if (engine->isAvailable()) {
        qDebug() << "IAudioCaptureEngine: Using WASAPI engine";
        return engine;
    }
    qWarning() << "IAudioCaptureEngine: WASAPI engine not available";
    delete engine;
#endif

#ifdef Q_OS_MAC
    auto engine = new CoreAudioCaptureEngine(parent);
    if (engine->isAvailable()) {
        qDebug() << "IAudioCaptureEngine: Using CoreAudio engine";
        return engine;
    }
    qWarning() << "IAudioCaptureEngine: CoreAudio engine not available";
    delete engine;
#endif

    qWarning() << "IAudioCaptureEngine: No audio capture engine available";
    return nullptr;
}

bool IAudioCaptureEngine::isNativeEngineAvailable()
{
#ifdef Q_OS_WIN
    return true;  // WASAPI available on Windows Vista+
#endif

#ifdef Q_OS_MAC
    return true;  // CoreAudio always available on macOS
#endif

    return false;
}

IAudioCaptureEngine::MicrophonePermission IAudioCaptureEngine::checkMicrophonePermission()
{
#ifdef Q_OS_MAC
    return checkMicrophonePermissionMac();
#else
    // Windows doesn't require explicit microphone permission
    return MicrophonePermission::Authorized;
#endif
}

void IAudioCaptureEngine::requestMicrophonePermission(std::function<void(bool granted)> callback)
{
#ifdef Q_OS_MAC
    requestMicrophonePermissionMac(callback);
#else
    // Windows doesn't require explicit permission, always grant
    if (callback) {
        callback(true);
    }
#endif
}
