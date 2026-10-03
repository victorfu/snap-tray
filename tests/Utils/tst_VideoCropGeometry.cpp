#include <QtTest/QtTest>

#include "utils/VideoCropGeometry.h"

using namespace SnapTray::VideoCropGeometry;

class tst_VideoCropGeometry : public QObject
{
    Q_OBJECT

private slots:
    void aspectFitRect_data();
    void aspectFitRect();
    void normalizeCropRect_data();
    void normalizeCropRect();
    void viewToVideoMapsThroughContentRect();
    void videoToViewIsInverseOfViewToVideo();
    void mappingRejectsEmptyContent();
    void viewPointToVideoMapsAndClamps();
};

void tst_VideoCropGeometry::aspectFitRect_data()
{
    QTest::addColumn<QSize>("frame");
    QTest::addColumn<QSizeF>("item");
    QTest::addColumn<QRectF>("expected");
    QTest::newRow("wide frame letterboxed") << QSize(200, 100) << QSizeF(400, 400) << QRectF(0, 100, 400, 200);
    QTest::newRow("tall frame pillarboxed") << QSize(100, 200) << QSizeF(400, 300) << QRectF(125, 0, 150, 300);
    QTest::newRow("exact fit") << QSize(320, 180) << QSizeF(320, 180) << QRectF(0, 0, 320, 180);
    QTest::newRow("empty frame") << QSize() << QSizeF(400, 300) << QRectF();
    QTest::newRow("empty item") << QSize(320, 180) << QSizeF() << QRectF();
}

void tst_VideoCropGeometry::aspectFitRect()
{
    QFETCH(QSize, frame);
    QFETCH(QSizeF, item);
    QFETCH(QRectF, expected);
    QCOMPARE(SnapTray::VideoCropGeometry::aspectFitRect(frame, item), expected);
}

void tst_VideoCropGeometry::normalizeCropRect_data()
{
    QTest::addColumn<QRect>("input");
    QTest::addColumn<QSize>("frame");
    QTest::addColumn<QRect>("expected");
    const QSize hd(1920, 1080);
    QTest::newRow("even rect unchanged") << QRect(100, 100, 640, 360) << hd << QRect(100, 100, 640, 360);
    QTest::newRow("odd origin and size") << QRect(101, 51, 641, 361) << hd << QRect(100, 50, 642, 362);
    QTest::newRow("clamped to frame") << QRect(1800, 1000, 400, 400) << hd << QRect(1800, 1000, 120, 80);
    QTest::newRow("too small grows") << QRect(10, 10, 20, 20) << hd << QRect(10, 10, 64, 64);
    QTest::newRow("too small at corner shifts in") << QRect(1900, 1060, 10, 10) << hd << QRect(1856, 1016, 64, 64);
    QTest::newRow("full frame means no crop") << QRect(0, 0, 1920, 1080) << hd << QRect();
    QTest::newRow("odd frame full means no crop") << QRect(0, 0, 1921, 1081) << QSize(1921, 1081) << QRect();
    QTest::newRow("outside frame") << QRect(3000, 3000, 10, 10) << hd << QRect();
    QTest::newRow("empty frame") << QRect(0, 0, 100, 100) << QSize() << QRect();
    // 48x40 frame: minimum side shrinks to the frame, so the crop covers it all.
    QTest::newRow("tiny frame full means no crop") << QRect(0, 0, 10, 10) << QSize(48, 40) << QRect();
}

void tst_VideoCropGeometry::normalizeCropRect()
{
    QFETCH(QRect, input);
    QFETCH(QSize, frame);
    QFETCH(QRect, expected);
    QCOMPARE(SnapTray::VideoCropGeometry::normalizeCropRect(input, frame), expected);
}

void tst_VideoCropGeometry::viewToVideoMapsThroughContentRect()
{
    // 800x400 video drawn at half scale, 100 px below the item top.
    const QRectF content(0, 100, 400, 200);
    const QSize frame(800, 400);
    QCOMPARE(viewToVideo(QRectF(100, 150, 100, 50), content, frame), QRect(200, 100, 200, 100));
}

void tst_VideoCropGeometry::videoToViewIsInverseOfViewToVideo()
{
    const QRectF content(40, 0, 320, 180);
    const QSize frame(1920, 1080);
    const QRect video(600, 300, 640, 360);
    QCOMPARE(viewToVideo(videoToView(video, content, frame), content, frame), video);
}

void tst_VideoCropGeometry::mappingRejectsEmptyContent()
{
    QCOMPARE(viewToVideo(QRectF(0, 0, 10, 10), QRectF(), QSize(100, 100)), QRect());
    QCOMPARE(videoToView(QRect(0, 0, 10, 10), QRectF(), QSize(100, 100)), QRectF());
}

void tst_VideoCropGeometry::viewPointToVideoMapsAndClamps()
{
    // 800x400 video drawn at half scale, 100 px below the item top.
    const QRectF content(0, 100, 400, 200);
    const QSize frame(800, 400);
    QCOMPARE(viewPointToVideo(QPointF(100, 150), content, frame), QPoint(200, 100));
    QCOMPARE(viewPointToVideo(QPointF(399.9, 299.9), content, frame), QPoint(799, 399)); // inside the last pixel
    QCOMPARE(viewPointToVideo(QPointF(-5, 150), content, frame), QPoint(-1, -1));         // outside the content
    QCOMPARE(viewPointToVideo(QPointF(100, 150), QRectF(), frame), QPoint(-1, -1));
}

QTEST_MAIN(tst_VideoCropGeometry)
#include "tst_VideoCropGeometry.moc"
