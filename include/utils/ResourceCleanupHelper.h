#ifndef RESOURCECLEANUPHELPER_H
#define RESOURCECLEANUPHELPER_H

#include <QObject>
#include <QString>
#include <QFile>

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
