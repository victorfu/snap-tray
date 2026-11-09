#include "encoding/WebPAnimEncoder.h"
#include <QDebug>
#include <QFile>
#include <QRect>

#include <cmath>
#include <cstring>

#include <webp/encode.h>
#include <webp/mux.h>

// Internal data structure to hold libwebp encoder state
namespace {

// Bounding box of the pixels that differ between two same-sized RGBA8888
// frames; empty when they match.
QRect changedBounds(const QImage &previous, const QImage &current)
{
    if (previous.isNull() || previous.size() != current.size()
        || previous.format() != current.format()) {
        return current.rect();
    }
    const int rowBytes = current.width() * 4;
    int top = -1;
    int bottom = -1;
    for (int y = 0; y < current.height(); ++y) {
        if (std::memcmp(previous.constScanLine(y), current.constScanLine(y), rowBytes) != 0) {
            if (top < 0) top = y;
            bottom = y;
        }
    }
    if (top < 0) {
        return QRect();
    }
    int left = current.width();
    int right = -1;
    for (int y = top; y <= bottom; ++y) {
        const auto *a = reinterpret_cast<const quint32 *>(previous.constScanLine(y));
        const auto *b = reinterpret_cast<const quint32 *>(current.constScanLine(y));
        for (int x = 0; x < left; ++x) {
            if (a[x] != b[x]) { left = x; break; }
        }
        for (int x = current.width() - 1; x > right; --x) {
            if (a[x] != b[x]) { right = x; break; }
        }
    }
    return QRect(QPoint(left, top), QPoint(right, bottom));
}

// Bytes libwebp produces for `image` encoded on its own at `quality`, 0 on
// failure.
qint64 encodedBytes(const QImage& image, float quality)
{
    WebPConfig config;
    WebPPicture picture;
    if (!WebPConfigInit(&config) || !WebPPictureInit(&picture)) return 0;
    config.quality = quality;
    config.method = 0;
    picture.width = image.width();
    picture.height = image.height();
    picture.use_argb = 1;
    WebPMemoryWriter writer;
    WebPMemoryWriterInit(&writer);
    picture.writer = WebPMemoryWrite;
    picture.custom_ptr = &writer;
    qint64 bytes = 0;
    if (WebPPictureImportRGBA(&picture, image.constBits(), int(image.bytesPerLine()))
        && WebPEncode(&config, &picture)) {
        bytes = qint64(writer.size);
    }
    WebPPictureFree(&picture);
    WebPMemoryWriterClear(&writer);
    return bytes;
}

constexpr int kSampleGrid = 4;      // tiles along a sampled side
constexpr int kSampleTileSide = 64;

// A side longer than this is sampled with tiles; a shorter one is taken whole.
bool sampledSide(int length)
{
    return length > kSampleGrid * kSampleTileSide;
}

bool sampledRegion(const QRect& region)
{
    return sampledSide(region.width()) || sampledSide(region.height());
}

// What the animation stores for the changed `region` of `current`: measured
// by encoding it, not assumed from a fixed rate that falls far short for
// content that does not compress (#171 review). Pixels unchanged since
// `previous` are transparent, as the animation encoder's blending makes
// them. A long side is sampled with full-resolution tiles evenly spread
// along it (a short side is taken whole), and the result scaled up by area
// with a margin for sampling. 0 when it cannot be measured; `sampledRate`
// gets the rate per pixel when the region was sampled.
qint64 measuredRegionBytes(const QImage& previous, const QImage& current, const QRect& region, float quality,
                           double* sampledRate)
{
    constexpr double kSamplingMargin = 1.25;
    const int tilesX = sampledSide(region.width()) ? kSampleGrid : 1;
    const int tilesY = sampledSide(region.height()) ? kSampleGrid : 1;
    const int tileWidth = tilesX > 1 ? kSampleTileSide : region.width();
    const int tileHeight = tilesY > 1 ? kSampleTileSide : region.height();
    if (tileWidth < 1 || tileHeight < 1) return 0;
    QImage sample(tileWidth * tilesX, tileHeight * tilesY, QImage::Format_RGBA8888);
    const bool hasPrevious = !previous.isNull() && previous.size() == current.size()
        && previous.format() == current.format();
    for (int ty = 0; ty < tilesY; ++ty) {
        for (int tx = 0; tx < tilesX; ++tx) {
            // Each tile at the centre of its cell of the region.
            const int left = region.left()
                + (tilesX == 1 ? 0 : (region.width() * (2 * tx + 1)) / (2 * tilesX) - tileWidth / 2);
            const int top = region.top()
                + (tilesY == 1 ? 0 : (region.height() * (2 * ty + 1)) / (2 * tilesY) - tileHeight / 2);
            for (int y = 0; y < tileHeight; ++y) {
                const auto* now = reinterpret_cast<const quint32*>(current.constScanLine(top + y)) + left;
                const auto* before = hasPrevious
                    ? reinterpret_cast<const quint32*>(previous.constScanLine(top + y)) + left : nullptr;
                auto* out = reinterpret_cast<quint32*>(sample.scanLine(ty * tileHeight + y)) + tx * tileWidth;
                for (int x = 0; x < tileWidth; ++x) {
                    out[x] = before && before[x] == now[x] ? 0u : now[x];
                }
            }
        }
    }
    const qint64 bytes = encodedBytes(sample, quality);
    if (bytes <= 0) return 0;
    if (tilesX == 1 && tilesY == 1) return bytes;
    const double perPixel = double(bytes) / (double(sample.width()) * sample.height()) * kSamplingMargin;
    if (sampledRate) *sampledRate = perPixel;
    return qint64(std::ceil(perPixel * double(region.width()) * region.height()));
}

} // namespace

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
    m_frameRate = qBound(1, frameRate, 240);
    m_framesWritten = 0;
    m_bufferedBytes->store(0);
    m_lastCountedFrame = QImage();
    m_sampledBytesPerPixel = 0;
    m_framesSinceMeasure = 0;
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

    // Set quality and speed
    m_data->config.quality = static_cast<float>(m_quality);
    m_data->config.method = 0;  // 0-6, 0 = fastest encoding
    m_data->config.thread_level = 1;  // Enable multi-threaded encoding
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

    // Ensure monotonically increasing (at least 1ms gap)
    if (m_framesWritten > 0 && currentTimestampMs <= m_lastTimestampMs) {
        currentTimestampMs = static_cast<int>(qMin(m_lastTimestampMs + 1,
                                                   static_cast<qint64>(INT_MAX)));
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

    // Import RGBA data. With use_argb set, libwebp (pinned v1.3.2) allocates the
    // ARGB buffer inside the import and frees any previous one, so a separate
    // WebPPictureAlloc beforehand would only be discarded. Allocation failure
    // and invalid dimensions surface here as an import failure.
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
    if (m_frameRate <= 0) {
        m_frameRate = 30;  // Defensive fallback
    }
    int frameDurationMs = 1000 / m_frameRate;
    if (timestampMs >= 0 && m_framesWritten > 0) {
        qint64 nextTs = timestampMs + frameDurationMs;
        m_data->timestampMs = nextTs > INT_MAX ? INT_MAX : static_cast<int>(nextTs);
    } else {
        qint64 nextTs = static_cast<qint64>(m_data->timestampMs) + frameDurationMs;
        m_data->timestampMs = nextTs > INT_MAX ? INT_MAX : static_cast<int>(nextTs);
    }

    m_framesWritten++;
    m_lastTimestampMs = currentTimestampMs;
    const QRect changed = changedBounds(m_lastCountedFrame, processed);
    if (!changed.isEmpty()) {
        // Large regions are measured every kMeasureEveryFrames changes; in
        // between, the last measured rate stands (content rarely changes
        // character within a few frames), keeping the cost a few percent.
        const qint64 area = qint64(changed.width()) * changed.height();
        qint64 bytes = 0;
        if (sampledRegion(changed) && m_sampledBytesPerPixel > 0
            && ++m_framesSinceMeasure < kMeasureEveryFrames) {
            bytes = qint64(std::ceil(m_sampledBytesPerPixel * double(area)));
        } else {
            double rate = 0;
            bytes = measuredRegionBytes(m_lastCountedFrame, processed, changed, m_data->config.quality, &rate);
            if (rate > 0) {
                m_sampledBytesPerPixel = rate;
                m_framesSinceMeasure = 0;
            }
        }
        if (bytes <= 0) {
            // Cannot measure: assume the worst a lossy frame needs.
            bytes = qint64(double(area) * kFallbackBytesPerPixel);
        }
        // Sparse changes in a large box (a ticking counter) can fall between
        // the sample tiles; the area floor covers them.
        bytes = std::max(bytes, qint64(double(area) * kMinBytesPerPixel));
        m_bufferedBytes->fetch_add(kEstimatedFrameOverheadBytes + bytes);
        m_lastCountedFrame = processed;
    }

    // Emit progress every 30 frames
    if (m_framesWritten % 30 == 0) {
        emit progress(m_framesWritten);
    }
}

void WebPAnimationEncoder::finish()
{
    if (!m_running || !m_data || !m_data->encoder) {
        emit finished(false, QString());
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
