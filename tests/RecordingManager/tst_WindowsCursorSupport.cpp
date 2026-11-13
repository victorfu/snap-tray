#include <QtTest/QtTest>

#include "recording/WindowsCursorSupport.h"

#include <thread>
#include <vector>

using namespace SnapTray::WindowsCursor;

namespace {

constexpr quint32 kSet = 0x00ffffffu;   // a mask bit that is set
constexpr quint32 kClear = 0x00000000u; // a mask bit that is clear

QRgb pixel(const QImage& image, int x, int y)
{
    return reinterpret_cast<const QRgb*>(image.constScanLine(y))[x];
}

} // namespace

class tst_WindowsCursorSupport : public QObject
{
    Q_OBJECT

private slots:
    void alphaCursorIsPremultiplied();
    void colorCursorWithoutAlphaUsesTheMask();
    void monochromeCursorBlackWhiteAndTransparent();
    void invertingPixelsGetAFillAndAnOutline();
    void invertingColorPixelCountsToo();
    void nothingVisibleIsATransparentPicture();
    void unusableBitmapsGiveNoPicture();
    void hookMessagesNameButtons_data();
    void hookMessagesNameButtons();
    void otherHookMessagesAreIgnored();
    void queueKeepsOrderAndRefusesWhenFull();
    void queueSurvivesAWriterThread();
    void tickAgeHandlesWrapAndFutureStamps();
};

void tst_WindowsCursorSupport::alphaCursorIsPremultiplied()
{
    // 2x1: half-transparent red, fully transparent.
    const std::vector<quint32> color{0x80ff0000u, 0x00000000u};
    const std::vector<quint32> mask{kClear, kClear}; // ignored once the colour has alpha
    const Picture picture = pictureFromBitmaps({2, 1, color.data(), mask.data()});
    QCOMPARE(picture.image.format(), QImage::Format_ARGB32_Premultiplied);
    QCOMPARE(picture.image.size(), QSize(2, 1));
    QCOMPARE(picture.hotspotShift, QPoint(0, 0));
    QCOMPARE(pixel(picture.image, 0, 0), qPremultiply(QRgb(0x80ff0000u)));
    QCOMPARE(qRed(pixel(picture.image, 0, 0)), 0x80);
    QCOMPARE(qAlpha(pixel(picture.image, 1, 0)), 0);
}

void tst_WindowsCursorSupport::colorCursorWithoutAlphaUsesTheMask()
{
    // 2x1 without alpha: an opaque blue pixel, and one the mask makes transparent.
    const std::vector<quint32> color{0x000000ffu, 0x00000000u};
    const std::vector<quint32> mask{kClear, kSet};
    const Picture picture = pictureFromBitmaps({2, 1, color.data(), mask.data()});
    QCOMPARE(picture.image.size(), QSize(2, 1));
    QCOMPARE(pixel(picture.image, 0, 0), QRgb(0xff0000ffu));
    QCOMPARE(qAlpha(pixel(picture.image, 1, 0)), 0);
}

void tst_WindowsCursorSupport::monochromeCursorBlackWhiteAndTransparent()
{
    // 3x1: AND row then XOR row. black, white, transparent.
    const std::vector<quint32> mask{
        kClear, kClear, kSet,  // AND
        kClear, kSet, kClear,  // XOR
    };
    const Picture picture = pictureFromBitmaps({3, 1, nullptr, mask.data()});
    QCOMPARE(picture.image.size(), QSize(3, 1));
    QCOMPARE(picture.hotspotShift, QPoint(0, 0));
    QCOMPARE(pixel(picture.image, 0, 0), QRgb(0xff000000u));
    QCOMPARE(pixel(picture.image, 1, 0), QRgb(0xffffffffu));
    QCOMPARE(qAlpha(pixel(picture.image, 2, 0)), 0);
}

void tst_WindowsCursorSupport::invertingPixelsGetAFillAndAnOutline()
{
    // 3x3 monochrome with one inverting pixel in the middle (AND and XOR set),
    // as in the classic I-beam. Everything else is transparent.
    std::vector<quint32> mask(3 * 3 * 2, kClear);
    for (int i = 0; i < 9; ++i) mask[size_t(i)] = kSet;      // AND: all transparent or inverting
    mask[size_t(9 + 4)] = kSet;                              // XOR: the centre inverts
    const Picture picture = pictureFromBitmaps({3, 3, nullptr, mask.data()});
    // Room for the outline on every side, and the hotspot moves with it.
    QCOMPARE(picture.image.size(), QSize(3 + 2 * kOutlineBorder, 3 + 2 * kOutlineBorder));
    QCOMPARE(picture.hotspotShift, QPoint(kOutlineBorder, kOutlineBorder));
    const int c = 1 + kOutlineBorder;
    QCOMPARE(pixel(picture.image, c, c), QRgb(0xff000000u));
    // The eight neighbours are the light outline; beyond them nothing is drawn.
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx || dy) QCOMPARE(pixel(picture.image, c + dx, c + dy), QRgb(0xffffffffu));
        }
    }
    QCOMPARE(qAlpha(pixel(picture.image, 0, 0)), 0);
    QCOMPARE(qAlpha(pixel(picture.image, 4, 4)), 0);

    // On the bitmap's edge the outline uses the added border.
    std::vector<quint32> edge(1 * 1 * 2, kSet);
    const Picture single = pictureFromBitmaps({1, 1, nullptr, edge.data()});
    QCOMPARE(single.image.size(), QSize(3, 3));
    QCOMPARE(pixel(single.image, 1, 1), QRgb(0xff000000u));
    QCOMPARE(pixel(single.image, 0, 0), QRgb(0xffffffffu));
    QCOMPARE(pixel(single.image, 2, 2), QRgb(0xffffffffu));
}

void tst_WindowsCursorSupport::invertingColorPixelCountsToo()
{
    // A colour cursor without alpha: mask set and a non-black colour inverts.
    const std::vector<quint32> color{0x00ffffffu};
    const std::vector<quint32> mask{kSet};
    const Picture picture = pictureFromBitmaps({1, 1, color.data(), mask.data()});
    QCOMPARE(picture.image.size(), QSize(3, 3));
    QCOMPARE(pixel(picture.image, 1, 1), QRgb(0xff000000u));
}

void tst_WindowsCursorSupport::nothingVisibleIsATransparentPicture()
{
    const std::vector<quint32> color(4, 0u);
    const std::vector<quint32> mask(4, kSet);
    const Picture picture = pictureFromBitmaps({2, 2, color.data(), mask.data()});
    QVERIFY(!picture.image.isNull());
    QCOMPARE(picture.image.size(), QSize(2, 2));
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) QCOMPARE(qAlpha(pixel(picture.image, x, y)), 0);
    }
}

void tst_WindowsCursorSupport::unusableBitmapsGiveNoPicture()
{
    const std::vector<quint32> one{kClear};
    QVERIFY(pictureFromBitmaps({}).image.isNull());
    QVERIFY(pictureFromBitmaps({0, 1, nullptr, one.data()}).image.isNull());
    QVERIFY(pictureFromBitmaps({1, 1, one.data(), nullptr}).image.isNull());
    QVERIFY(pictureFromBitmaps({100000, 1, nullptr, one.data()}).image.isNull());
}

void tst_WindowsCursorSupport::hookMessagesNameButtons_data()
{
    QTest::addColumn<quint32>("message");
    QTest::addColumn<quint32>("mouseData");
    QTest::addColumn<Qt::MouseButton>("button");
    QTest::addColumn<bool>("down");
    QTest::newRow("left down") << 0x0201u << 0u << Qt::LeftButton << true;
    QTest::newRow("left up") << 0x0202u << 0u << Qt::LeftButton << false;
    QTest::newRow("right down") << 0x0204u << 0u << Qt::RightButton << true;
    QTest::newRow("right up") << 0x0205u << 0u << Qt::RightButton << false;
    QTest::newRow("middle down") << 0x0207u << 0u << Qt::MiddleButton << true;
    QTest::newRow("middle up") << 0x0208u << 0u << Qt::MiddleButton << false;
    QTest::newRow("back down") << 0x020Bu << (1u << 16) << Qt::BackButton << true;
    QTest::newRow("back up") << 0x020Cu << (1u << 16) << Qt::BackButton << false;
    QTest::newRow("forward down") << 0x020Bu << (2u << 16) << Qt::ForwardButton << true;
    QTest::newRow("forward up") << 0x020Cu << (2u << 16) << Qt::ForwardButton << false;
}

void tst_WindowsCursorSupport::hookMessagesNameButtons()
{
    QFETCH(quint32, message);
    QFETCH(quint32, mouseData);
    QFETCH(Qt::MouseButton, button);
    QFETCH(bool, down);
    const auto change = buttonChangeFromHookMessage(message, mouseData);
    QVERIFY(change.has_value());
    QCOMPARE(change->button, button);
    QCOMPARE(change->down, down);
}

void tst_WindowsCursorSupport::otherHookMessagesAreIgnored()
{
    QVERIFY(!buttonChangeFromHookMessage(0x0200u, 0u));        // WM_MOUSEMOVE
    QVERIFY(!buttonChangeFromHookMessage(0x020Au, 120u << 16)); // WM_MOUSEWHEEL
    QVERIFY(!buttonChangeFromHookMessage(0x020Eu, 120u << 16)); // WM_MOUSEHWHEEL
    QVERIFY(!buttonChangeFromHookMessage(0x020Bu, 3u << 16));   // an X button that is not recorded
    QVERIFY(!buttonChangeFromHookMessage(0x020Bu, 0u));
}

void tst_WindowsCursorSupport::queueKeepsOrderAndRefusesWhenFull()
{
    SpscQueue<int, 3> queue;
    QVERIFY(!queue.pop().has_value());
    QVERIFY(queue.push(1));
    QVERIFY(queue.push(2));
    QVERIFY(queue.push(3));
    QVERIFY(!queue.push(4)); // full: the new one is refused, nothing is overwritten
    QCOMPARE(queue.pop().value_or(-1), 1);
    QVERIFY(queue.push(5));  // room again, across the wrap
    QCOMPARE(queue.pop().value_or(-1), 2);
    QCOMPARE(queue.pop().value_or(-1), 3);
    QCOMPARE(queue.pop().value_or(-1), 5);
    QVERIFY(!queue.pop().has_value());
}

void tst_WindowsCursorSupport::queueSurvivesAWriterThread()
{
    constexpr int kCount = 20000;
    SpscQueue<int, 64> queue;
    std::thread writer([&queue] {
        for (int i = 0; i < kCount;) {
            if (queue.push(i)) ++i;
            else std::this_thread::yield();
        }
    });
    int expected = 0;
    while (expected < kCount) {
        if (const auto value = queue.pop()) {
            QCOMPARE(*value, expected);
            ++expected;
        } else {
            std::this_thread::yield();
        }
    }
    writer.join();
    QVERIFY(!queue.pop().has_value());
}

void tst_WindowsCursorSupport::tickAgeHandlesWrapAndFutureStamps()
{
    QCOMPARE(tickAgeMs(1000u, 900u), qint64(100));
    QCOMPARE(tickAgeMs(1000u, 1000u), qint64(0));
    QCOMPARE(tickAgeMs(1000u, 1010u), qint64(0));          // stamped after "now"
    QCOMPARE(tickAgeMs(5u, 0xfffffff0u), qint64(21));      // across the 49-day wrap
}

QTEST_APPLESS_MAIN(tst_WindowsCursorSupport)
#include "tst_WindowsCursorSupport.moc"
