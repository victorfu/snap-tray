#pragma once
#include "longshot/LongshotSession.h"
#include <QObject>
#include <QFutureWatcher>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QUrl>
#include <QSet>
#include <QPointer>
#include <QVariantList>
#include <atomic>
class QQmlEngine;
struct LongshotPreviewState;
#include <memory>

// Single-worker owner of the offline engine. All public actions are GUI-thread only.
class LongshotController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool hasResult READ hasResult NOTIFY changed)
    Q_PROPERTY(int progress READ progress NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QUrl preview READ preview NOTIFY changed)
    Q_PROPERTY(int partCount READ partCount NOTIFY changed)
    Q_PROPERTY(int selectedPart READ selectedPart WRITE setSelectedPart NOTIFY changed)
    Q_PROPERTY(int partStartRow READ partStartRow NOTIFY changed)
    Q_PROPERTY(QSize imageSize READ imageSize NOTIFY changed)
    Q_PROPERTY(QVariantList markers READ markers NOTIFY changed)
    Q_PROPERTY(bool includeHeader MEMBER m_includeHeader NOTIFY changed)
public:
    explicit LongshotController(QObject* parent = nullptr,
        SnapTray::Longshot::LongshotSession::SourceFactory factory = SnapTray::Longshot::LongshotSession::SourceFactory());
    ~LongshotController() override;
    bool busy() const { return m_busy; }
    bool hasResult() const { return !m_result.parts.isEmpty(); }
    int progress() const { return m_progress; }
    QString status() const { return m_status; }
    QString message() const { return m_message; }
    QUrl preview() const { return m_preview; }
    int partCount() const { return m_result.parts.size(); }
    int selectedPart() const { return m_selectedPart; }
    QSize imageSize() const;
    int partStartRow() const;
    QVariantList markers() const { return m_markers; }
    void setSelectedPart(int part);
    void start(const QString& path, qint64 startMs, qint64 endMs, const QRect& crop);
    // Invalidate a displayed result after editing without discarding compatible session caches.
    void invalidate();
    void installImageProvider(QQmlEngine* engine);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void save();
    Q_INVOKABLE void copy();
    Q_INVOKABLE void pin();
    Q_INVOKABLE void clearMessage();
    // Separate from native dialog for deterministic disk-failure/retry tests.
    bool saveToDirectory(const QString& directory, const QString& baseName);
signals:
    void changed();
    void idle();
    void resultReady();
    void imageSaved(const QImage& image);
    void pinRequested(const QImage& image);
private:
    void updatePreview();
    void buildMarkers(const SnapTray::Longshot::RunReport& report);
    std::shared_ptr<LongshotPreviewState> m_previewState;
    QString m_providerId;
    QPointer<QQmlEngine> m_engine;
    QThreadPool m_pool;
    std::shared_ptr<SnapTray::Longshot::LongshotSession> m_session;
    QFutureWatcher<SnapTray::Longshot::RunReport> m_watcher;
    std::shared_ptr<std::atomic_bool> m_cancel;
    quint64 m_generation = 0;
    bool m_busy = false;
    bool m_includeHeader = false;
    int m_progress = 0;
    int m_selectedPart = 0;
    QString m_status, m_message;
    QUrl m_preview;
    QVariantList m_markers;
    SnapTray::Longshot::RenderResult m_result;
    QTemporaryDir m_previewDir;
    quint64 m_previewRevision = 0;
    QHash<int, QString> m_savedParts;
    QSet<int> m_recordedParts;
    QString m_saveDestination;
};
