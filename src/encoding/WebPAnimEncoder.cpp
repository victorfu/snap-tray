#include "encoding/WebPAnimEncoder.h"
#include <QDebug>
#include <QFile>

#include <webp/encode.h>
#include <webp/mux.h>

// Internal data structure to hold libwebp encoder state
struct WebPAnimationEncoder::WebPEncoderData {
    WebPAnimEncoder* encoder = nullptr;
    WebPAnimEncoderOptions encOptions;
    WebPConfig config;
    int timestampMs = 0;
};

WebPAnimationEncoder::WebPAnimationEncoder(QObject *parent)
    : QObject(parent)
    , m_data(nullptr)
    , m_frameRate(30)
    , m_quality(80)
    , m_looping(true)
    , m_framesWritten(0)
    , m_lastTimestampMs(0)
    , m_running(false)
    , m_aborted(false)
{
}

WebPAnimationEncoder::~WebPAnimationEncoder()
{
    if (m_running) {
        abort();
    }
}

void WebPAnimationEncoder::setQuality(int quality)
{
    m_quality = qBound(0, quality, 100);
}

void WebPAnimationEncoder::setLooping(bool loop)
{
    m_looping = loop;
}

bool WebPAnimationEncoder::start(const QString &outputPath, const QSize &frameSize, int frameRate)
{
    if (m_running) {
        m_lastError = "Encoder already running";
        return false;
    }

    if (frameSize.width() < 1 || frameSize.height() < 1) {
        m_lastError = QString("Frame size too small: %1x%2")
            .arg(frameSize.width()).arg(frameSize.height());
        emit error(m_lastError);
        return false;
    }

    m_outputPath = outputPath;
    m_frameSize = frameSize;
    m_frameRate = frameRate;
    m_framesWritten = 0;
    m_lastTimestampMs = 0;
    m_lastError.clear();
    m_aborted = false;

    // Allocate internal data
    m_data = new WebPEncoderData();

    // Initialize encoder options
    if (!WebPAnimEncoderOptionsInit(&m_data->encOptions)) {
        m_lastError = "Failed to initialize WebP encoder options";
        delete m_data;
        m_data = nullptr;
        emit error(m_lastError);
        return false;
    }

    // Configure for good quality/speed balance
    m_data->encOptions.anim_params.loop_count = m_looping ? 0 : 1;  // 0 = infinite loop
    m_data->encOptions.minimize_size = 0;  // Faster encoding
    m_data->encOptions.allow_mixed = 1;    // Allow lossy and lossless frames

    // Initialize WebP config
    if (!WebPConfigInit(&m_data->config)) {
        m_lastError = "Failed to initialize WebP config";
        delete m_data;
        m_data = nullptr;
        emit error(m_lastError);
        return false;
    }

    // Set quality
    m_data->config.quality = static_cast<float>(m_quality);
    m_data->config.method = 2;  // 0-6, higher = slower but better compression
    m_data->config.lossless = 0;  // Use lossy encoding for smaller files

    // Validate config
    if (!WebPValidateConfig(&m_data->config)) {
        m_lastError = "Invalid WebP config";
        delete m_data;
        m_data = nullptr;
        emit error(m_lastError);
        return false;
    }

    // Create the animation encoder
    m_data->encoder = WebPAnimEncoderNew(
        m_frameSize.width(),
        m_frameSize.height(),
        &m_data->encOptions
    );

    if (!m_data->encoder) {
        m_lastError = "Failed to create WebP animation encoder";
        delete m_data;
        m_data = nullptr;
        emit error(m_lastError);
        return false;
    }

    m_data->timestampMs = 0;
    m_running = true;
    qDebug() << "WebPAnimationEncoder: Started -" << m_frameSize << "@" << m_frameRate << "fps, quality:" << m_quality;
    return true;
}

void WebPAnimationEncoder::writeFrame(const QImage &frame, qint64 timestampMs)
{
    if (!m_running || m_aborted || !m_data || !m_data->encoder) {
        return;
    }

    if (frame.isNull()) {
        qWarning() << "WebPAnimationEncoder: Received null frame";
        return;
    }

    // Process frame
    QImage processed = frame;

    // Scale if needed
    if (processed.size() != m_frameSize) {
        processed = processed.scaled(m_frameSize, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }

    // Convert to RGBA8888 format
    if (processed.format() != QImage::Format_RGBA8888) {
        processed = processed.convertToFormat(QImage::Format_RGBA8888);
    }

    if (processed.isNull()) {
        qWarning() << "WebPAnimationEncoder: Frame conversion failed";
        return;
    }

    // Calculate timestamp (cap at INT_MAX to prevent overflow for very long recordings)
    int currentTimestampMs;
    if (timestampMs >= 0 && m_framesWritten > 0) {
        currentTimestampMs = timestampMs > INT_MAX ? INT_MAX : static_cast<int>(timestampMs);
    } else {
        // Use frame rate to calculate timestamp
        currentTimestampMs = m_data->timestampMs;
    }

    // Create WebPPicture for this frame
    WebPPicture pic;
    if (!WebPPictureInit(&pic)) {
        qWarning() << "WebPAnimationEncoder: Failed to init WebPPicture";
        return;
    }

    pic.width = processed.width();
    pic.height = processed.height();
    pic.use_argb = 1;

    // Allocate picture buffer
    if (!WebPPictureAlloc(&pic)) {
        qWarning() << "WebPAnimationEncoder: Failed to allocate WebPPicture";
        WebPPictureFree(&pic);
        return;
    }

    // Import RGBA data
    if (!WebPPictureImportRGBA(&pic,
                               processed.constBits(),
                               processed.bytesPerLine())) {
        qWarning() << "WebPAnimationEncoder: Failed to import RGBA data";
        WebPPictureFree(&pic);
        return;
    }

    // Add frame to animation
    int success = WebPAnimEncoderAdd(m_data->encoder, &pic, currentTimestampMs, &m_data->config);
    WebPPictureFree(&pic);

    if (!success) {
        qWarning() << "WebPAnimationEncoder: Failed to add frame" << m_framesWritten;
        return;
    }

    // Update timestamp for next frame
    int frameDurationMs = 1000 / m_frameRate;
    if (timestampMs >= 0 && m_framesWritten > 0) {
        qint64 nextTs = timestampMs + frameDurationMs;
        m_data->timestampMs = nextTs > INT_MAX ? INT_MAX : static_cast<int>(nextTs);
    } else {
        qint64 nextTs = static_cast<qint64>(m_data->timestampMs) + frameDurationMs;
        m_data->timestampMs = nextTs > INT_MAX ? INT_MAX : static_cast<int>(nextTs);
    }

    m_framesWritten++;
    m_lastTimestampMs = timestampMs;

    // Emit progress every 30 frames
    if (m_framesWritten % 30 == 0) {
        emit progress(m_framesWritten);
    }
}

void WebPAnimationEncoder::finish()
{
    if (!m_running || !m_data || !m_data->encoder) {
        return;
    }

    qDebug() << "WebPAnimationEncoder: Finishing with" << m_framesWritten << "frames";

    // Add final frame to close the animation
    // (WebP needs a null frame at the end timestamp to set duration of last frame)
    WebPAnimEncoderAdd(m_data->encoder, nullptr, m_data->timestampMs, nullptr);

    // Assemble the animation
    WebPData webpData;
    WebPDataInit(&webpData);

    bool success = false;
    if (WebPAnimEncoderAssemble(m_data->encoder, &webpData)) {
        // Write to file
        QFile file(m_outputPath);
        if (file.open(QIODevice::WriteOnly)) {
            qint64 written = file.write(
                reinterpret_cast<const char*>(webpData.bytes),
                static_cast<qint64>(webpData.size)
            );
            file.close();

            if (written == static_cast<qint64>(webpData.size)) {
                success = true;
                qDebug() << "WebPAnimationEncoder: Saved" << webpData.size << "bytes to" << m_outputPath;
            } else {
                m_lastError = QString("Failed to write WebP data: wrote %1 of %2 bytes")
                    .arg(written).arg(webpData.size);
            }
        } else {
            m_lastError = "Failed to open output file: " + file.errorString();
        }
    } else {
        m_lastError = "WebP animation assembly failed";
    }

    WebPDataClear(&webpData);
    cleanup();

    if (success) {
        emit finished(true, m_outputPath);
    } else {
        emit error(m_lastError);
        emit finished(false, m_outputPath);
    }
}

void WebPAnimationEncoder::abort()
{
    if (!m_running) {
        return;
    }

    qDebug() << "WebPAnimationEncoder: Aborting";
    m_aborted = true;

    cleanup();

    // Remove incomplete output file
    if (!m_outputPath.isEmpty() && QFile::exists(m_outputPath)) {
        QFile::remove(m_outputPath);
    }
}

void WebPAnimationEncoder::cleanup()
{
    if (m_data) {
        if (m_data->encoder) {
            WebPAnimEncoderDelete(m_data->encoder);
            m_data->encoder = nullptr;
        }
        delete m_data;
        m_data = nullptr;
    }
    m_running = false;
}
