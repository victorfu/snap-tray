#include "qml/LongshotController.h"
#include "longshot/FrameReaderLongshotSource.h"
#include "utils/ImageSaveUtils.h"
#include "settings/FileSettingsManager.h"
#include <QApplication>
#include <QDebug>
#include "PlatformFeatures.h"
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QMutex>
#include <QMutexLocker>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
using namespace SnapTray::Longshot;
namespace {
constexpr int kPreviewMaxSide = 2048;
constexpr int kPreviewTileHeight = 1024;
}
struct LongshotPreviewState { QMutex mutex; QImage image; QHash<QString, QImage> thumbnails; };
class LongshotImageProvider final : public QQuickImageProvider {
public:
    explicit LongshotImageProvider(std::shared_ptr<LongshotPreviewState> state)
        : QQuickImageProvider(QQuickImageProvider::Image), m_state(std::move(state)) {}
    QImage requestImage(const QString& id, QSize* size, const QSize& requested) override {
        QMutexLocker lock(&m_state->mutex);
        if (m_state->thumbnails.contains(id)) {
            const auto thumbnail = m_state->thumbnails.value(id);
            if (size) *size = thumbnail.size();
            return thumbnail;
        }
        bool ok = false;
        const int tile = id.section('/', 1, 1).toInt(&ok);
        if (!ok || tile < 0 || tile >= (m_state->image.height() + kPreviewTileHeight - 1) / kPreviewTileHeight) return {};
        auto image = m_state->image.copy(0, tile * kPreviewTileHeight, m_state->image.width(),
                                       std::min(kPreviewTileHeight, m_state->image.height() - tile * kPreviewTileHeight));
        if (size) *size = image.size();
        return image.scaled(QSize(qBound(1, requested.width() > 0 ? requested.width() : image.width(), kPreviewMaxSide),
                                  kPreviewMaxSide), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
private:
    std::shared_ptr<LongshotPreviewState> m_state;
};
namespace {
QString failureMessage(LongshotError error)
{
    switch (error) {
    case LongshotError::TooFewFrames: return LongshotController::tr("Select a longer recording range.");
    case LongshotError::CropTooSmall: return LongshotController::tr("Crop must be at least 64 pixels on each side.");
    case LongshotError::TooManyFrames: return LongshotController::tr("This range is too long. Trim it and try again.");
    case LongshotError::OutOfMemory: return LongshotController::tr("Not enough memory. Choose a smaller crop or range.");
    case LongshotError::NoReliableContent: return LongshotController::tr("No reliable scrolling content was found.");
    default: return LongshotController::tr("The recording could not be decoded. The source has been kept.");
    }
}
}
LongshotController::LongshotController(QObject* parent, LongshotSession::SourceFactory factory)
    : QObject(parent), m_session(std::make_shared<LongshotSession>(factory ? std::move(factory) : FrameReaderLongshotSource::createNative))
{
    m_previewState = std::make_shared<LongshotPreviewState>();
    m_pool.setMaxThreadCount(1);
    connect(&m_watcher, &QFutureWatcher<WorkResult>::finished, this, [this] {
        const auto work = m_watcher.future().takeResult();
        m_busy = false;
        m_status.clear();
        if (work.generation != m_generation) { emit changed(); emit idle(); return; }
        const auto error = work.analyzing ? work.analysis.error : work.render.error;
        if (m_cancel->load() || error == LongshotError::Cancelled) {
            m_phase = work.analyzing ? "idle" : "recommendation";
        } else if (error != LongshotError::None) {
            m_phase = work.analyzing ? "error" : "recommendation";
            m_failureReason = error == LongshotError::TooManyFrames ? "tooLong"
                : error == LongshotError::NoReliableContent ? "noContent" : "error";
            m_message = failureMessage(error);
        } else if (work.analyzing) {
            publishCandidates(work.analysis);
            m_phase = m_candidates.isEmpty() ? "error" : "recommendation";
            m_failureReason = work.analysis.noScrolling ? "noScrolling" : "noContent";
        } else {
            m_result = work.render;
            m_selectedPart = 0;
            m_phase = "result";
            buildMarkers();
            updatePreview();
            emit resultReady();
        }
        m_progress = 100;
        emit changed();
        emit idle();
    });
}
LongshotController::~LongshotController()
{
    cancel();
    m_pool.waitForDone();
    if (m_engine) m_engine->removeImageProvider(m_providerId);
}
void LongshotController::start(const QString& path, qint64 startMs, qint64 endMs, const QRect& crop)
{
    if (m_busy) return;
    invalidate();
    m_path = path; m_startMs = startMs; m_endMs = endMs; m_crop = crop;
    launch(true);
}
void LongshotController::generate()
{
    if (m_busy || m_selectedCandidate < 0 || m_selectedCandidate >= m_candidates.size()) return;
    clearResult();
    launch(false);
}
void LongshotController::launch(bool analyzing)
{
    m_busy = true; m_progress = 0; m_message.clear(); m_failureReason.clear();
    m_phase = analyzing ? "analyzing" : "rendering";
    m_status = analyzing ? tr("Finding content to stitch…") : tr("Generating long screenshot…");
    const auto token = m_cancel = std::make_shared<std::atomic_bool>(false);
    const auto session = m_session;
    const quint64 generation = ++m_generation;
    const auto id = candidate().value("id").toString();
    const auto path = m_path;
    const auto crop = m_crop;
    const qint64 start = m_startMs, end = m_endMs;
    auto progress = [this, token, generation](int percent) {
        if (token->load()) return false;
        QMetaObject::invokeMethod(this, [this, token, generation, percent] {
            if (generation != m_generation || !m_busy || token->load()) return;
            if (m_progress != percent) { m_progress = percent; emit changed(); }
        }, Qt::QueuedConnection);
        return true;
    };
    m_watcher.setFuture(QtConcurrent::run(&m_pool, [=] {
        WorkResult work; work.generation = generation; work.analyzing = analyzing;
        try {
            if (analyzing) {
                session->setRecording(path); session->setCrop(crop); session->setTrim(start, end);
                LongshotOptions options; options.splitOversize = true;
                session->setOptions(options);
                work.analysis = session->analyze(progress);
            } else work.render = session->renderCandidate(id, progress);
        } catch (const std::bad_alloc&) {
            work.analysis.error = work.render.error = LongshotError::OutOfMemory;
        } catch (const std::exception& error) {
            qWarning() << "Longshot worker failed:" << error.what();
            work.analysis.error = work.render.error = LongshotError::SourceUnavailable;
        }
        return work;
    }));
    emit changed();
}
void LongshotController::cancel() { if (m_cancel) m_cancel->store(true); }
void LongshotController::clearResult()
{
    { QMutexLocker lock(&m_previewState->mutex); m_previewState->image = {}; }
    m_result = {}; m_markers.clear(); m_preview.clear(); m_selectedPart = 0;
    m_savedParts.clear(); m_recordedParts.clear(); m_saveDestination.clear();
}
void LongshotController::invalidate()
{
    ++m_generation;
    cancel();
    clearResult();
    m_candidates.clear(); m_selectedCandidate = -1; m_phase = "idle";
    m_message.clear(); m_failureReason.clear();
    { QMutexLocker lock(&m_previewState->mutex); m_previewState->thumbnails.clear(); }
    emit changed();
}
void LongshotController::showRecommendation()
{
    if (m_busy || m_candidates.isEmpty()) return;
    ++m_generation;
    clearResult(); m_message.clear(); m_phase = "recommendation"; emit changed();
}
void LongshotController::setSelectedCandidate(int index)
{
    if (m_busy || index < 0 || index >= m_candidates.size() || index == m_selectedCandidate) return;
    ++m_generation;
    clearResult(); m_selectedCandidate = index; m_phase = "recommendation"; m_message.clear(); emit changed();
}
void LongshotController::publishCandidates(const AnalysisReport& report)
{
    QMutexLocker lock(&m_previewState->mutex);
    m_candidates.clear();
    for (const auto& item : report.candidates) {
        const QString first = "candidate/" + item.id + "/start", last = "candidate/" + item.id + "/end";
        m_previewState->thumbnails[first] = item.startPreview;
        m_previewState->thumbnails[last] = item.endPreview;
        m_candidates.append(QVariantMap{{"id", item.id}, {"startMs", item.startMs}, {"endMs", item.endMs},
            {"width", item.size.width()}, {"height", item.size.height()}, {"imageCount", item.imageCount},
            {"partial", item.partial}, {"needsReview", item.needsReview}, {"recommended", m_candidates.isEmpty()},
            {"reason", item.needsReview ? "review" : item.partial ? "partial" : "continuous"},
            {"startPreview", QUrl("image://" + m_providerId + "/" + first)},
            {"endPreview", QUrl("image://" + m_providerId + "/" + last)}});
    }
    m_selectedCandidate = m_candidates.isEmpty() ? -1 : 0;
}
QSize LongshotController::imageSize() const { return hasResult() ? m_result.parts[m_selectedPart].size() : QSize(); }
int LongshotController::partStartRow() const
{
    int row = 0;
    for (int i = 0; i < m_selectedPart; ++i) row += m_result.parts[i].height();
    return row;
}
void LongshotController::setSelectedPart(int part)
{
    if (m_busy || part < 0 || part >= partCount() || part == m_selectedPart) return;
    m_selectedPart = part; updatePreview(); emit changed();
}
void LongshotController::installImageProvider(QQmlEngine* engine)
{
    if (!m_providerId.isEmpty()) return;
    m_engine = engine;
    m_providerId = "longshot" + QUuid::createUuid().toString(QUuid::Id128);
    engine->addImageProvider(m_providerId, new LongshotImageProvider(m_previewState));
}
void LongshotController::updatePreview()
{
    if (!m_providerId.isEmpty()) {
        QMutexLocker lock(&m_previewState->mutex);
        m_previewState->image = m_result.parts[m_selectedPart];
        m_preview = QUrl("image://" + m_providerId + "/" + QString::number(++m_previewRevision));
        return;
    }
    const QString old = m_preview.toLocalFile();
    const QString path = m_previewDir.filePath(QString::number(++m_previewRevision) + ".png");
    if (m_result.parts[m_selectedPart].scaled(kPreviewMaxSide, kPreviewMaxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(path))
        m_preview = QUrl::fromLocalFile(path);
    else { m_preview.clear(); m_message = tr("Could not create the result preview."); }
    if (!old.isEmpty()) QFile::remove(old);
}
void LongshotController::buildMarkers()
{
    m_markers.clear();
    int previous = -2, part = 0, partEnd = m_result.parts.isEmpty() ? 0 : m_result.parts[0].height();
    for (int row : m_result.lowConfidenceRows) {
        while (row >= partEnd && part + 1 < m_result.parts.size()) {
            ++part; partEnd += m_result.parts[part].height(); previous = -2;
        }
        if (row == previous + 1) {
            auto marker = m_markers.last().toMap(); marker["endRow"] = row + 1; m_markers.last() = marker;
        } else m_markers.append(QVariantMap{{"row", row}, {"endRow", row + 1}, {"part", part}});
        previous = row;
    }
}
void LongshotController::annotate() { if (!m_busy && hasResult()) emit annotateRequested(m_result.parts[m_selectedPart]); }
void LongshotController::pin() { if (!m_busy && hasResult()) emit pinRequested(m_result.parts[m_selectedPart]); }
void LongshotController::clearMessage() { m_message.clear(); emit changed(); }
void LongshotController::copy()
{
    if (m_busy || !hasResult()) return;
    const quint64 generation = m_generation;
    PlatformFeatures::instance().copyImageToClipboardForGuiAsync(m_result.parts[m_selectedPart], this,
        [this, generation](PlatformFeatures::ClipboardCopyResult result) {
            if (generation != m_generation || result == PlatformFeatures::ClipboardCopyResult::Superseded) return;
            m_message = result == PlatformFeatures::ClipboardCopyResult::Success ? tr("Image copied.") : tr("Copy failed. Please try again.");
            emit changed();
        });
}
void LongshotController::save()
{
    if (m_busy || !hasResult()) return;
    if (!m_saveDestination.isEmpty() && m_savedParts.size() < partCount()) {
        const QFileInfo destination(m_saveDestination);
        saveToDirectory(destination.absolutePath(), destination.fileName());
        return;
    }
    auto& settings = FileSettingsManager::instance();
    const QString dir = settings.resolveManualScreenshotSaveDirectory(settings.loadUseLastScreenshotSaveLocation());
    FilenameTemplateEngine::Context context;
    context.type = QStringLiteral("LongScreenshot");
    context.width = m_result.parts.first().width();
    context.height = m_result.fullHeightPx;
    context.prefix = settings.loadFilenamePrefix();
    context.dateFormat = settings.loadDateFormat();
    const QString name = FilenameTemplateEngine::renderFilename(settings.loadFilenameTemplate(), context).filename;
    const QPointer<LongshotController> guard(this);
    const quint64 generation = m_generation;
    const QString path = QFileDialog::getSaveFileName(nullptr, tr("Save Long Screenshot"), QDir(dir).filePath(name), tr("PNG image (*.png)"));
    if (!guard || path.isEmpty() || generation != m_generation) return;
    saveToDirectory(QFileInfo(path).absolutePath(), QFileInfo(path).completeBaseName());
}
bool LongshotController::saveToDirectory(const QString& directory, const QString& baseName)
{
    if (m_busy || !hasResult() || baseName.isEmpty()) return false;
    const QString destination = QDir(directory).absoluteFilePath(baseName);
    if (destination != m_saveDestination) { m_savedParts.clear(); m_saveDestination = destination; }
    QStringList failures;
    for (int i = 0; i < partCount(); ++i) {
        if (m_savedParts.contains(i) && QFileInfo::exists(m_savedParts[i])) continue;
        ImageSaveUtils::UniqueSaveSpec spec;
        spec.outputDir = directory;
        // User-entered names are literals, not template expressions.
        spec.filenameTemplate = "{prefix}";
        const QString suffix = partCount() > 1
            ? QString("-%1").arg(i + 1, 3, 10, QLatin1Char('0')) : QString();
        spec.context.prefix = baseName + suffix;
        spec.context.ext = "png";
        const auto saved = ImageSaveUtils::saveImageUnique(m_result.parts[i], spec, "PNG");
        if (saved.success) {
            m_savedParts[i] = saved.filePath;
            if (!m_recordedParts.contains(i)) {
                m_recordedParts.insert(i);
                emit imageSaved(m_result.parts[i]);
            }
            FileSettingsManager::instance().rememberManualScreenshotSaveDirectory(saved.filePath);
        } else failures.append(tr("Image %1: %2").arg(i + 1).arg(saved.error.message));
    }
    m_message = tr("Saved %1 of %2 images.").arg(m_savedParts.size()).arg(partCount());
    if (!failures.isEmpty()) m_message += "\n" + m_savedParts.values().join('\n') + "\n" + failures.join('\n');
    emit changed(); return failures.isEmpty();
}
