#include <QtTest>

#include "recording/RecordingArea.h"

using namespace SnapTray;

// #194: an area chosen in logical screen coordinates becomes the preview's
// crop in video pixels on any screen layout and scale.
class TestRecordingArea : public QObject
{
    Q_OBJECT

private slots:
    void videoCrop_data();
    void videoCrop();
    void videoCropNeverLosesAPixel();
    void clampToScreen();
};

void TestRecordingArea::videoCrop_data()
{
    QTest::addColumn<QRect>("area");
    QTest::addColumn<QRect>("recorded");
    QTest::addColumn<QSize>("frame");
    QTest::addColumn<QRect>("expected");
    QTest::newRow("1x") << QRect(100, 50, 640, 360) << QRect(0, 0, 1920, 1080) << QSize(1920, 1080)
                        << QRect(100, 50, 640, 360);
    QTest::newRow("2x HiDPI") << QRect(100, 50, 640, 360) << QRect(0, 0, 1728, 1117) << QSize(3456, 2234)
                              << QRect(200, 100, 1280, 720);
    // 150%: 2560x1440 pixels shown as 1707x960; odd results round to even.
    QTest::newRow("1.5x") << QRect(101, 51, 401, 301) << QRect(0, 0, 1707, 960) << QSize(2560, 1440)
                          << QRect(150, 76, 602, 452);
    QTest::newRow("second screen to the right") << QRect(2020, 150, 800, 600) << QRect(1920, 0, 2560, 1440)
                                                << QSize(2560, 1440) << QRect(100, 150, 800, 600);
    QTest::newRow("screen at negative coordinates, 2x")
        << QRect(-1340, -800, 400, 300) << QRect(-1440, -900, 1440, 900) << QSize(2880, 1800)
        << QRect(200, 200, 800, 600);
    QTest::newRow("whole screen is no crop") << QRect(0, 0, 1920, 1080) << QRect(0, 0, 1920, 1080)
                                             << QSize(1920, 1080) << QRect();
    QTest::newRow("tiny area grows to the minimum") << QRect(10, 10, 8, 8) << QRect(0, 0, 1920, 1080)
                                                    << QSize(1920, 1080) << QRect(10, 10, 64, 64);
    QTest::newRow("partly off the recording is clipped") << QRect(1800, 1000, 400, 400) << QRect(0, 0, 1920, 1080)
                                                         << QSize(1920, 1080) << QRect(1800, 1000, 120, 80);
    QTest::newRow("another screen entirely") << QRect(3000, 0, 100, 100) << QRect(0, 0, 1920, 1080)
                                             << QSize(1920, 1080) << QRect();
}

void TestRecordingArea::videoCrop()
{
    QFETCH(QRect, area);
    QFETCH(QRect, recorded);
    QFETCH(QSize, frame);
    QFETCH(QRect, expected);
    const QRect crop = RecordingArea::videoCrop(area, recorded, frame);
    QCOMPARE(crop, expected);
    if (!crop.isEmpty()) {
        QCOMPARE(crop.x() % 2, 0);
        QCOMPARE(crop.y() % 2, 0);
        QCOMPARE(crop.width() % 2, 0);
        QCOMPARE(crop.height() % 2, 0);
        QVERIFY(crop.width() >= 64 && crop.height() >= 64);
        QVERIFY(QRect(QPoint(0, 0), frame).contains(crop));
    }
}

void TestRecordingArea::videoCropNeverLosesAPixel()
{
    // At 150% the chosen area's own pixels are all inside the crop (up to
    // the even alignment of the origin).
    const QRect recorded(0, 0, 1707, 960);
    const QSize frame(2560, 1440);
    for (int x = 0; x < 40; x += 3) {
        const QRect area(300 + x, 200 + x, 333, 211);
        const QRect crop = RecordingArea::videoCrop(area, recorded, frame);
        const qreal s = 2560.0 / 1707.0;
        QVERIFY(crop.x() <= qFloor(area.x() * s));
        QVERIFY(crop.right() + 2 >= qCeil((area.x() + area.width()) * s) - 1);
    }
}

void TestRecordingArea::clampToScreen()
{
    const QRect screen(1920, 0, 1440, 900);
    // Kept on its own screen: never across to the next one.
    QCOMPARE(RecordingArea::clampToScreen(QRect(1800, 100, 400, 300), screen, 2.0), QRect(1920, 100, 280, 300));
    QCOMPARE(RecordingArea::clampToScreen(QRect(0, 0, 100, 100), screen, 2.0), QRect());
    // At least 64 video pixels: 32 logical at 2x, 64 at 1x, moved back on screen.
    QCOMPARE(RecordingArea::clampToScreen(QRect(3350, 890, 5, 5), screen, 2.0), QRect(3328, 868, 32, 32));
    QCOMPARE(RecordingArea::clampToScreen(QRect(1930, 10, 5, 5), screen, 1.0), QRect(1930, 10, 64, 64));
    // Partly above the screen: the part on it.
    QCOMPARE(RecordingArea::clampToScreen(QRect(2000, -50, 400, 300), screen, 1.0), QRect(2000, 0, 400, 250));
}

QTEST_GUILESS_MAIN(TestRecordingArea)
#include "tst_RecordingArea.moc"
