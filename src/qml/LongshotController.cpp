#include "qml/LongshotController.h"
#include "longshot/FrameReaderLongshotSource.h"
#include "utils/ImageSaveUtils.h"
#include "settings/FileSettingsManager.h"
#include <QApplication>
#include <QClipboard>
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
struct LongshotPreviewState { QMutex mutex; QImage image; };
class LongshotImageProvider final : public QQuickImageProvider {
public:
    explicit LongshotImageProvider(std::shared_ptr<LongshotPreviewState> state)
        : QQuickImageProvider(QQuickImageProvider::Image), m_state(std::move(state)) {}
    QImage requestImage(const QString& id, QSize* size, const QSize& requested) override {
        QMutexLocker lock(&m_state->mutex);
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
    connect(&m_watcher, &QFutureWatcher<RunReport>::finished, this, [this] {
        const auto report = m_watcher.result();
        m_busy = false;
        if (!m_cancel->load() && report.error == LongshotError::None && !report.render.parts.isEmpty()) {
            m_result = report.render; m_selectedPart = 0; m_progress = 100;
            m_edits.clear(); m_originalPartStarts.clear();
            int originalStart = 0;
            for (const auto& part : m_result.parts) {
                m_edits.emplace_back(part); m_originalPartStarts.push_back(originalStart);
                originalStart += part.height();
            }
            buildMarkers(report);
            m_originalMarkers = m_markers;
            m_originalSpans = report.render.sourceSpans;
            m_message.clear();
            updatePreview();
            emit resultReady();
        } else if (!m_cancel->load() && report.error != LongshotError::Cancelled) {
            m_message = failureMessage(report.error);
        }
        m_status.clear();
        emit changed(); emit idle();
    });
}
LongshotController::~LongshotController()
{
    cancel();
    // The normal close path waits asynchronously for idle before deleting the backend.
    // Defensive destruction also releases all native file handles before source cleanup.
    m_pool.waitForDone();
    if (m_engine) m_engine->removeImageProvider(m_providerId);
}
void LongshotController::start(const QString& path, qint64 startMs, qint64 endMs, const QRect& crop)
{
    if (m_busy) return;
    invalidate(); m_message.clear(); m_busy = true; m_progress = 0;
    m_status = tr("Analyzing...");
    const auto token = m_cancel = std::make_shared<std::atomic_bool>(false);
    const auto session = m_session;
    const quint64 generation = ++m_generation;
    const bool includeHeader = m_includeHeader;
    // QObject remains alive until worker completion; queued deliveries are discarded on destruction.
    auto deliver = [this, generation](int percent, WorkStage stage) {
        QMetaObject::invokeMethod(this, [this, generation, percent, stage] {
            if (generation != m_generation || !m_busy || m_cancel->load()) return;
            m_progress = percent;
            m_status = stage == WorkStage::Analyze ? tr("Analyzing...")
                : stage == WorkStage::Solve ? tr("Solving positions...") : tr("Rendering...");
            emit changed();
        }, Qt::QueuedConnection);
    };
    m_watcher.setFuture(QtConcurrent::run(&m_pool, [session, token, path, startMs, endMs, crop, includeHeader, deliver] {
        session->setRecording(path); session->setTrim(startMs, endMs); session->setCrop(crop);
        LongshotOptions options; options.splitOversize = true; options.includeStickyHeader = includeHeader;
        session->setOptions(options);
        WorkStage current = WorkStage::Analyze;
        int lastPercent = -1;
        try {
            return session->run([&](int percent) {
                if (percent != lastPercent) { deliver(percent, current); lastPercent = percent; }
                return !token->load();
            }, [&](WorkStage stage) { current = stage; lastPercent = -1; deliver(0, stage); });
        } catch (const std::bad_alloc&) {
            RunReport report; report.error = LongshotError::OutOfMemory; return report;
        }

    }));
    emit changed();
}
void LongshotController::cancel() { if (m_cancel) m_cancel->store(true); }
void LongshotController::invalidate()
{
    if (m_busy) { cancel(); return; }
    { QMutexLocker lock(&m_previewState->mutex); m_previewState->image = {}; }
    m_edits.clear(); m_originalPartStarts.clear(); m_originalMarkers.clear(); m_originalSpans.clear();
    m_result = {}; m_markers.clear(); m_preview = QUrl(); m_savedParts.clear(); m_recordedParts.clear(); m_saveDestination.clear();
    emit changed();
}
QString LongshotController::partLabel() const
{
    if (!hasResult()) return {};
    const auto info = m_result.partInfo.value(m_selectedPart);
    return tr("Section %1 of %2 · Part %3 of %4").arg(info.section + 1).arg(m_result.sectionCount)
        .arg(info.indexInSection + 1).arg(info.partsInSection);
}
QString LongshotController::resultSummary() const
{
    if (!hasResult()) return {};
    const auto info = m_result.partInfo.value(m_selectedPart);
    QString summary = tr("Auto-cropped columns: left %1, right %2.").arg(info.autoCroppedLeft).arg(info.autoCroppedRight);
    if (!m_markers.isEmpty()) summary += " " + tr("Review the marked areas: the result may be incomplete.");
    return summary;
}
QSize LongshotController::imageSize() const { return hasResult() ? m_result.parts[m_selectedPart].size() : QSize(); }
int LongshotController::partStartRow() const
{
    int row = 0;
    for (int i = 0; i < m_selectedPart && i < partCount(); ++i) row += m_result.parts[i].height();
    return row;
}
void LongshotController::setSelectedPart(int part)
{
    if (part < 0 || part >= partCount() || part == m_selectedPart) return;
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
    else { m_preview = QUrl(); m_message = tr("Could not create the result preview."); }
    if (!old.isEmpty()) QFile::remove(old);
}
void LongshotController::buildMarkers(const RunReport& report)
{
    m_markers.clear();
    auto appendRows = [&](const std::vector<int>& rows, const QString& kind) {
        int previous = -2;
        for (int row : rows) {
            if (row <= previous + 1) {
                auto marker = m_markers.last().toMap();
                marker["endRow"] = std::max(marker.value("endRow").toInt(),row+1);
                m_markers.last() = marker;
                previous = row; continue;
            }
            previous = row;
            qint64 time = -1;
            for (const auto& span : report.render.sourceSpans)
                if (row >= span.firstRow && row < span.endRow) { time = span.timeMs; break; }
            m_markers.append(QVariantMap{{"row", row}, {"endRow", row+1}, {"timeMs", time}, {"label", kind}});
        }
    };
    appendRows(report.render.breakRows, tr("Missing coverage"));
    appendRows(report.render.lowConfidenceRows, tr("Low confidence"));
    for (auto time : report.analysis.solve.breakTimesMs) {
        const bool exported = std::any_of(report.render.partInfo.cbegin(), report.render.partInfo.cend(),
                                          [time](const RenderPartInfo& part) { return part.startMs == time; });
        if (!exported)
            m_markers.append(QVariantMap{{"row", -1}, {"timeMs", time}, {"label", tr("Unjoined recording section")}});
    }
}
bool LongshotController::canUndo() const { return hasResult() && m_edits[size_t(m_selectedPart)].canUndo(); }
bool LongshotController::canRedo() const { return hasResult() && m_edits[size_t(m_selectedPart)].canRedo(); }
bool LongshotController::imageEdited() const { return hasResult() && m_edits[size_t(m_selectedPart)].edited(); }
bool LongshotController::edit(const std::function<bool(LongshotImageEdit&)>& operation)
{
    if (m_busy || !hasResult()) return false;
    auto candidate = m_edits[size_t(m_selectedPart)];
    if (!operation(candidate)) return false;
    QImage image = candidate.image();
    if (image.isNull()) { m_message = failureMessage(LongshotError::OutOfMemory); emit changed(); return false; }
    m_edits[size_t(m_selectedPart)] = std::move(candidate);
    m_result.parts[m_selectedPart] = std::move(image);
    m_result.fullHeightPx = 0;
    for (const auto& part : m_result.parts) m_result.fullHeightPx += part.height();
    m_savedParts.remove(m_selectedPart); m_recordedParts.remove(m_selectedPart);
    m_message.clear();
    remapMarkers(); updatePreview(); emit changed(); return true;
}
bool LongshotController::keepRows(int begin, int end)
{ return edit([=](LongshotImageEdit& image) { return image.keepRows(begin,end); }); }
bool LongshotController::removeRows(int begin, int end)
{ return edit([=](LongshotImageEdit& image) { return image.removeRows(begin,end); }); }
bool LongshotController::undoEdit()
{ return edit([](LongshotImageEdit& image) { return image.undo(); }); }
bool LongshotController::redoEdit()
{ return edit([](LongshotImageEdit& image) { return image.redo(); }); }
bool LongshotController::resetImage()
{ return edit([](LongshotImageEdit& image) { return image.reset(); }); }
void LongshotController::remapMarkers()
{
    m_markers.clear();
    for (const auto& marker : m_originalMarkers) {
        QVariantMap mapped = marker.toMap();
        const int originalRow = mapped.value("row").toInt();
        if (originalRow < 0) { m_markers.append(mapped); continue; }
        const int originalEnd = mapped.value("endRow",originalRow+1).toInt();
        int outputOffset = 0;
        for (int part = 0; part < partCount(); ++part) {
            const int originalOffset = m_originalPartStarts[size_t(part)];
            for (const auto& range : m_edits[size_t(part)].mapRows(originalRow-originalOffset,originalEnd-originalOffset)) {
                auto fragment = mapped;
                fragment["row"] = outputOffset+range.outputBegin;
                fragment["endRow"] = outputOffset+range.outputBegin+range.count;
                const int sourceRow = originalOffset+range.originalBegin;
                qint64 time = -1;
                for (const auto& span : m_originalSpans)
                    if (sourceRow >= span.firstRow && sourceRow < span.endRow) { time = span.timeMs; break; }
                fragment["timeMs"] = time;
                m_markers.append(fragment);
            }
            outputOffset += m_result.parts[part].height();
        }
    }
}
void LongshotController::annotate()
{
    if (!m_busy && hasResult()) emit annotateRequested(m_result.parts[m_selectedPart]);
}
void LongshotController::clearMessage() { m_message.clear(); emit changed(); }
void LongshotController::copy()
{
    if (m_busy || !hasResult()) return;
    QApplication::clipboard()->setImage(m_result.parts[m_selectedPart]);
    m_message = tr("Copied part %1.").arg(m_selectedPart + 1); emit changed();
}
void LongshotController::pin()
{
    if (!m_busy && hasResult()) emit pinRequested(m_result.parts[m_selectedPart]);
}
void LongshotController::save()
{
    if (m_busy || !hasResult()) return;
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
        const auto info = m_result.partInfo.value(i);
        QString suffix;
        if (m_result.sectionCount > 1) {
            suffix = QString("-s%1").arg(info.section + 1, 2, 10, QLatin1Char('0'));
            if (info.partsInSection > 1)
                suffix += QString("-p%1").arg(info.indexInSection + 1, 3, 10, QLatin1Char('0'));
        } else if (partCount() > 1) {
            suffix = QString("-%1").arg(i + 1, 3, 10, QLatin1Char('0'));
        }
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
        } else failures.append(tr("Part %1: %2").arg(i + 1).arg(saved.error.message));
    }
    m_message = tr("Saved %1 of %2 parts.").arg(m_savedParts.size()).arg(partCount());
    if (!failures.isEmpty()) m_message += "\n" + m_savedParts.values().join('\n') + "\n" + failures.join('\n');
    emit changed(); return failures.isEmpty();
}
