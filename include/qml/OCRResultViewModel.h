#pragma once

#include <QObject>
#include <QPixmap>
#include <functional>

struct OCRResult;
struct TableDetectionResult;
template <typename T> class QFuture;

class OCRResultViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    Q_PROPERTY(QString characterCountText READ characterCountText NOTIFY textChanged)
    Q_PROPERTY(bool isEdited READ isEdited NOTIFY textChanged)
    Q_PROPERTY(bool hasTsv READ hasTsv NOTIFY resultSet)
    Q_PROPERTY(bool copyFeedbackActive READ copyFeedbackActive NOTIFY copyFeedbackActiveChanged)
    Q_PROPERTY(bool verticalRetryAvailable READ verticalRetryAvailable NOTIFY verticalStateChanged)
    Q_PROPERTY(bool verticalRetryInProgress READ verticalRetryInProgress NOTIFY verticalStateChanged)
    Q_PROPERTY(bool showingVerticalResult READ showingVerticalResult NOTIFY verticalStateChanged)
    Q_PROPERTY(QString verticalStatusText READ verticalStatusText NOTIFY verticalStateChanged)

public:
    using OCRResultCallback = std::function<void(const OCRResult&)>;
    // Runs native OCR on an image and reports on the GUI thread. May complete
    // synchronously. Callers own lifetime guards for the engine they capture.
    using Recognizer = std::function<void(const QImage&, const OCRResultCallback&)>;

    explicit OCRResultViewModel(QObject* parent = nullptr);

    void setOCRResult(const OCRResult& result);
    // Enables the opt-in single-column vertical CJK retry for the image the
    // current result came from. The retry yields text only; its geometry
    // belongs to a re-laid image and never replaces the original blocks.
    void setVerticalRetrySource(const QPixmap& source, Recognizer recognizer);

    QString text() const;
    void setText(const QString& text);
    QString characterCountText() const;
    bool isEdited() const;
    bool hasTsv() const;
    bool copyFeedbackActive() const;
    bool verticalRetryAvailable() const;
    bool verticalRetryInProgress() const;
    bool showingVerticalResult() const;
    QString verticalStatusText() const;

    // Shows the vertical result, running the retry first if needed.
    Q_INVOKABLE void retryAsVerticalText();
    Q_INVOKABLE void cancelVerticalRetry();
    Q_INVOKABLE void showOriginalResult();
    Q_INVOKABLE void copyText();
    Q_INVOKABLE void copyAsTsv();
    Q_INVOKABLE void close();

signals:
    void textChanged();
    // Emitted immediately for text, and again when current table analysis completes.
    void resultSet();
    void copyFeedbackActiveChanged();
    void verticalStateChanged();
    void textCopied(const QString& text);
    void dialogClosed();

private:
    friend class TestOCRResultViewModel;
    struct TextView {
        QString text;
        QString original;
    };
    void applyTableDetectionResult(const QFuture<TableDetectionResult>& future, quint64 generation);
    void onVerticalImageReady(const QImage& image, int rejection, quint64 generation);
    void onVerticalRecognized(const OCRResult& result, quint64 generation);
    void finishVerticalRetry(const QString& status);
    void resetVerticalState();
    void switchView(bool vertical);

    QString m_text;
    QString m_originalText;
    TextView m_nativeView;   // Parked while the vertical result is shown
    TextView m_verticalView; // Parked while the native result is shown
    QPixmap m_verticalSource;
    Recognizer m_recognizer;
    QString m_verticalStatus;
    quint64 m_verticalGeneration = 0; // Late retries for an older source/closed dialog are dropped
    bool m_hasVerticalResult = false;
    bool m_showingVertical = false;
    bool m_verticalInProgress = false;
    QString m_detectedTsv;
    quint64 m_resultGeneration = 0; // GUI-thread generation; older workers cannot replace newer TSV.
    bool m_copyFeedbackActive = false;
};
