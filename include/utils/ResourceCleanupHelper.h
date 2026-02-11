#ifndef RESOURCECLEANUPHELPER_H
#define RESOURCECLEANUPHELPER_H

#include <QObject>
#include <QString>
#include <QFile>
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

// ============================================================================
// Type Aliases for Common unique_ptr Patterns
// ============================================================================

/// Engine pointer with stop + disconnect + deleteLater
using CaptureEnginePtr = std::unique_ptr<ICaptureEngine, StopAndDeleteLater<ICaptureEngine>>;
using AudioEnginePtr = std::unique_ptr<IAudioCaptureEngine, StopAndDeleteLater<IAudioCaptureEngine>>;

// ============================================================================
// Static Helper Functions
// ============================================================================

class ResourceCleanupHelper {
public:
    ResourceCleanupHelper() = delete;

    // Remove temp file and clear path
    static void removeTempFile(QString& path) {
        if (!path.isEmpty() && QFile::exists(path)) {
            QFile::remove(path);
            path.clear();
        }
    }
};

#endif // RESOURCECLEANUPHELPER_H
