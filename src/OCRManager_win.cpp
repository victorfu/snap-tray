#include "OCRManager.h"
#include <QDebug>
#include <QImage>
#include <QCoreApplication>
#include <QThread>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
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

// Try to create OCR engine with preferred languages
OcrEngine tryCreateOcrEngine()
{
    // Preferred language order: Traditional Chinese, Simplified Chinese, English
    static const wchar_t* languageTags[] = {
        L"zh-Hant-TW",
        L"zh-Hans-CN",
        L"en-US"
    };

    for (const auto* tag : languageTags) {
        try {
            Windows::Globalization::Language lang(tag);
            if (OcrEngine::IsLanguageSupported(lang)) {
                return OcrEngine::TryCreateFromLanguage(lang);
            }
        } catch (...) {
            // Language not available, try next
        }
    }

    // Fall back to user profile languages
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
    OcrWorker(const QImage &image, QObject *parent = nullptr)
        : QThread(parent)
        , m_image(image)
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
        winrt::init_apartment(winrt::apartment_type::multi_threaded);

        try {
            OcrEngine engine = tryCreateOcrEngine();
            if (!engine) {
                m_error = QStringLiteral("Failed to create OCR engine. "
                    "Please install OCR language packs in Windows Settings.");
                return;
            }

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
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        OcrEngine engine = OcrEngine::TryCreateFromUserProfileLanguages();
        bool available = (engine != nullptr);
        winrt::uninit_apartment();
        return available;
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
    OcrWorker *worker = new OcrWorker(image, this);

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
