#include "OCRManager.h"
#include <QDebug>
#include <QImage>
#include <QCoreApplication>
#include <QThread>

#include <unknwn.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>

#pragma comment(lib, "windowsapp")

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Media::Ocr;
using namespace Windows::Graphics::Imaging;
using namespace Windows::Storage::Streams;

// IMemoryBufferByteAccess interface for accessing raw buffer data
// Use __declspec approach to avoid MIDL_INTERFACE macro and IUnknown ambiguity with C++/WinRT
struct __declspec(uuid("5b0d3235-4dba-4d44-865e-8f1d0e4fd04d")) __declspec(novtable)
IMemoryBufferByteAccess : ::IUnknown
{
    virtual HRESULT __stdcall GetBuffer(
        uint8_t **value,
        uint32_t *capacity) = 0;
};

namespace {

// Map BCP-47 language codes to Windows language tags
QString mapToWindowsLanguageTag(const QString &bcp47)
{
    // Windows uses region codes (zh-TW, zh-CN) instead of script subtags (zh-Hant, zh-Hans)
    if (bcp47 == "zh-Hant") {
        return "zh-TW";
    } else if (bcp47 == "zh-Hans") {
        return "zh-CN";
    }
    return bcp47;
}

// Map Windows language tags back to BCP-47 codes
QString mapToBcp47(const QString &winTag)
{
    // Convert Windows region codes to BCP-47 script subtags
    if (winTag == "zh-TW" || winTag == "zh-Hant-TW") {
        return "zh-Hant";
    } else if (winTag == "zh-CN" || winTag == "zh-Hans-CN") {
        return "zh-Hans";
    }
    return winTag;
}

// Try to create OCR engine with preferred languages
OcrEngine tryCreateOcrEngine(const QStringList &preferredLanguages)
{
    // Try each preferred language in order
    for (const QString &langCode : preferredLanguages) {
        try {
            // Convert BCP-47 codes to Windows format
            QString winLangCode = mapToWindowsLanguageTag(langCode);

            Windows::Globalization::Language lang(winLangCode.toStdWString().c_str());
            if (OcrEngine::IsLanguageSupported(lang)) {
                OcrEngine engine = OcrEngine::TryCreateFromLanguage(lang);
                if (engine) {
                    qDebug() << "OCRManager: Created engine for language:" << langCode;
                    return engine;
                }
            }
        } catch (...) {
            // Language not available, try next
            continue;
        }
    }

    // Fall back to user profile languages
    qDebug() << "OCRManager: Falling back to user profile languages";
    return OcrEngine::TryCreateFromUserProfileLanguages();
}

// Convert QImage to SoftwareBitmap
SoftwareBitmap qImageToSoftwareBitmap(const QImage &image)
{
    QImage rgbaImage = image.convertToFormat(QImage::Format_RGBA8888);
    int width = rgbaImage.width();
    int height = rgbaImage.height();

    // Create SoftwareBitmap
    SoftwareBitmap bitmap(BitmapPixelFormat::Rgba8, width, height, BitmapAlphaMode::Premultiplied);

    // Copy pixel data
    {
        BitmapBuffer buffer = bitmap.LockBuffer(BitmapBufferAccessMode::Write);
        auto reference = buffer.CreateReference();
        auto byteAccess = reference.as<IMemoryBufferByteAccess>();

        uint8_t* destData = nullptr;
        uint32_t destCapacity = 0;
        winrt::check_hresult(byteAccess->GetBuffer(&destData, &destCapacity));

        const uint8_t* srcData = rgbaImage.constBits();
        int srcBytesPerLine = rgbaImage.bytesPerLine();
        int destBytesPerLine = width * 4;

        for (int y = 0; y < height; ++y) {
            memcpy(destData + y * destBytesPerLine,
                   srcData + y * srcBytesPerLine,
                   destBytesPerLine);
        }
    }

    return bitmap;
}

// Worker thread for OCR processing
class OcrWorker : public QThread
{
public:
    OcrWorker(const QImage &image, const QStringList &languages, QObject *parent = nullptr)
        : QThread(parent)
        , m_image(image)
        , m_languages(languages)
        , m_success(false)
    {
    }

    bool success() const { return m_success; }
    QString text() const { return m_text; }
    QString error() const { return m_error; }

protected:
    void run() override
    {
        // Initialize WinRT for this thread
        // Handle case where apartment is already initialized in a different mode
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        } catch (const winrt::hresult_error& ex) {
            // RPC_E_CHANGED_MODE (0x80010106) means apartment already initialized
            // in different mode - this is OK, we can still use WinRT APIs
            if (ex.code() != HRESULT(0x80010106)) {
                m_error = QString::fromStdWString(std::wstring(ex.message()));
                return;
            }
        }

        try {
            OcrEngine engine = tryCreateOcrEngine(m_languages);
            if (!engine) {
                m_error = QStringLiteral("Failed to create OCR engine. "
                    "Please install OCR language packs in Windows Settings.");
                return;
            }

            qDebug() << "OCRManager: Starting text recognition with languages:" << m_languages;

            SoftwareBitmap bitmap = qImageToSoftwareBitmap(m_image);
            OcrResult result = engine.RecognizeAsync(bitmap).get();

            QStringList lines;
            for (const auto& line : result.Lines()) {
                lines.append(QString::fromStdWString(std::wstring(line.Text())));
            }

            m_text = lines.join(QStringLiteral("\n")).trimmed();
            m_success = true;

        } catch (const winrt::hresult_error& ex) {
            m_error = QString::fromStdWString(std::wstring(ex.message()));
        } catch (const std::exception& ex) {
            m_error = QString::fromUtf8(ex.what());
        } catch (...) {
            m_error = QStringLiteral("Unknown error during OCR processing");
        }

        winrt::uninit_apartment();
    }

private:
    QImage m_image;
    QStringList m_languages;
    bool m_success;
    QString m_text;
    QString m_error;
};

} // anonymous namespace

OCRManager::OCRManager(QObject *parent)
    : QObject(parent)
{
}

OCRManager::~OCRManager()
{
}

bool OCRManager::isAvailable()
{
    try {
        // Try to initialize WinRT apartment, but ignore error if already initialized
        // Qt applications may already have COM/WinRT initialized in a different mode
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        } catch (const winrt::hresult_error& ex) {
            // RPC_E_CHANGED_MODE (0x80010106) means apartment already initialized in different mode
            // This is OK - we can still use WinRT APIs
            if (ex.code() != HRESULT(0x80010106)) {
                throw; // Re-throw other errors
            }
        }

        // Try with default language (en-US) to check availability
        OcrEngine engine = tryCreateOcrEngine({"en-US"});
        return engine != nullptr;
    } catch (...) {
        return false;
    }
}

void OCRManager::recognizeText(const QPixmap &pixmap, const OCRCallback &callback)
{
    if (pixmap.isNull()) {
        QString errorMsg = QStringLiteral("Invalid pixmap provided for OCR");
        if (callback) {
            callback(false, QString(), errorMsg);
        }
        emit recognitionComplete(false, QString(), errorMsg);
        return;
    }

    QImage image = pixmap.toImage();
    OcrWorker *worker = new OcrWorker(image, m_languages, this);

    connect(worker, &QThread::finished, this, [this, worker, callback]() {
        bool success = worker->success();
        QString text = worker->text();
        QString error = worker->error();

        if (callback) {
            callback(success, text, error);
        }
        emit recognitionComplete(success, text, error);

        worker->deleteLater();
    });

    worker->start();
}

QList<OCRLanguageInfo> OCRManager::availableLanguages()
{
    QList<OCRLanguageInfo> result;

    try {
        // Initialize WinRT if needed
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        } catch (const winrt::hresult_error& ex) {
            if (ex.code() != HRESULT(0x80010106)) {
                throw;
            }
        }

        auto languages = OcrEngine::AvailableRecognizerLanguages();

        for (const auto& lang : languages) {
            OCRLanguageInfo info;
            QString tag = QString::fromStdWString(std::wstring(lang.LanguageTag()));
            info.code = mapToBcp47(tag);
            info.nativeName = QString::fromStdWString(std::wstring(lang.NativeName()));
            info.englishName = QString::fromStdWString(std::wstring(lang.DisplayName()));
            result.append(info);
        }

        qDebug() << "OCRManager: Found" << result.size() << "available languages";

    } catch (const winrt::hresult_error& ex) {
        qDebug() << "OCRManager: Failed to enumerate languages:"
                 << QString::fromStdWString(std::wstring(ex.message()));
    } catch (...) {
        qDebug() << "OCRManager: Unknown error enumerating languages";
    }

    return result;
}

void OCRManager::setRecognitionLanguages(const QStringList &languageCodes)
{
    m_languages = languageCodes;
    qDebug() << "OCRManager: Recognition languages set to:" << m_languages;
}

QStringList OCRManager::recognitionLanguages() const
{
    return m_languages;
}
