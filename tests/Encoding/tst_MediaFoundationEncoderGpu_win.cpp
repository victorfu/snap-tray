#include "MediaFoundationEncoder.h"
#include "capture/D3D11Frame.h"

#include <QtTest>
#include <QColor>
#include <QImage>
#include <QTemporaryDir>

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
using namespace SnapTray;

namespace {

const QSize kFrameSize(640, 360);
constexpr int kFrameRate = 30;
// Solid frames survive H.264 almost exactly; a wrong YUV matrix moves
// saturated colors by 20+ levels, which this tolerance catches.
constexpr int kColorTolerance = 12;

std::shared_ptr<D3D11Frame> solidFrame(D3D11TexturePool &pool, const QColor &color)
{
    auto frame = pool.acquire();
    if (!frame) return nullptr;
    ComPtr<ID3D11RenderTargetView> target;
    if (FAILED(frame->device->device->CreateRenderTargetView(frame->texture.Get(), nullptr, &target))) {
        return nullptr;
    }
    const float rgba[4] = {float(color.redF()), float(color.greenF()), float(color.blueF()), 1.0f};
    frame->device->context->ClearRenderTargetView(target.Get(), rgba);
    return frame;
}

QImage solidImage(const QColor &color)
{
    QImage image(kFrameSize, QImage::Format_ARGB32);
    image.fill(color);
    return image;
}

struct Decoded {
    QSize size;
    QList<QColor> centers;  // center pixel of every decoded frame
    QString error;
};

Decoded decodeCenters(const QString &path)
{
    Decoded result;
    ComPtr<IMFAttributes> attributes;
    MFCreateAttributes(&attributes, 1);
    attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    ComPtr<IMFSourceReader> reader;
    if (FAILED(MFCreateSourceReaderFromURL(reinterpret_cast<LPCWSTR>(path.utf16()), attributes.Get(), &reader))) {
        result.error = QStringLiteral("cannot open output");
        return result;
    }
    const DWORD stream = DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
    ComPtr<IMFMediaType> rgb;
    MFCreateMediaType(&rgb);
    rgb->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    rgb->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (FAILED(reader->SetCurrentMediaType(stream, nullptr, rgb.Get()))) {
        result.error = QStringLiteral("no RGB32 output");
        return result;
    }
    ComPtr<IMFMediaType> current;
    reader->GetCurrentMediaType(stream, &current);
    UINT32 width = 0;
    UINT32 height = 0;
    MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, &width, &height);
    result.size = QSize(int(width), int(height));
    const LONG stride = LONG(MFGetAttributeUINT32(current.Get(), MF_MT_DEFAULT_STRIDE, width * 4));

    for (;;) {
        DWORD flags = 0;
        ComPtr<IMFSample> sample;
        if (FAILED(reader->ReadSample(stream, 0, nullptr, &flags, nullptr, &sample))) {
            result.error = QStringLiteral("decode failed");
            break;
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (!sample) continue;
        ComPtr<IMFMediaBuffer> buffer;
        sample->ConvertToContiguousBuffer(&buffer);
        BYTE *data = nullptr;
        DWORD length = 0;
        if (FAILED(buffer->Lock(&data, nullptr, &length))) continue;
        const int row = int(height) / 2;
        // A negative stride is a bottom-up image: row 0 is the last line.
        const BYTE *line = stride >= 0 ? data + row * stride : data + (int(height) - 1 - row) * -stride;
        const QRgb pixel = reinterpret_cast<const QRgb *>(line)[width / 2];
        result.centers.append(QColor(pixel));
        buffer->Unlock();
    }
    return result;
}

bool colorNear(const QColor &actual, const QColor &expected)
{
    return qAbs(actual.red() - expected.red()) <= kColorTolerance
        && qAbs(actual.green() - expected.green()) <= kColorTolerance
        && qAbs(actual.blue() - expected.blue()) <= kColorTolerance;
}

QString describe(const QColor &color)
{
    return QStringLiteral("rgb(%1,%2,%3)").arg(color.red()).arg(color.green()).arg(color.blue());
}

bool finish(MediaFoundationEncoder &encoder)
{
    bool success = false;
    QObject::connect(&encoder, &IVideoEncoder::finished, &encoder,
                     [&success](bool ok, const QString &) { success = ok; });
    encoder.finish();
    return success;
}

} // namespace

class TestMediaFoundationEncoderGpu : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testNativeFramesEncodeOnTheGpu();
    void testCpuFramesJoinTheGpuStream();
    void testFramesFromAnotherDeviceAreReadBack();
    void testWithoutDeviceFramesStayOnTheCpu();
    void testSoftwareEncodingKeepsCpuFrames();

private:
    // A started GPU-input encoder, or a skip reason.
    QString startGpuEncoder(MediaFoundationEncoder &encoder, std::shared_ptr<D3D11FrameDevice> &device,
                            const QString &path);
    QTemporaryDir m_dir;
    bool m_mfStarted = false;
};

void TestMediaFoundationEncoderGpu::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_mfStarted = SUCCEEDED(MFStartup(MF_VERSION));
    QVERIFY(m_mfStarted);
}

void TestMediaFoundationEncoderGpu::cleanupTestCase()
{
    if (m_mfStarted) MFShutdown();
}

QString TestMediaFoundationEncoderGpu::startGpuEncoder(MediaFoundationEncoder &encoder,
                                                       std::shared_ptr<D3D11FrameDevice> &device,
                                                       const QString &path)
{
    device = D3D11FrameDevice::create();
    if (!device || !device->videoSupport) {
        return QStringLiteral("no hardware D3D11 device with video support");
    }
    encoder.setNativeFrameDevice(device);
    if (!encoder.start(path, kFrameSize, kFrameRate)) {
        return QStringLiteral("encoder did not start: %1").arg(encoder.lastError());
    }
    if (!encoder.acceptsNativeFrames()) {
        // The probe decided against this adapter; the log says why.
        return QStringLiteral("GPU input unavailable on %1").arg(device->adapterName);
    }
    return {};
}

void TestMediaFoundationEncoderGpu::testNativeFramesEncodeOnTheGpu()
{
    const QString path = m_dir.filePath(QStringLiteral("gpu.mp4"));
    MediaFoundationEncoder encoder;
    std::shared_ptr<D3D11FrameDevice> device;
    if (const QString skip = startGpuEncoder(encoder, device, path); !skip.isEmpty()) {
        QSKIP(qPrintable(skip));
    }
    auto pool = D3D11TexturePool::create(device, kFrameSize);
    QVERIFY(pool);

    constexpr int kFrames = 30;
    for (int i = 0; i < kFrames; ++i) {
        auto frame = solidFrame(*pool, i < kFrames / 2 ? QColor(Qt::red) : QColor(Qt::blue));
        QVERIFY(frame);
        encoder.writeNativeFrame(frame, qint64(i) * 1000 / kFrameRate);
        // The encoder converts right away and keeps no reference: the
        // capture pool can reuse the texture as soon as the frame is dropped.
        QCOMPARE(frame.use_count(), 1L);
    }
    QCOMPARE(encoder.framesWritten(), qint64(kFrames));
    QVERIFY2(finish(encoder), qPrintable(encoder.lastError()));

    const Decoded decoded = decodeCenters(path);
    QVERIFY2(decoded.error.isEmpty(), qPrintable(decoded.error));
    QCOMPARE(decoded.size, kFrameSize);
    QVERIFY2(decoded.centers.size() >= kFrames - 2, qPrintable(QString::number(decoded.centers.size())));
    QVERIFY2(colorNear(decoded.centers.first(), Qt::red), qPrintable(describe(decoded.centers.first())));
    QVERIFY2(colorNear(decoded.centers.last(), Qt::blue), qPrintable(describe(decoded.centers.last())));
}

void TestMediaFoundationEncoderGpu::testCpuFramesJoinTheGpuStream()
{
    // Watermarked frames and capture fallbacks arrive as images in the same
    // stream: they are uploaded and converted with the same matrix.
    const QString path = m_dir.filePath(QStringLiteral("mixed.mp4"));
    MediaFoundationEncoder encoder;
    std::shared_ptr<D3D11FrameDevice> device;
    if (const QString skip = startGpuEncoder(encoder, device, path); !skip.isEmpty()) {
        QSKIP(qPrintable(skip));
    }
    auto pool = D3D11TexturePool::create(device, kFrameSize);
    QVERIFY(pool);
    const QColor green(0, 160, 0);
    const QColor magenta(200, 0, 200);
    for (int i = 0; i < 10; ++i) {
        encoder.writeNativeFrame(solidFrame(*pool, green), qint64(i) * 1000 / kFrameRate);
    }
    for (int i = 10; i < 20; ++i) {
        encoder.writeFrame(solidImage(magenta), qint64(i) * 1000 / kFrameRate);
    }
    QCOMPARE(encoder.framesWritten(), qint64(20));
    QVERIFY2(finish(encoder), qPrintable(encoder.lastError()));

    const Decoded decoded = decodeCenters(path);
    QVERIFY2(decoded.error.isEmpty(), qPrintable(decoded.error));
    QVERIFY(!decoded.centers.isEmpty());
    QVERIFY2(colorNear(decoded.centers.first(), green), qPrintable(describe(decoded.centers.first())));
    QVERIFY2(colorNear(decoded.centers.last(), magenta), qPrintable(describe(decoded.centers.last())));
}

void TestMediaFoundationEncoderGpu::testFramesFromAnotherDeviceAreReadBack()
{
    // A capture device recreated mid-recording (device removed) hands over
    // textures the encoder's device cannot use; the stream keeps going.
    const QString path = m_dir.filePath(QStringLiteral("other-device.mp4"));
    MediaFoundationEncoder encoder;
    std::shared_ptr<D3D11FrameDevice> device;
    if (const QString skip = startGpuEncoder(encoder, device, path); !skip.isEmpty()) {
        QSKIP(qPrintable(skip));
    }
    auto otherDevice = D3D11FrameDevice::create();
    QVERIFY(otherDevice);
    auto otherPool = D3D11TexturePool::create(otherDevice, kFrameSize);
    QVERIFY(otherPool);
    const QColor yellow(230, 200, 0);
    for (int i = 0; i < 10; ++i) {
        encoder.writeNativeFrame(solidFrame(*otherPool, yellow), qint64(i) * 1000 / kFrameRate);
    }
    QCOMPARE(encoder.framesWritten(), qint64(10));
    QVERIFY2(finish(encoder), qPrintable(encoder.lastError()));
    const Decoded decoded = decodeCenters(path);
    QVERIFY2(decoded.error.isEmpty(), qPrintable(decoded.error));
    QVERIFY(!decoded.centers.isEmpty());
    QVERIFY2(colorNear(decoded.centers.last(), yellow), qPrintable(describe(decoded.centers.last())));
}

void TestMediaFoundationEncoderGpu::testWithoutDeviceFramesStayOnTheCpu()
{
    const QString path = m_dir.filePath(QStringLiteral("cpu.mp4"));
    MediaFoundationEncoder encoder;
    QVERIFY2(encoder.start(path, kFrameSize, kFrameRate), qPrintable(encoder.lastError()));
    QVERIFY(!encoder.acceptsNativeFrames());
    for (int i = 0; i < 10; ++i) {
        encoder.writeFrame(solidImage(Qt::white), qint64(i) * 1000 / kFrameRate);
    }
    QCOMPARE(encoder.framesWritten(), qint64(10));
    QVERIFY2(finish(encoder), qPrintable(encoder.lastError()));
    const Decoded decoded = decodeCenters(path);
    QVERIFY2(decoded.error.isEmpty(), qPrintable(decoded.error));
    QCOMPARE(decoded.size, kFrameSize);
    QVERIFY(!decoded.centers.isEmpty());
}

void TestMediaFoundationEncoderGpu::testSoftwareEncodingKeepsCpuFrames()
{
    auto device = D3D11FrameDevice::create();
    if (!device) {
        QSKIP("no hardware D3D11 device");
    }
    const QString path = m_dir.filePath(QStringLiteral("software.mp4"));
    MediaFoundationEncoder encoder(false, nullptr);
    encoder.setNativeFrameDevice(device);
    QVERIFY2(encoder.start(path, kFrameSize, kFrameRate), qPrintable(encoder.lastError()));
    QVERIFY(!encoder.acceptsNativeFrames());
    // A native frame still lands in the CPU stream through a readback.
    auto pool = D3D11TexturePool::create(device, kFrameSize);
    QVERIFY(pool);
    encoder.writeNativeFrame(solidFrame(*pool, Qt::white), 0);
    QCOMPARE(encoder.framesWritten(), qint64(1));
    encoder.abort();
}

QTEST_MAIN(TestMediaFoundationEncoderGpu)
#include "tst_MediaFoundationEncoderGpu_win.moc"
