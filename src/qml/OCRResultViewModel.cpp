#include "qml/OCRResultViewModel.h"
#include "detection/TableDetector.h"

#include <QApplication>
#include <QClipboard>
#include <QTimer>

OCRResultViewModel::OCRResultViewModel(QObject* parent)
    : QObject(parent)
{
}

void OCRResultViewModel::setOCRResult(const OCRResult& result)
{
    m_originalText = result.text;
    m_text = result.text;

    const TableDetectionResult detection = TableDetector::detect(result.blocks);
    m_detectedTsv = detection.isTable ? detection.tsv : QString();

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
