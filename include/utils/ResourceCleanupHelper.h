#ifndef RESOURCECLEANUPHELPER_H
#define RESOURCECLEANUPHELPER_H

#include <QObject>
#include <memory>

class ICaptureEngine;
class IAudioCaptureEngine;

/**
 * @brief Custom deleter template for engines requiring stop() before deletion
 *
 * Use for ICaptureEngine and IAudioCaptureEngine. Disconnects all signals,
 * stops the engine, then schedules for deferred deletion.
 */
template<typename T>
struct StopAndDeleteLater {
    void operator()(T* engine) const {
        if (engine) {
            QObject::disconnect(engine, nullptr, nullptr, nullptr);
            engine->stop();
            engine->deleteLater();
        }
    }
};

/**
 * Audio engines may need to defer their own destruction until a driver call
 * returns. Keep that policy behind the audio interface instead of always
 * posting deleteLater() immediately after stop().
 */
template<typename T>
struct DisposeAudioLater {
    void operator()(T* engine) const {
        if (engine) {
            QObject::disconnect(engine, nullptr, nullptr, nullptr);
            engine->disposeAsync();
        }
    }
};

// ============================================================================
// Type Aliases for Common unique_ptr Patterns
// ============================================================================

/// Engine pointer with stop + disconnect + deleteLater
using CaptureEnginePtr = std::unique_ptr<ICaptureEngine, StopAndDeleteLater<ICaptureEngine>>;
using AudioEnginePtr = std::unique_ptr<IAudioCaptureEngine, DisposeAudioLater<IAudioCaptureEngine>>;

#endif // RESOURCECLEANUPHELPER_H
