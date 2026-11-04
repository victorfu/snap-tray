#include <QtTest/QtTest>
#include <QScopeGuard>

#include "video/MediaFoundationFrameCopy_win.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <mfapi.h>
#include <mfobjects.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
#endif

namespace {
QImage testImage(const QSize& size)
{
    QImage image(size, QImage::Format_RGB32);
    for (int y = 0; y < size.height(); ++y) {
        for (int x = 0; x < size.width(); ++x) {
            image.setPixel(x, y, qRgb(20 + y * 30, 40 + x * 20, 60 + x + y));
        }
    }
    return image;
}
}

class tst_MediaFoundationFrameCopy : public QObject
{
    Q_OBJECT

private slots:
    void signedPitch_data();
    void signedPitch();
    void unusedByteIsOpaque_data();
    void unusedByteIsOpaque();
#ifdef Q_OS_WIN
    void nativeBuffer_data();
    void nativeBuffer();
#endif
};

void tst_MediaFoundationFrameCopy::signedPitch_data()
{
    QTest::addColumn<int>("pitch");
    QTest::addColumn<int>("height");
    QTest::newRow("top-down") << 8 << 3;
    QTest::newRow("bottom-up") << -8 << 3;
    QTest::newRow("top-down-padded") << 16 << 3;
    QTest::newRow("bottom-up-padded") << -16 << 3;
    QTest::newRow("single-row-negative") << -16 << 1;
}

void tst_MediaFoundationFrameCopy::signedPitch()
{
    QFETCH(int, pitch);
    QFETCH(int, height);
    const QImage expected = testImage(QSize(2, height));
    const int stride = qAbs(pitch);
    // Extra sentinel rows make the old out-of-frame reads fail pixel checks
    // deterministically, without relying on an allocator crash or sanitizers.
    QByteArray storage(stride * height * 2, char(0xA5));
    for (int y = 0; y < height; ++y) {
        const int memoryRow = pitch < 0 ? height - 1 - y : y;
        std::memcpy(storage.data() + memoryRow * stride,
                    expected.constScanLine(y), expected.bytesPerLine());
    }
    const QByteArray before(storage.constData(), storage.size());
    const auto* scanline0 = reinterpret_cast<const uchar*>(storage.constData())
        + (pitch < 0 ? (height - 1) * stride : 0);

    const QImage actual = copyMediaFoundationRgb32Frame(scanline0, pitch, expected.size());
    QCOMPARE(actual, expected);
    QCOMPARE(storage, before);
    // The returned frame must remain valid after its source is unlocked/reused.
    storage.fill(char(0));
    QCOMPARE(actual, expected);
}

void tst_MediaFoundationFrameCopy::unusedByteIsOpaque_data()
{
    QTest::addColumn<int>("unused");
    QTest::newRow("zero") << 0;
    QTest::newRow("partial") << 85;
    QTest::newRow("opaque") << 255;
}

void tst_MediaFoundationFrameCopy::unusedByteIsOpaque()
{
    QFETCH(int, unused);
    const QRgb native[] = {qRgba(255, 255, 255, unused), qRgba(40, 80, 120, unused)};
    const QImage frame = copyMediaFoundationRgb32Frame(reinterpret_cast<const uchar*>(native),
                                                       sizeof(native), QSize(2, 1));
    // Qt's RGB32 contract is 0xffRRGGBB; MF's unused X byte is not alpha.
    const auto* pixels = reinterpret_cast<const QRgb*>(frame.constBits());
    QCOMPARE(pixels[0], qRgb(255, 255, 255));
    QCOMPARE(pixels[1], qRgb(40, 80, 120));
    const QImage texture = frame.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QCOMPARE(texture.pixelColor(0, 0), QColor(Qt::white));
    QCOMPARE(texture.pixelColor(1, 0), QColor(40, 80, 120));
    QCOMPARE(qAlpha(native[0]), unused);
}

#ifdef Q_OS_WIN
void tst_MediaFoundationFrameCopy::nativeBuffer_data()
{
    QTest::addColumn<bool>("bottomUp");
    QTest::newRow("top-down") << false;
    QTest::newRow("bottom-up") << true;
}

void tst_MediaFoundationFrameCopy::nativeBuffer()
{
    QFETCH(bool, bottomUp);
    QVERIFY(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)));
    const auto uninitialize = qScopeGuard([] { CoUninitialize(); });
    QVERIFY(SUCCEEDED(MFStartup(MF_VERSION)));
    const auto shutdown = qScopeGuard([] { MFShutdown(); });

    const QImage expected = testImage(QSize(2, 3));
    ComPtr<IMFMediaBuffer> buffer;
    QVERIFY(SUCCEEDED(MFCreate2DMediaBuffer(expected.width(), expected.height(),
                                          MFVideoFormat_RGB32.Data1, bottomUp, &buffer)));
    ComPtr<IMF2DBuffer> buffer2D;
    QVERIFY(SUCCEEDED(buffer.As(&buffer2D)));
    BYTE* scanline0 = nullptr;
    LONG pitch = 0;
    QVERIFY(SUCCEEDED(buffer2D->Lock2D(&scanline0, &pitch)));
    QImage actual;
    {
        const auto unlock = qScopeGuard([&] { buffer2D->Unlock2D(); });
        QCOMPARE(pitch < 0, bottomUp);
        QVERIFY(qAbs(pitch) >= expected.bytesPerLine());
        for (int y = 0; y < expected.height(); ++y) {
            std::memcpy(scanline0 + qsizetype(y) * pitch,
                        expected.constScanLine(y), expected.bytesPerLine());
        }
        actual = copyMediaFoundationRgb32Frame(scanline0, pitch, expected.size());
    }
    buffer2D.Reset();
    buffer.Reset();
    QCOMPARE(actual, expected);
}
#endif

QTEST_GUILESS_MAIN(tst_MediaFoundationFrameCopy)
#include "tst_MediaFoundationFrameCopy_win.moc"
