#pragma once

#include <QCache>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QImage>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class RecordingPreviewBackend;

class RecordingFilmstrip : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool hasTiles READ hasTiles NOTIFY hasTilesChanged)
    Q_PROPERTY(QObject* backend READ backend WRITE setBackend NOTIFY backendChanged)

public:
    explicit RecordingFilmstrip(QQuickItem* parent = nullptr);
    ~RecordingFilmstrip() override;
    bool hasTiles() const { return m_hasTiles; }
    QObject* backend() const;
    void setBackend(QObject* object);
    void paint(QPainter* painter) override;

signals:
    void backendChanged();
    void hasTilesChanged();

private:
    // The interval is both the displayed source range and the redaction range.
    using Interval = QPair<qint64, qint64>;
    using Tile = QPair<Interval, QImage>;
    void releaseReader();
    void invalidate();
    void schedule();
    void setHasTiles(bool available);
    void request();
    int density() const;

    QPointer<RecordingPreviewBackend> m_backend;
    QFileSystemWatcher m_sourceWatcher;
    QTimer m_timer;
    QFutureWatcher<Tile>* m_worker = nullptr;
    QCache<Interval, QImage> m_cache{4 * 1024 * 1024};
    QList<Interval> m_visible;
    qint64 m_duration = 0;
    quint64 m_generation = 0;
    int m_density = 0;
    bool m_hasTiles = false;
    bool m_released = false;
    bool m_sourceChanged = false;
};
