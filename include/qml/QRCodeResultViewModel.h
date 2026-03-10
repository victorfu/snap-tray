#pragma once

#include <QObject>
#include <QImage>
#include <QPixmap>

class QRCodeResultViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    Q_PROPERTY(QString format READ format NOTIFY resultSet)
    Q_PROPERTY(QString formatDisplayName READ formatDisplayName NOTIFY resultSet)
    Q_PROPERTY(QString characterCountText READ characterCountText NOTIFY textChanged)
    Q_PROPERTY(bool isEdited READ isEdited NOTIFY textChanged)
    Q_PROPERTY(bool hasUrl READ hasUrl NOTIFY textChanged)
    Q_PROPERTY(QString detectedUrl READ detectedUrl NOTIFY textChanged)
    Q_PROPERTY(QString thumbnailSource READ thumbnailSource NOTIFY resultSet)
    Q_PROPERTY(QString generatedPreviewSource READ generatedPreviewSource NOTIFY generatedPreviewChanged)
    Q_PROPERTY(bool canGenerate READ canGenerate NOTIFY textChanged)
    Q_PROPERTY(bool hasGeneratedImage READ hasGeneratedImage NOTIFY generatedPreviewChanged)
    Q_PROPERTY(bool pinActionAvailable READ pinActionAvailable NOTIFY pinActionAvailableChanged)
    Q_PROPERTY(bool copyFeedbackActive READ copyFeedbackActive NOTIFY copyFeedbackActiveChanged)

public:
    explicit QRCodeResultViewModel(QObject* parent = nullptr);
    ~QRCodeResultViewModel() override;

    void setResult(const QString& text, const QString& format, const QPixmap& sourceImage = QPixmap());
    void setPinActionAvailable(bool available);

    QString text() const;
    void setText(const QString& text);
    QString format() const;
    QString formatDisplayName() const;
    QString characterCountText() const;
    bool isEdited() const;
    bool hasUrl() const;
    QString detectedUrl() const;
    QString thumbnailSource() const;
    QString generatedPreviewSource() const;
    bool canGenerate() const;
    bool hasGeneratedImage() const;
    bool pinActionAvailable() const;
    bool copyFeedbackActive() const;

    Q_INVOKABLE void copyText();
    Q_INVOKABLE void openUrl();
    Q_INVOKABLE void generateQR();
    Q_INVOKABLE void pinQR();
    Q_INVOKABLE void close();

signals:
    void textChanged();
    void resultSet();
    void generatedPreviewChanged();
    void pinActionAvailableChanged();
    void copyFeedbackActiveChanged();
    void textCopied(const QString& text);
    void urlOpened(const QString& url);
    void qrCodeGenerated(const QImage& image, const QString& text);
    void pinGeneratedRequested(const QPixmap& pixmap);
    void dialogClosed();

private:
    QString detectUrl(const QString& text) const;
    bool isValidUrl(const QString& urlString) const;
    QString getFormatDisplayName(const QString& format) const;

    QString m_text;
    QString m_originalText;
    QString m_format;
    QImage m_generatedQrImage;
    bool m_pinActionAvailable = false;
    bool m_copyFeedbackActive = false;
    QString m_imageProviderId;
    QString m_thumbnailProviderId;
};
