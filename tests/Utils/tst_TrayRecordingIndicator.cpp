#include <QtTest>

#include <QImage>
#include <QSet>

#include "ui/TrayRecordingIndicator.h"

using SnapTray::TrayRecordingStatus;

namespace {

QPixmap makeBase(int devicePixels, qreal dpr)
{
    QPixmap pixmap(devicePixels, devicePixels);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(QColor(0x63, 0x66, 0xF1));
    return pixmap;
}

QByteArray imageBytes(const QPixmap& pixmap)
{
    const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    return QByteArray(reinterpret_cast<const char*>(image.constBits()), image.sizeInBytes());
}

} // namespace

class tst_TrayRecordingIndicator : public QObject
{
    Q_OBJECT

private slots:
    void idleReturnsBaseUnchanged();
    void activeStatusesKeepSizeAndDevicePixelRatio_data();
    void activeStatusesKeepSizeAndDevicePixelRatio();
    void activeStatusesAreVisuallyDistinct();
    void preparingAndCountdownShareStartingBadge();
    void badgeStaysInsideBottomRightCorner();
    void formatElapsed_data();
    void formatElapsed();
};

void tst_TrayRecordingIndicator::idleReturnsBaseUnchanged()
{
    const QPixmap base = makeBase(32, 1.0);
    const QPixmap composed = SnapTray::composeTrayRecordingPixmap(base, TrayRecordingStatus::Idle);
    QCOMPARE(imageBytes(composed), imageBytes(base));
    QVERIFY(SnapTray::composeTrayRecordingPixmap(QPixmap(), TrayRecordingStatus::Recording).isNull());
}

void tst_TrayRecordingIndicator::activeStatusesKeepSizeAndDevicePixelRatio_data()
{
    QTest::addColumn<int>("devicePixels");
    QTest::addColumn<qreal>("dpr");
    QTest::newRow("windows-linux 32px") << 32 << 1.0;
    QTest::newRow("macOS 19pt @2x") << 38 << 2.0;
}

void tst_TrayRecordingIndicator::activeStatusesKeepSizeAndDevicePixelRatio()
{
    QFETCH(int, devicePixels);
    QFETCH(qreal, dpr);
    const QPixmap base = makeBase(devicePixels, dpr);
    for (auto status : {TrayRecordingStatus::Preparing, TrayRecordingStatus::Countdown,
                        TrayRecordingStatus::Recording, TrayRecordingStatus::Paused,
                        TrayRecordingStatus::Finalizing}) {
        const QPixmap composed = SnapTray::composeTrayRecordingPixmap(base, status);
        QCOMPARE(composed.size(), base.size());
        QCOMPARE(composed.devicePixelRatio(), base.devicePixelRatio());
        QVERIFY2(imageBytes(composed) != imageBytes(base), "active status must draw a badge");
    }
}

void tst_TrayRecordingIndicator::activeStatusesAreVisuallyDistinct()
{
    const QPixmap base = makeBase(32, 1.0);
    QSet<QByteArray> renders;
    for (auto status : {TrayRecordingStatus::Preparing, TrayRecordingStatus::Recording,
                        TrayRecordingStatus::Paused, TrayRecordingStatus::Finalizing}) {
        renders.insert(imageBytes(SnapTray::composeTrayRecordingPixmap(base, status)));
    }
    QCOMPARE(renders.size(), 4);
}

void tst_TrayRecordingIndicator::preparingAndCountdownShareStartingBadge()
{
    const QPixmap base = makeBase(32, 1.0);
    QCOMPARE(imageBytes(SnapTray::composeTrayRecordingPixmap(base, TrayRecordingStatus::Preparing)),
             imageBytes(SnapTray::composeTrayRecordingPixmap(base, TrayRecordingStatus::Countdown)));
}

void tst_TrayRecordingIndicator::badgeStaysInsideBottomRightCorner()
{
    const QPixmap base = makeBase(32, 1.0);
    const QImage before = base.toImage();
    const QImage after = SnapTray::composeTrayRecordingPixmap(base, TrayRecordingStatus::Recording).toImage();
    // The top-left quadrant keeps the original artwork.
    for (int y = 0; y < 12; ++y) {
        for (int x = 0; x < 12; ++x) {
            QCOMPARE(after.pixel(x, y), before.pixel(x, y));
        }
    }
    // The badge center sits in the bottom-right corner.
    QVERIFY(after.pixel(25, 25) != before.pixel(25, 25));
}

void tst_TrayRecordingIndicator::formatElapsed_data()
{
    QTest::addColumn<qint64>("elapsedMs");
    QTest::addColumn<QString>("expected");
    QTest::newRow("negative clamps") << qint64(-500) << QStringLiteral("0:00");
    QTest::newRow("zero") << qint64(0) << QStringLiteral("0:00");
    QTest::newRow("sub-second truncates") << qint64(999) << QStringLiteral("0:00");
    QTest::newRow("seconds") << qint64(9'500) << QStringLiteral("0:09");
    QTest::newRow("minutes") << qint64(83'000) << QStringLiteral("1:23");
    QTest::newRow("just under hour") << qint64(3'599'999) << QStringLiteral("59:59");
    QTest::newRow("hours") << qint64(3'723'000) << QStringLiteral("1:02:03");
}

void tst_TrayRecordingIndicator::formatElapsed()
{
    QFETCH(qint64, elapsedMs);
    QFETCH(QString, expected);
    QCOMPARE(SnapTray::formatTrayRecordingElapsed(elapsedMs), expected);
}

QTEST_MAIN(tst_TrayRecordingIndicator)
#include "tst_TrayRecordingIndicator.moc"
