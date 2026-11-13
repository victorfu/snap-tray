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
    void bitmapsTooShortForTheirSizeGiveNoPicture();
    void edgeLimitIsInclusive();
    void alphaIgnoresTheMask();
    void outlineLeavesOpaquePixelsAlone();
    void injectedEventsAreNotRecorded();
    void pumpDeliversInOrderWithAges();
    void pumpIgnoresADrainFromInsideAHandler();
    void pumpCountsWhatDidNotFit();
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
    const Picture picture = pictureFromBitmaps({2, 1, color, mask});
    QCOMPARE(picture.image.format(), QImage::Format_ARGB32_Premultiplied);
    QCOMPARE(picture.image.size(), QSize(2, 1));
    QCOMPARE(picture.hotspotShift, QPoint(0, 0));
    // Half-transparent pure red, premultiplied: red is scaled by the alpha.
    QCOMPARE(pixel(picture.image, 0, 0), QRgb(0x80800000u));
    QCOMPARE(qAlpha(pixel(picture.image, 1, 0)), 0);
}

void tst_WindowsCursorSupport::colorCursorWithoutAlphaUsesTheMask()
{
    // 2x1 without alpha: an opaque blue pixel, and one the mask makes transparent.
    const std::vector<quint32> color{0x000000ffu, 0x00000000u};
    const std::vector<quint32> mask{kClear, kSet};
    const Picture picture = pictureFromBitmaps({2, 1, color, mask});
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
    const Picture picture = pictureFromBitmaps({3, 1, {}, mask});
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
    const Picture picture = pictureFromBitmaps({3, 3, {}, mask});
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
    const Picture single = pictureFromBitmaps({1, 1, {}, edge});
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
    const Picture picture = pictureFromBitmaps({1, 1, color, mask});
    QCOMPARE(picture.image.size(), QSize(3, 3));
    QCOMPARE(pixel(picture.image, 1, 1), QRgb(0xff000000u));
}

void tst_WindowsCursorSupport::nothingVisibleIsATransparentPicture()
{
    const std::vector<quint32> color(4, 0u);
    const std::vector<quint32> mask(4, kSet);
    const Picture picture = pictureFromBitmaps({2, 2, color, mask});
    QVERIFY(!picture.image.isNull());
    QCOMPARE(picture.image.size(), QSize(2, 2));
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) QCOMPARE(qAlpha(pixel(picture.image, x, y)), 0);
    }
}

void tst_WindowsCursorSupport::unusableBitmapsGiveNoPicture()
{
    const std::vector<quint32> one{kClear};
    const std::vector<quint32> two{kClear, kClear};
    QVERIFY(pictureFromBitmaps({}).image.isNull());
    QVERIFY(pictureFromBitmaps({0, 1, {}, two}).image.isNull());
    QVERIFY(pictureFromBitmaps({1, 1, one, {}}).image.isNull());
    QVERIFY(pictureFromBitmaps({100000, 1, {}, two}).image.isNull());
}

void tst_WindowsCursorSupport::bitmapsTooShortForTheirSizeGiveNoPicture()
{
    const std::vector<quint32> three(3, kClear);
    const std::vector<quint32> four(4, kClear);
    // A monochrome mask holds two pictures: 2x1 needs four pixels, not three or two.
    QVERIFY(pictureFromBitmaps({2, 1, {}, three}).image.isNull());
    QVERIFY(!pictureFromBitmaps({2, 1, {}, four}).image.isNull());
    // A colour cursor needs width * height of each.
    QVERIFY(pictureFromBitmaps({2, 2, three, four}).image.isNull());
    QVERIFY(pictureFromBitmaps({2, 2, four, three}).image.isNull());
    QVERIFY(!pictureFromBitmaps({2, 2, four, four}).image.isNull());
}

void tst_WindowsCursorSupport::edgeLimitIsInclusive()
{
    const std::vector<quint32> atLimit(std::size_t(kMaxCursorEdge) * 2, kClear);
    QCOMPARE(pictureFromBitmaps({kMaxCursorEdge, 1, {}, atLimit}).image.size(), QSize(kMaxCursorEdge, 1));
    const std::vector<quint32> beyond(std::size_t(kMaxCursorEdge + 1) * 2, kClear);
    QVERIFY(pictureFromBitmaps({kMaxCursorEdge + 1, 1, {}, beyond}).image.isNull());
}

void tst_WindowsCursorSupport::alphaIgnoresTheMask()
{
    // With alpha in the colour bitmap, a set mask bit hides nothing.
    const std::vector<quint32> color{0xff00ff00u};
    const std::vector<quint32> mask{kSet};
    const Picture picture = pictureFromBitmaps({1, 1, color, mask});
    QCOMPARE(picture.image.size(), QSize(1, 1));
    QCOMPARE(pixel(picture.image, 0, 0), QRgb(0xff00ff00u));
}

void tst_WindowsCursorSupport::outlineLeavesOpaquePixelsAlone()
{
    // 2x1 monochrome: a white opaque pixel next to an inverting one.
    const std::vector<quint32> mask{
        kClear, kSet, // AND
        kSet, kSet,   // XOR
    };
    const Picture picture = pictureFromBitmaps({2, 1, {}, mask});
    QCOMPARE(picture.image.size(), QSize(2 + 2 * kOutlineBorder, 1 + 2 * kOutlineBorder));
    QCOMPARE(pixel(picture.image, kOutlineBorder, kOutlineBorder), QRgb(0xffffffffu));     // still the opaque white
    QCOMPARE(pixel(picture.image, kOutlineBorder + 1, kOutlineBorder), QRgb(0xff000000u)); // the inverting fill
    // A cursor without inverting pixels gets no border at all.
    const std::vector<quint32> plain{kClear, kClear, kSet, kSet};
    const Picture opaque = pictureFromBitmaps({2, 1, {}, plain});
    QCOMPARE(opaque.image.size(), QSize(2, 1));
    QCOMPARE(opaque.hotspotShift, QPoint(0, 0));
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

void tst_WindowsCursorSupport::injectedEventsAreNotRecorded()
{
    constexpr quint32 kLeftDown = 0x0201u;
    constexpr quint32 kInjected = 0x1u;        // LLMHF_INJECTED
    constexpr quint32 kLowerIlInjected = 0x2u; // LLMHF_LOWER_IL_INJECTED
    const auto real = recordedButtonChange(kLeftDown, 0u, 0u);
    QVERIFY(real.has_value());
    QCOMPARE(real->button, Qt::LeftButton);
    QVERIFY(real->down);
    QVERIFY(!recordedButtonChange(kLeftDown, 0u, kInjected));
    QVERIFY(!recordedButtonChange(kLeftDown, 0u, kLowerIlInjected));
    QVERIFY(!recordedButtonChange(kLeftDown, 0u, kInjected | kLowerIlInjected));
    // Other flag bits do not matter; other messages still are not buttons.
    QVERIFY(recordedButtonChange(kLeftDown, 0u, 0x10u).has_value());
    QVERIFY(!recordedButtonChange(0x0200u, 0u, 0u)); // WM_MOUSEMOVE
}

namespace {

struct Delivered {
    Qt::MouseButton button;
    bool down;
    qint64 ageMs;
};

} // namespace

void tst_WindowsCursorSupport::pumpDeliversInOrderWithAges()
{
    ButtonEventPump<8> pump;
    pump.push({Qt::LeftButton, true}, 1000u);
    pump.push({Qt::LeftButton, false}, 1040u);
    pump.push({Qt::RightButton, true}, 1100u);
    std::vector<Delivered> got;
    const quint32 dropped = pump.drain(1100u, [&got](const ButtonChange& change, qint64 ageMs) {
        got.push_back({change.button, change.down, ageMs});
    });
    QCOMPARE(dropped, 0u);
    QCOMPARE(int(got.size()), 3);
    QCOMPARE(got[0].button, Qt::LeftButton);
    QVERIFY(got[0].down);
    QCOMPARE(got[0].ageMs, qint64(100));
    QVERIFY(!got[1].down);
    QCOMPARE(got[1].ageMs, qint64(60));
    QCOMPARE(got[2].button, Qt::RightButton);
    QCOMPARE(got[2].ageMs, qint64(0));
    // Nothing is delivered twice.
    int again = 0;
    pump.drain(1200u, [&again](const ButtonChange&, qint64) { ++again; });
    QCOMPARE(again, 0);
}

void tst_WindowsCursorSupport::pumpIgnoresADrainFromInsideAHandler()
{
    // The recorder's press handler polls, which drains: the queued release
    // must not arrive inside the press handler, before the press is recorded.
    ButtonEventPump<8> pump;
    pump.push({Qt::LeftButton, true}, 10u);
    pump.push({Qt::LeftButton, false}, 20u);
    QStringList order;
    pump.drain(30u, [&](const ButtonChange& change, qint64) {
        order << (change.down ? QStringLiteral("press begins") : QStringLiteral("release begins"));
        int nested = 0;
        const quint32 nestedDropped = pump.drain(30u, [&nested](const ButtonChange&, qint64) { ++nested; });
        QCOMPARE(nested, 0);
        QCOMPARE(nestedDropped, 0u);
        order << (change.down ? QStringLiteral("press ends") : QStringLiteral("release ends"));
    });
    QCOMPARE(order, QStringList({QStringLiteral("press begins"), QStringLiteral("press ends"),
                                 QStringLiteral("release begins"), QStringLiteral("release ends")}));
    // The guard is released afterwards.
    pump.push({Qt::RightButton, true}, 40u);
    int later = 0;
    pump.drain(40u, [&later](const ButtonChange&, qint64) { ++later; });
    QCOMPARE(later, 1);
}

void tst_WindowsCursorSupport::pumpCountsWhatDidNotFit()
{
    ButtonEventPump<2> pump;
    for (quint32 i = 0; i < 5; ++i) pump.push({Qt::LeftButton, i % 2 == 0}, i);
    int delivered = 0;
    QCOMPARE(pump.drain(10u, [&delivered](const ButtonChange&, qint64) { ++delivered; }), 3u);
    QCOMPARE(delivered, 2); // the first two; later ones were refused, not overwritten
    // The count is reported once, and every later loss again.
    QCOMPARE(pump.drain(10u, [](const ButtonChange&, qint64) {}), 0u);
    for (quint32 i = 0; i < 3; ++i) pump.push({Qt::LeftButton, true}, i);
    QCOMPARE(pump.drain(10u, [](const ButtonChange&, qint64) {}), 1u);
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
