#include "qml/OCRResultViewModel.h"
#include "detection/TableDetector.h"

#include <QApplication>
#include <QClipboard>
#include <QFutureWatcher>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

OCRResultViewModel::OCRResultViewModel(QObject* parent)
    : QObject(parent)
{
}

void OCRResultViewModel::setOCRResult(const OCRResult& result)
{
    m_originalText = result.text;
    m_text = result.text;

    m_detectedTsv.clear();
    const quint64 generation = ++m_resultGeneration;
    if (result.blocks.size() >= 4) {
        auto* watcher = new QFutureWatcher<TableDetectionResult>(this);
        connect(watcher, &QFutureWatcher<TableDetectionResult>::finished, this,
                [this, watcher, generation]() {
            const auto detection = watcher->result();
            watcher->deleteLater();
            if (generation != m_resultGeneration) {
                return;
            }
            m_detectedTsv = detection.isTable ? detection.tsv : QString();
            emit resultSet();
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
    return !m_detectedTsv.isEmpty();
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
    if (m_detectedTsv.isEmpty())
        return;
    QApplication::clipboard()->setText(m_detectedTsv);
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
    emit dialogClosed();
}
