#include "qml/ImageExportViewModel.h"

#include <QFutureWatcher>
#include <QLocale>
#include <QPointer>
#include <QCoreApplication>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrent>

namespace SnapTray {

namespace {

// Never a real generation: marks a view model that is gone.
constexpr quint64 kAbandonedGeneration = ~quint64(0);

// Every Export dialog encodes here, one image at a time, so reopening the
// dialog on a huge capture cannot pile up encodes across the global pool.
QThreadPool* encodePool()
{
    static QThreadPool* pool = [] {
        auto* created = new QThreadPool(qApp);
        created->setMaxThreadCount(1);
        return created;
    }();
    return pool;
}

QString pixelSizeText(const QSize& size)
{
    return QStringLiteral("%1 \u00D7 %2 px").arg(size.width()).arg(size.height());
}

} // namespace

ImageExportViewModel::ImageExportViewModel(const QImage& source,
                                           const ImageExportOptions& initialOptions,
                                           QObject* parent)
    : QObject(parent)
    , m_source(source)
    , m_options(initialOptions.normalized())
    , m_encoder(&ImageExport::encode)
    , m_debounce(new QTimer(this))
    , m_liveGeneration(std::make_shared<std::atomic<quint64>>(0))
{
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kEstimateDebounceMs);
    connect(m_debounce, &QTimer::timeout, this, &ImageExportViewModel::startEncode);
    scheduleEstimate();
}

ImageExportViewModel::~ImageExportViewModel()
{
    // A queued encode for this dialog has nobody left to deliver to.
    m_liveGeneration->store(kAbandonedGeneration);
}

int ImageExportViewModel::longEdge() const
{
    return qMin(m_options.longEdge, maxLongEdge());
}

int ImageExportViewModel::maxLongEdge() const
{
    const int sourceLongEdge = qMax(m_source.width(), m_source.height());
    return qBound(ImageExportOptions::kMinLongEdge, sourceLongEdge, ImageExportOptions::kMaxLongEdge);
}

void ImageExportViewModel::applyOptions(const ImageExportOptions& requested)
{
    const ImageExportOptions options = requested.normalized();
    if (options == m_options) {
        return;
    }
    m_options = options;
    emit optionsChanged();
    scheduleEstimate();
}

void ImageExportViewModel::setResizeMode(int mode)
{
    if (mode < static_cast<int>(ImageExportOptions::Resize::Original)
        || mode > static_cast<int>(ImageExportOptions::Resize::LongEdge)) {
        return;
    }
    ImageExportOptions options = m_options;
    options.resize = static_cast<ImageExportOptions::Resize>(mode);
    applyOptions(options);
}

void ImageExportViewModel::setPercent(int percent)
{
    ImageExportOptions options = m_options;
    options.percent = percent;
    applyOptions(options);
}

void ImageExportViewModel::setLongEdge(int pixels)
{
    ImageExportOptions options = m_options;
    options.longEdge = pixels;
    applyOptions(options);
}

void ImageExportViewModel::setFormat(int format)
{
    if (format != static_cast<int>(ImageExportOptions::Format::Png)
        && format != static_cast<int>(ImageExportOptions::Format::Jpeg)) {
        return;
    }
    ImageExportOptions options = m_options;
    options.format = static_cast<ImageExportOptions::Format>(format);
    applyOptions(options);
}

void ImageExportViewModel::setJpegQuality(int quality)
{
    ImageExportOptions options = m_options;
    options.jpegQuality = quality;
    applyOptions(options);
}

QString ImageExportViewModel::sourceSizeText() const
{
    return pixelSizeText(m_source.size());
}

QString ImageExportViewModel::outputSizeText() const
{
    return pixelSizeText(outputPixelSize());
}

QString ImageExportViewModel::fileSizeText() const
{
    if (m_candidate) {
        const qint64 bytes = m_candidate->data.size();
        const QLocale locale;
        return tr("%1 (%2 bytes)").arg(locale.formattedDataSize(bytes), locale.toString(bytes));
    }
    if (!m_estimateError.isEmpty()) {
        return tr("Could not encode: %1").arg(m_estimateError);
    }
    return tr("Calculating...");
}

QString ImageExportViewModel::noteText() const
{
    if (!m_options.isLossy()) {
        return tr("PNG is lossless: every pixel of the output size is kept exactly.");
    }
    QString note = tr("JPEG is lossy: text and thin lines lose detail, and a smaller file is not the same image.");
    if (m_candidate && m_candidate->transparencyDropped) {
        note += QLatin1Char(' ')
            + tr("This image has transparent areas, which JPEG cannot keep.")
            + QLatin1Char(' ') + tr("Transparent areas are filled with white.");
    }
    return note;
}

void ImageExportViewModel::setErrorText(const QString& text)
{
    if (m_errorText == text) {
        return;
    }
    m_errorText = text;
    emit errorTextChanged();
}

void ImageExportViewModel::setEncoderForTesting(Encoder encoder)
{
    m_encoder = encoder ? std::move(encoder) : Encoder(&ImageExport::encode);
    scheduleEstimate();
}

void ImageExportViewModel::scheduleEstimate()
{
    ++m_generation;
    m_liveGeneration->store(m_generation);
    m_candidate.reset();
    m_estimateError.clear();
    setErrorText(QString());
    // Always: the note depends on the format even while nothing is encoded yet.
    emit estimateChanged();
    m_debounce->start();
}

void ImageExportViewModel::startEncode()
{
    if (m_running) {
        // One encode at a time: the newest options run when this one ends.
        m_restartPending = true;
        return;
    }
    m_running = true;
    m_restartPending = false;
    ++m_runsStarted;

    const quint64 generation = m_generation;
    auto* watcher = new QFutureWatcher<ImageExport::Encoded>(this);
    connect(watcher, &QFutureWatcher<ImageExport::Encoded>::finished, this,
            [this, watcher, generation]() {
                const ImageExport::Encoded encoded = watcher->result();
                watcher->deleteLater();
                finishEncode(generation, encoded);
            });
    // The worker only holds copies (the image is implicitly shared), so it
    // may outlive this object; its result is then simply never delivered.
    watcher->setFuture(QtConcurrent::run(
        encodePool(),
        [encoder = m_encoder, source = m_source, options = m_options, generation,
         live = m_liveGeneration]() {
            if (live->load() != generation) {
                ImageExport::Encoded skipped;
                skipped.options = options;
                skipped.error = QStringLiteral("superseded");
                return skipped;
            }
            return encoder(source, options);
        }));
}

void ImageExportViewModel::finishEncode(quint64 generation, const ImageExport::Encoded& encoded)
{
    m_running = false;
    if (generation == m_generation) {
        if (encoded.isValid()) {
            m_candidate = encoded;
        } else {
            m_estimateError = encoded.error.isEmpty() ? tr("Unknown error") : encoded.error;
        }
        emit estimateChanged();
    }
    // A stale result is dropped. If the debounce timer is still running it
    // will start the next encode itself.
    if (m_restartPending && !m_debounce->isActive()) {
        startEncode();
    }
}

void ImageExportViewModel::save()
{
    if (canSave()) {
        emit saveRequested();
    }
}

void ImageExportViewModel::cancel()
{
    emit cancelRequested();
}

} // namespace SnapTray
