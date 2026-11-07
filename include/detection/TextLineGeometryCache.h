#pragma once
#include "detection/TextLineSnapper.h"
#include "OCRManager.h"
#include <QObject>
#include <QPointer>

// Session-owned, single-flight OCR cache. Call on the GUI thread, including
// runner completion (OCRManager delivers its worker result on this thread).
class TextLineGeometryCache : public QObject {
public:
    struct Key {
        qint64 source = 0;
        QRect crop; // physical screenshot pixels
        QSize imageSize;
        qreal dpr = 1;
        QStringList languages;
        bool operator==(const Key& other) const {
            return source == other.source && crop == other.crop &&
                imageSize == other.imageSize && dpr == other.dpr && languages == other.languages;
        }
    };
    using Runner = std::function<void(const QPixmap&, const QStringList&, const OCRCallback&)>;
    explicit TextLineGeometryCache(Runner runner, QObject* parent = nullptr)
        : QObject(parent), m_runner(std::move(runner)) {}
    void request(const Key& key, const QPixmap& source);
    void clear();
    std::shared_ptr<const TextLineSnapshot> snapshot() const { return m_snapshot; }
private:
    void start();
    Runner m_runner;
    std::optional<Key> m_key;
    QPixmap m_source;
    std::shared_ptr<const TextLineSnapshot> m_snapshot;
    quint64 m_generation = 0;
    bool m_inFlight = false;
    bool m_attempted = false;
};
