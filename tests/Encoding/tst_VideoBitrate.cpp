#include <QtTest/QtTest>

#include "encoding/VideoBitrate.h"

class tst_VideoBitrate : public QObject
{
    Q_OBJECT

private slots:
    void forQuality_data();
    void forQuality();
};

void tst_VideoBitrate::forQuality_data()
{
    QTest::addColumn<QSize>("size");
    QTest::addColumn<int>("fps");
    QTest::addColumn<int>("quality");
    QTest::addColumn<int>("expected");
    // 1920*1080*30 * (0.1 + 0.55*0.2 = 0.21) = 13,063,680
    QTest::newRow("default quality 1080p30") << QSize(1920, 1080) << 30 << 55 << 13063680;
    QTest::newRow("tiny clamps to minimum") << QSize(64, 48) << 10 << 0 << SnapTray::VideoBitrate::kMinBitrate;
    QTest::newRow("4k60 clamps to maximum") << QSize(3840, 2160) << 60 << 100 << SnapTray::VideoBitrate::kMaxBitrate;
    QTest::newRow("quality above 100 is clamped") << QSize(1280, 720) << 30 << 150 << 8294400;
}

void tst_VideoBitrate::forQuality()
{
    QFETCH(QSize, size);
    QFETCH(int, fps);
    QFETCH(int, quality);
    QFETCH(int, expected);
    QCOMPARE(SnapTray::VideoBitrate::forQuality(size, fps, quality), expected);
}

QTEST_GUILESS_MAIN(tst_VideoBitrate)
#include "tst_VideoBitrate.moc"
