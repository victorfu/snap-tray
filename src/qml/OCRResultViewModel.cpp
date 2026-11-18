#include "qml/OCRResultViewModel.h"
#include "detection/TableDetector.h"
#include "utils/VerticalCJKNormalizer.h"

#include <QApplication>
#include <QClipboard>
#include <QFutureWatcher>
#include <QPointer>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

OCRResultViewModel::OCRResultViewModel(QObject* parent)
    : QObject(parent)
{
}

namespace {
struct VerticalImage {
    QImage image;
    VerticalCJKNormalizer::Rejection rejection = VerticalCJKNormalizer::Rejection::InvalidImage;
};
} // namespace

void OCRResultViewModel::setOCRResult(const OCRResult& result)
{
    // A new result may come from another image; its retry source is set anew.
    m_verticalSource = QPixmap();
    m_recognizer = nullptr;
    resetVerticalState();
    m_originalText = result.text;
    m_text = result.text;

    m_detectedTsv.clear();
    const quint64 generation = ++m_resultGeneration;
    if (result.blocks.size() >= 4) {
        auto* watcher = new QFutureWatcher<TableDetectionResult>(this);
        connect(watcher, &QFutureWatcher<TableDetectionResult>::finished, this,
                [this, watcher, generation]() {
            watcher->deleteLater();
            applyTableDetectionResult(watcher->future(), generation);
        });
        // Only value-owned OCR data crosses threads. Closing the dialog destroys
        // the watcher without waiting for analysis or accessing this from a worker.
        watcher->setFuture(QtConcurrent::run([blocks = result.blocks]() {
            return TableDetector::detect(blocks);
        }));
    }

    emit textChanged();
    emit resultSet();
}

void OCRResultViewModel::applyTableDetectionResult(
    const QFuture<TableDetectionResult>& future, quint64 generation)
{
    if (generation != m_resultGeneration) {
        qDebug() << "OCR table detection: ignoring stale result";
        return;
    }
    try {
        const auto detection = future.result();
        m_detectedTsv = detection.isTable ? detection.tsv : QString();
        if (!detection.isTable) {
            qDebug() << "OCR table detection: no table found";
        }
    } catch (...) {
        m_detectedTsv.clear();
        qWarning() << "OCR table detection failed; plain text remains available";
    }
    emit resultSet();
}

void OCRResultViewModel::setVerticalRetrySource(const QPixmap& source, Recognizer recognizer)
{
    resetVerticalState();
    m_verticalSource = source;
    m_recognizer = std::move(recognizer);
    emit verticalStateChanged();
}

void OCRResultViewModel::resetVerticalState()
{
    const bool wasShowingVertical = m_showingVertical;
    ++m_verticalGeneration;
    if (wasShowingVertical) {
        m_text = m_nativeView.text;
        m_originalText = m_nativeView.original;
    }
    m_nativeView = {};
    m_verticalView = {};
    m_verticalStatus.clear();
    m_hasVerticalResult = false;
    m_showingVertical = false;
    m_verticalInProgress = false;
    emit verticalStateChanged();
    if (wasShowingVertical) {
        emit textChanged();
        emit resultSet();
    }
}

bool OCRResultViewModel::verticalRetryAvailable() const
{
    return !m_verticalSource.isNull() && static_cast<bool>(m_recognizer);
}

bool OCRResultViewModel::verticalRetryInProgress() const { return m_verticalInProgress; }

bool OCRResultViewModel::showingVerticalResult() const { return m_showingVertical; }

QString OCRResultViewModel::verticalStatusText() const { return m_verticalStatus; }

void OCRResultViewModel::retryAsVerticalText()
{
    if (!verticalRetryAvailable() || m_verticalInProgress || m_showingVertical) {
        return;
    }
    if (m_hasVerticalResult) {
        switchView(true);
        return;
    }

    m_verticalInProgress = true;
    m_verticalStatus = tr("Recognizing as vertical text...");
    const quint64 generation = ++m_verticalGeneration;
    emit verticalStateChanged();

    // QPixmap is GUI-thread only; hand the worker a value-owned QImage.
    const QImage source = m_verticalSource.toImage();
    auto* watcher = new QFutureWatcher<VerticalImage>(this);
    connect(watcher, &QFutureWatcher<VerticalImage>::finished, this, [this, watcher, generation]() {
        watcher->deleteLater();
        VerticalImage prepared;
        try {
            prepared = watcher->future().result();
        } catch (...) {
            qWarning() << "OCR vertical retry: preprocessing failed";
        }
        onVerticalImageReady(prepared.image, static_cast<int>(prepared.rejection), generation);
    });
    watcher->setFuture(QtConcurrent::run([source]() {
        const auto normalized = VerticalCJKNormalizer::normalize(source);
        return VerticalImage{normalized.image, normalized.rejection};
    }));
}

void OCRResultViewModel::onVerticalImageReady(const QImage& image, int rejection, quint64 generation)
{
    if (generation != m_verticalGeneration) {
        qDebug() << "OCR vertical retry: ignoring stale preprocessing result";
        return;
    }
    using VerticalCJKNormalizer::Rejection;
    const auto reason = static_cast<Rejection>(rejection);
    if (reason != Rejection::None || image.isNull()) {
        finishVerticalRetry(reason == Rejection::NoForeground || reason == Rejection::InvalidImage
                                ? tr("No text found for vertical retry")
                                : tr("Not a single column of vertical text"));
        return;
    }

    QPointer<OCRResultViewModel> guard(this);
    m_recognizer(image, [guard, generation](const OCRResult& result) {
        if (guard) {
            guard->onVerticalRecognized(result, generation);
        }
    });
}

void OCRResultViewModel::onVerticalRecognized(const OCRResult& result, quint64 generation)
{
    if (generation != m_verticalGeneration) {
        qDebug() << "OCR vertical retry: ignoring stale recognition result";
        return;
    }
    const QString text = result.text.trimmed();
    if (!result.success || text.isEmpty()) {
        finishVerticalRetry(tr("Vertical retry found no text"));
        return;
    }
    m_verticalView = {text, text};
    m_hasVerticalResult = true;
    m_verticalInProgress = false;
    m_verticalStatus.clear();
    switchView(true);
}

void OCRResultViewModel::finishVerticalRetry(const QString& status)
{
    // The native result stays untouched; only the status reports the failure.
    m_verticalInProgress = false;
    m_verticalStatus = status;
    emit verticalStateChanged();
}

void OCRResultViewModel::cancelVerticalRetry()
{
    if (!m_verticalInProgress) {
        return;
    }
    ++m_verticalGeneration;
    finishVerticalRetry(QString());
}

void OCRResultViewModel::showOriginalResult()
{
    if (m_showingVertical) {
        switchView(false);
    }
}

void OCRResultViewModel::switchView(bool vertical)
{
    if (vertical == m_showingVertical) {
        emit verticalStateChanged();
        return;
    }
    // Park the current text, edits included, so switching back restores it.
    TextView& outgoing = vertical ? m_nativeView : m_verticalView;
    const TextView& incoming = vertical ? m_verticalView : m_nativeView;
    outgoing = {m_text, m_originalText};
    m_text = incoming.text;
    m_originalText = incoming.original;
    m_showingVertical = vertical;
    m_verticalStatus.clear();
    emit verticalStateChanged();
    emit textChanged();
    emit resultSet(); // TSV belongs to the native geometry only
}

QString OCRResultViewModel::text() const { return m_text; }

void OCRResultViewModel::setText(const QString& text)
{
    if (m_text != text) {
        m_text = text;
        emit textChanged();
    }
}

QString OCRResultViewModel::characterCountText() const
{
    const int count = m_text.length();
    QString countText = (count == 1) ? tr("%1 character").arg(count)
                                     : tr("%1 characters").arg(count);
    if (isEdited())
        countText = tr("%1 (edited)").arg(countText);
    return countText;
}

bool OCRResultViewModel::isEdited() const
{
    return m_text != m_originalText;
}

bool OCRResultViewModel::hasTsv() const
{
    return !m_showingVertical && !m_detectedTsv.isEmpty();
}

bool OCRResultViewModel::copyFeedbackActive() const
{
    return m_copyFeedbackActive;
}

void OCRResultViewModel::copyText()
{
    if (m_text.isEmpty())
        return;
    QApplication::clipboard()->setText(m_text);
    cancelVerticalRetry();
    emit textCopied(m_text);

    m_copyFeedbackActive = true;
    emit copyFeedbackActiveChanged();
    emit dialogClosed();
    QTimer::singleShot(1500, this, [this]() {
        m_copyFeedbackActive = false;
        emit copyFeedbackActiveChanged();
    });
}

void OCRResultViewModel::copyAsTsv()
{
    if (!hasTsv())
        return;
    QApplication::clipboard()->setText(m_detectedTsv);
    cancelVerticalRetry();
    emit textCopied(m_detectedTsv);

    m_copyFeedbackActive = true;
    emit copyFeedbackActiveChanged();
    emit dialogClosed();
    QTimer::singleShot(1500, this, [this]() {
        m_copyFeedbackActive = false;
        emit copyFeedbackActiveChanged();
    });
}

void OCRResultViewModel::close()
{
    // A retry finishing after the dialog closed must not touch the result.
    cancelVerticalRetry();
    emit dialogClosed();
}
