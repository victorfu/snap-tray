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
#include <memory>
class QQmlEngine;
struct LongshotPreviewState;

// GUI-thread owner. Analysis and rendering run serially on a dedicated worker.
class LongshotController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString phase READ phase NOTIFY changed)
    Q_PROPERTY(QString failureReason READ failureReason NOTIFY changed)
    Q_PROPERTY(QVariantList candidates READ candidates NOTIFY changed)
    Q_PROPERTY(int selectedCandidate READ selectedCandidate WRITE setSelectedCandidate NOTIFY changed)
    Q_PROPERTY(QVariantMap candidate READ candidate NOTIFY changed)
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
public:
    explicit LongshotController(QObject* parent = nullptr,
        SnapTray::Longshot::LongshotSession::SourceFactory factory = {});
    ~LongshotController() override;
    QString phase() const { return m_phase; }
    QString failureReason() const { return m_failureReason; }
    QVariantList candidates() const { return m_candidates; }
    int selectedCandidate() const { return m_selectedCandidate; }
    QVariantMap candidate() const { return m_candidates.value(m_selectedCandidate).toMap(); }
    bool busy() const { return m_busy; }
    bool hasResult() const { return !m_result.parts.isEmpty(); }
    int progress() const { return m_progress; }
    QString status() const { return m_status; }
    QString message() const { return m_message; }
    QUrl preview() const { return m_preview; }
    int partCount() const { return m_result.parts.size(); }
    int selectedPart() const { return m_selectedPart; }
    int partStartRow() const;
    QSize imageSize() const;
    QVariantList markers() const { return m_markers; }
    void setSelectedPart(int part);
    void setSelectedCandidate(int index);
    void start(const QString& path, qint64 startMs, qint64 endMs, const QRect& crop);
    Q_INVOKABLE void invalidate();
    void installImageProvider(QQmlEngine* engine);
    Q_INVOKABLE void generate();
    Q_INVOKABLE void showRecommendation();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void save();
    Q_INVOKABLE void copy();
    Q_INVOKABLE void pin();
    Q_INVOKABLE void clearMessage();
    Q_INVOKABLE void annotate();
    bool saveToDirectory(const QString& directory, const QString& baseName);
signals:
    void changed();
    void idle();
    void resultReady();
    void imageSaved(const QImage& image);
    void pinRequested(const QImage& image);
    void annotateRequested(const QImage& image);
private:
    friend class tst_LongshotController;
    struct WorkResult {
        quint64 generation = 0;
        bool analyzing = false;
        SnapTray::Longshot::AnalysisReport analysis;
        SnapTray::Longshot::RenderResult render;
    };
    void launch(bool analyzing);
    void clearResult();
    void updatePreview();
    void buildMarkers();
    void publishCandidates(const SnapTray::Longshot::AnalysisReport& report);
    std::shared_ptr<LongshotPreviewState> m_previewState;
    QString m_providerId;
    QPointer<QQmlEngine> m_engine;
    QThreadPool m_pool;
    std::shared_ptr<SnapTray::Longshot::LongshotSession> m_session;
    QFutureWatcher<WorkResult> m_watcher;
    std::shared_ptr<std::atomic_bool> m_cancel;
    quint64 m_generation = 0;
    bool m_busy = false;
    int m_progress = 0;
    int m_selectedPart = 0;
    int m_selectedCandidate = -1;
    QString m_phase = QStringLiteral("idle");
    QString m_failureReason, m_status, m_message;
    QString m_path;
    qint64 m_startMs = 0, m_endMs = -1;
    QRect m_crop;
    QUrl m_preview;
    QVariantList m_candidates, m_markers;
    SnapTray::Longshot::RenderResult m_result;
    QTemporaryDir m_previewDir;
    quint64 m_previewRevision = 0;
    QHash<int, QString> m_savedParts;
    QSet<int> m_recordedParts;
    QString m_saveDestination;
};
