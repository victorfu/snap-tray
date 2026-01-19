#ifndef RESOURCECLEANUPHELPER_H
#define RESOURCECLEANUPHELPER_H

#include <QObject>
#include <QString>
#include <QFile>
#include <memory>

// Forward declarations for type aliases
class NativeGifEncoder;
class IVideoEncoder;
class ICaptureEngine;
class IAudioCaptureEngine;

// ============================================================================
// Custom Deleters for std::unique_ptr with QObjects
// ============================================================================

/**
 * @brief Custom deleter that calls deleteLater() for safe QObject destruction
 *
 * Use when the QObject may emit signals during destruction. The deleteLater()
 * ensures the object lives until the event loop processes it.
 */
struct QObjectDeleteLater {
    void operator()(QObject* obj) const {
        if (obj) {
            obj->deleteLater();
        }
    }
};

/**
 * @brief Custom deleter that disconnects all signals before deleteLater()
 *
 * Use when the QObject has active signal connections that should be
 * severed before deletion to prevent callbacks during destruction.
 */
struct DisconnectAndDeleteLater {
    void operator()(QObject* obj) const {
        if (obj) {
            QObject::disconnect(obj, nullptr, nullptr, nullptr);
            obj->deleteLater();
        }
    }
};

/**
 * @brief Custom deleter template for objects requiring abort() before deletion
 *
 * Use for IVideoEncoder and NativeGifEncoder to ensure encoding is
 * properly aborted before the object is scheduled for deletion.
 */
template<typename T>
struct AbortAndDeleteLater {
    void operator()(T* obj) const {
        if (obj) {
            obj->abort();
            obj->deleteLater();
        }
    }
};

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

/// Generic QObject pointer with deferred deletion
template<typename T>
using QObjectUniquePtr = std::unique_ptr<T, QObjectDeleteLater>;

/// QObject pointer that disconnects before deferred deletion
template<typename T>
using SafeQObjectPtr = std::unique_ptr<T, DisconnectAndDeleteLater>;

/// Encoder pointer with abort + deleteLater
using GifEncoderPtr = std::unique_ptr<NativeGifEncoder, AbortAndDeleteLater<NativeGifEncoder>>;
using VideoEncoderPtr = std::unique_ptr<IVideoEncoder, AbortAndDeleteLater<IVideoEncoder>>;

/// Engine pointer with stop + disconnect + deleteLater
using CaptureEnginePtr = std::unique_ptr<ICaptureEngine, StopAndDeleteLater<ICaptureEngine>>;
using AudioEnginePtr = std::unique_ptr<IAudioCaptureEngine, StopAndDeleteLater<IAudioCaptureEngine>>;

// ============================================================================
// Legacy Static Helper Functions (kept for compatibility)
// ============================================================================

class ResourceCleanupHelper {
public:
    ResourceCleanupHelper() = delete;

    // Disconnect all signals and deleteLater for QObject
    template<typename T>
    static void safeDisconnectAndDelete(T*& obj) {
        if (obj) {
            QObject::disconnect(obj, nullptr, nullptr, nullptr);
            obj->deleteLater();
            obj = nullptr;
        }
    }

    // Stop engine, disconnect, and deleteLater
    template<typename T>
    static void stopAndDelete(T*& engine) {
        if (engine) {
            QObject::disconnect(engine, nullptr, nullptr, nullptr);
            engine->stop();
            engine->deleteLater();
            engine = nullptr;
        }
    }

    // Remove temp file and clear path
    static void removeTempFile(QString& path) {
        if (!path.isEmpty() && QFile::exists(path)) {
            QFile::remove(path);
            path.clear();
        }
    }
};

#endif // RESOURCECLEANUPHELPER_H
