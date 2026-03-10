#include "qml/QRCodeResultViewModel.h"
#include "qml/DialogImageProvider.h"
#include "QRCodeManager.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QUuid>

namespace {
constexpr int kGenerateEccLevel = 1; // Medium
}

QRCodeResultViewModel::QRCodeResultViewModel(QObject* parent)
    : QObject(parent)
    , m_imageProviderId(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_thumbnailProviderId(QUuid::createUuid().toString(QUuid::WithoutBraces) + "_thumb")
{
}

QRCodeResultViewModel::~QRCodeResultViewModel()
{
    SnapTray::DialogImageProvider::removeImage(m_imageProviderId);
    SnapTray::DialogImageProvider::removeImage(m_thumbnailProviderId);
}

void QRCodeResultViewModel::setResult(const QString& text, const QString& format, const QPixmap& sourceImage)
{
    m_originalText = text;
    m_text = text;
    m_format = format;
    m_generatedQrImage = QImage();

    // Set thumbnail
    if (!sourceImage.isNull()) {
        QImage thumb = sourceImage.scaled(56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation).toImage();
        SnapTray::DialogImageProvider::setImage(m_thumbnailProviderId, thumb);
    } else {
        SnapTray::DialogImageProvider::removeImage(m_thumbnailProviderId);
    }

    SnapTray::DialogImageProvider::removeImage(m_imageProviderId);

    emit textChanged();
    emit resultSet();
    emit generatedPreviewChanged();
}

void QRCodeResultViewModel::setPinActionAvailable(bool available)
{
    if (m_pinActionAvailable != available) {
        m_pinActionAvailable = available;
        emit pinActionAvailableChanged();
    }
}

QString QRCodeResultViewModel::text() const { return m_text; }

void QRCodeResultViewModel::setText(const QString& text)
{
    if (m_text != text) {
        m_text = text;
        // Invalidate generated preview on any edit
        m_generatedQrImage = QImage();
        SnapTray::DialogImageProvider::removeImage(m_imageProviderId);
        emit textChanged();
        emit generatedPreviewChanged();
    }
}

QString QRCodeResultViewModel::format() const { return m_format; }

QString QRCodeResultViewModel::formatDisplayName() const
{
    return getFormatDisplayName(m_format);
}

QString QRCodeResultViewModel::characterCountText() const
{
    const int count = m_text.length();
    QString countText = (count == 1) ? tr("%1 character").arg(count)
                                     : tr("%1 characters").arg(count);
    if (isEdited())
        countText = tr("%1 (edited)").arg(countText);
    return countText;
}

bool QRCodeResultViewModel::isEdited() const { return m_text != m_originalText; }
bool QRCodeResultViewModel::hasUrl() const { return !detectUrl(m_text).isEmpty(); }
QString QRCodeResultViewModel::detectedUrl() const { return detectUrl(m_text); }

QString QRCodeResultViewModel::thumbnailSource() const
{
    return QStringLiteral("image://dialog/%1").arg(m_thumbnailProviderId);
}

QString QRCodeResultViewModel::generatedPreviewSource() const
{
    if (m_generatedQrImage.isNull())
        return {};
    return QStringLiteral("image://dialog/%1").arg(m_imageProviderId);
}

bool QRCodeResultViewModel::canGenerate() const
{
    return QRCodeManager::canEncode(m_text, kGenerateEccLevel);
}

bool QRCodeResultViewModel::hasGeneratedImage() const { return !m_generatedQrImage.isNull(); }
bool QRCodeResultViewModel::pinActionAvailable() const { return m_pinActionAvailable; }
bool QRCodeResultViewModel::copyFeedbackActive() const { return m_copyFeedbackActive; }

void QRCodeResultViewModel::copyText()
{
    if (m_text.isEmpty())
        return;
    QApplication::clipboard()->setText(m_text);
    emit textCopied(m_text);

    m_copyFeedbackActive = true;
    emit copyFeedbackActiveChanged();
    QTimer::singleShot(1500, this, [this]() {
        m_copyFeedbackActive = false;
        emit copyFeedbackActiveChanged();
    });
}

void QRCodeResultViewModel::openUrl()
{
    const QString url = detectUrl(m_text);
    if (!url.isEmpty()) {
        QDesktopServices::openUrl(QUrl(url));
        emit urlOpened(url);
    }
}

void QRCodeResultViewModel::generateQR()
{
    if (!QRCodeManager::canEncode(m_text, kGenerateEccLevel))
        return;

    QRCodeManager manager;
    QREncodeOptions options;
    options.width = 320;
    options.height = 320;
    options.margin = 2;
    options.eccLevel = kGenerateEccLevel;

    const QImage image = manager.encode(m_text, options);
    if (image.isNull())
        return;

    m_generatedQrImage = image;
    SnapTray::DialogImageProvider::setImage(m_imageProviderId, image);
    emit generatedPreviewChanged();
    emit qrCodeGenerated(image, m_text);
}

void QRCodeResultViewModel::pinQR()
{
    if (m_generatedQrImage.isNull())
        return;
    emit pinGeneratedRequested(QPixmap::fromImage(m_generatedQrImage));
}

void QRCodeResultViewModel::close()
{
    emit dialogClosed();
}

QString QRCodeResultViewModel::detectUrl(const QString& text) const
{
    QRegularExpression urlRegex(
        R"(https?://[^\s<>"{}|\\^`\[\]]+)",
        QRegularExpression::CaseInsensitiveOption);

    auto match = urlRegex.match(text);
    if (match.hasMatch()) {
        QString url = match.captured(0);
        if (isValidUrl(url))
            return url;
    }
    return {};
}

bool QRCodeResultViewModel::isValidUrl(const QString& urlString) const
{
    if (urlString.isEmpty())
        return false;
    QUrl url(urlString);
    if (!url.isValid())
        return false;
    QString scheme = url.scheme().toLower();
    if (scheme != "http" && scheme != "https")
        return false;
    return !url.host().isEmpty();
}

QString QRCodeResultViewModel::getFormatDisplayName(const QString& format) const
{
    const QMap<QString, QString> formatNames = {
        {"QR_CODE", tr("QR Code")},
        {"DATA_MATRIX", tr("Data Matrix")},
        {"AZTEC", tr("Aztec Code")},
        {"PDF_417", tr("PDF417")},
        {"EAN_8", tr("EAN-8")},
        {"EAN_13", tr("EAN-13")},
        {"UPC_A", tr("UPC-A")},
        {"UPC_E", tr("UPC-E")},
        {"CODE_39", tr("Code 39")},
        {"CODE_93", tr("Code 93")},
        {"CODE_128", tr("Code 128")},
        {"ITF", tr("ITF")},
        {"CODABAR", tr("Codabar")}
    };
    return formatNames.value(format, tr("Barcode"));
}
