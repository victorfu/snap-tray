#pragma once

#include <QObject>

struct OCRResult;

class OCRResultViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    Q_PROPERTY(QString characterCountText READ characterCountText NOTIFY textChanged)
    Q_PROPERTY(bool isEdited READ isEdited NOTIFY textChanged)
    Q_PROPERTY(bool hasTsv READ hasTsv NOTIFY resultSet)
    Q_PROPERTY(bool copyFeedbackActive READ copyFeedbackActive NOTIFY copyFeedbackActiveChanged)

public:
    explicit OCRResultViewModel(QObject* parent = nullptr);

    void setOCRResult(const OCRResult& result);

    QString text() const;
    void setText(const QString& text);
    QString characterCountText() const;
    bool isEdited() const;
    bool hasTsv() const;
    bool copyFeedbackActive() const;

    Q_INVOKABLE void copyText();
    Q_INVOKABLE void copyAsTsv();
    Q_INVOKABLE void close();

signals:
    void textChanged();
    void resultSet();
    void copyFeedbackActiveChanged();
    void textCopied(const QString& text);
    void dialogClosed();

private:
    QString m_text;
    QString m_originalText;
    QString m_detectedTsv;
    bool m_copyFeedbackActive = false;
};
