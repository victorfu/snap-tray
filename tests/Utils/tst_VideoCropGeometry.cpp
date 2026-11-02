#include <QtTest/QtTest>

#include "utils/VideoCropGeometry.h"
#include "recording/PreviewEditHistory.h"

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
    void fitAspect_data();
    void fitAspect();
    void fitAspectIsStableForLegalRects();
    void originalAspectOnNativeDisplaySizes();
    void editHistoryUndoRedo();
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

void tst_VideoCropGeometry::fitAspect_data()
{
    QTest::addColumn<QRect>("bounds");
    QTest::addColumn<QSize>("frame");
    QTest::addColumn<QSize>("aspect");
    QTest::addColumn<QRect>("expected");

    const QSize fhd(1920, 1080);
    // Whole 16:9 frame at 16:9 is "no crop".
    QTest::newRow("full-frame-same-aspect") << QRect(0, 0, 1920, 1080) << fhd << QSize(16, 9) << QRect();
    // Square from the full frame: as tall as the frame, centred.
    QTest::newRow("square-in-landscape") << QRect(0, 0, 1920, 1080) << fhd << QSize(1, 1)
                                         << QRect(420, 0, 1080, 1080);
    // 9:16 in landscape: unit 18x32, 33 units = 594x1056, centred.
    QTest::newRow("portrait-in-landscape") << QRect(0, 0, 1920, 1080) << fhd << QSize(9, 16)
                                           << QRect(662, 12, 594, 1056);
    // Bounds narrower than the aspect: limited by width, centred vertically.
    QTest::newRow("width-limited") << QRect(100, 100, 400, 400) << fhd << QSize(16, 9)
                                   << QRect(108, 192, 384, 216);
    // Tiny bounds grow to the minimum: 4:3 in units of 8x6, 11 units (88x66) reach 64 on the short side.
    QTest::newRow("grows-to-minimum") << QRect(500, 500, 10, 10) << fhd << QSize(4, 3)
                                      << QRect(460, 472, 88, 66);
    // Near an edge: stays inside the frame.
    QTest::newRow("clamped-to-frame") << QRect(1900, 1060, 20, 20) << fhd << QSize(1, 1)
                                      << QRect(1856, 1016, 64, 64);
    // Odd frame: the even-floored frame bounds everything.
    QTest::newRow("odd-frame") << QRect(0, 0, 1921, 1081) << QSize(1921, 1081) << QSize(1, 1)
                               << QRect(420, 0, 1080, 1080);
    // Free aspect falls back to normalizeCropRect().
    QTest::newRow("free") << QRect(11, 11, 101, 101) << fhd << QSize(0, 0) << QRect(10, 10, 102, 102);
    QTest::newRow("empty-frame") << QRect(0, 0, 10, 10) << QSize() << QSize(1, 1) << QRect();
}

void tst_VideoCropGeometry::fitAspect()
{
    QFETCH(QRect, bounds);
    QFETCH(QSize, frame);
    QFETCH(QSize, aspect);
    QFETCH(QRect, expected);
    const QRect result = SnapTray::VideoCropGeometry::fitAspect(bounds, frame, aspect.width(), aspect.height());
    QCOMPARE(result, expected);
    if (!result.isEmpty()) {
        QCOMPARE(result.x() % 2, 0);
        QCOMPARE(result.y() % 2, 0);
        QCOMPARE(result.width() % 2, 0);
        QCOMPARE(result.height() % 2, 0);
        if (aspect.width() > 0) {
            // Exact ratio, not approximately.
            QCOMPARE(qint64(result.width()) * aspect.height(), qint64(result.height()) * aspect.width());
        }
    }
}

void tst_VideoCropGeometry::fitAspectIsStableForLegalRects()
{
    // Typed coordinates must not drift: a legal rect maps to itself.
    const QSize frame(2560, 1440);
    for (const QSize aspect : {QSize(16, 9), QSize(4, 3), QSize(1, 1), QSize(9, 16), QSize(0, 0)}) {
        for (const QRect seed : {QRect(100, 200, 640, 480), QRect(0, 0, 900, 900), QRect(1000, 300, 300, 700)}) {
            const QRect legal = SnapTray::VideoCropGeometry::fitAspect(seed, frame, aspect.width(), aspect.height());
            QVERIFY(!legal.isEmpty());
            QCOMPARE(SnapTray::VideoCropGeometry::fitAspect(legal, frame, aspect.width(), aspect.height()), legal);
        }
    }
}

void tst_VideoCropGeometry::originalAspectOnNativeDisplaySizes()
{
    // PR #167 review: 3456x2234 reduces to 1728:1117, so exact even
    // multiples step by the whole frame and every Original crop was cleared.
    // Large ratios round the paired side to even pixels instead.
    using SnapTray::VideoCropGeometry::fitAspect;
    for (const QSize frame : {QSize(3456, 2234), QSize(2880, 1864), QSize(3024, 1964), QSize(2560, 1664)}) {
        const int aw = frame.width();
        const int ah = frame.height();
        const auto close = [&](const QRect& r) {
            // Within one pixel of the exact ratio on the paired side.
            return qAbs(qint64(r.width()) * ah - qint64(r.height()) * aw) <= aw;
        };
        const QRect draft = fitAspect(QRect(1000, 600, 800, 600), frame, aw, ah);
        QVERIFY2(!draft.isEmpty(), qPrintable(QStringLiteral("%1x%2").arg(aw).arg(ah)));
        QVERIFY(draft.width() <= 800 && draft.height() <= 600);
        QVERIFY(draft.width() >= 790 || draft.height() >= 590); // fills the draft, not a coarse step
        QVERIFY(close(draft));
        for (int v : {draft.x(), draft.y(), draft.width(), draft.height()}) QCOMPARE(v % 2, 0);
        QCOMPARE(fitAspect(draft, frame, aw, ah), draft);

        // A tiny draft grows only to the minimum, not to the whole frame.
        const QRect tiny = fitAspect(QRect(500, 500, 10, 10), frame, aw, ah);
        QVERIFY(!tiny.isEmpty());
        QVERIFY(qMin(tiny.width(), tiny.height()) >= SnapTray::VideoCropGeometry::kMinCropSide);
        QVERIFY2(tiny.width() <= frame.width() / 16,
                 qPrintable(QStringLiteral("%1x%2 in %3x%4").arg(tiny.width()).arg(tiny.height()).arg(aw).arg(ah)));
        QVERIFY(close(tiny));
        QCOMPARE(fitAspect(tiny, frame, aw, ah), tiny);

        // Typed coordinates never drift, at any size.
        for (int w = 20; w < aw; w += 97) {
            for (int h = 20; h < ah; h += 131) {
                const QRect legal = fitAspect(QRect(7, 9, w, h), frame, aw, ah);
                if (legal.isEmpty()) continue;
                QVERIFY(close(legal));
                QVERIFY(QRect(QPoint(0, 0), frame).contains(legal));
                QCOMPARE(fitAspect(legal, frame, aw, ah), legal);
            }
        }
    }
}

void tst_VideoCropGeometry::editHistoryUndoRedo()
{
    SnapTray::PreviewEditHistory history;
    QVERIFY(!history.canUndo());
    QVERIFY(!history.canRedo());
    QVERIFY(history.current().isUnedited());

    SnapTray::PreviewEditState crop;
    crop.crop = QRect(10, 10, 100, 100);
    SnapTray::PreviewEditState cropAndTrim = crop;
    cropAndTrim.trimStart = 500;
    cropAndTrim.trimEnd = 4000;

    QVERIFY(history.record(crop));
    QVERIFY(!history.record(crop));  // no-op edits are not steps
    QVERIFY(history.record(cropAndTrim));
    QCOMPARE(history.undo(), crop);
    QCOMPARE(history.undo(), SnapTray::PreviewEditState{});
    QVERIFY(!history.canUndo());
    QCOMPARE(history.undo(), SnapTray::PreviewEditState{});
    QCOMPARE(history.redo(), crop);
    QVERIFY(history.canRedo());

    // A new edit after undo drops the redo branch.
    SnapTray::PreviewEditState other;
    other.trimStart = 100;
    QVERIFY(history.record(other));
    QVERIFY(!history.canRedo());
    QCOMPARE(history.undo(), crop);

    // Bounded.
    SnapTray::PreviewEditHistory bounded;
    for (int i = 1; i <= SnapTray::PreviewEditHistory::kMaxSteps + 20; ++i) {
        SnapTray::PreviewEditState step;
        step.trimStart = i;
        bounded.record(step);
    }
    int undos = 0;
    while (bounded.canUndo()) {
        bounded.undo();
        ++undos;
    }
    QCOMPARE(undos, SnapTray::PreviewEditHistory::kMaxSteps);
}

QTEST_MAIN(tst_VideoCropGeometry)
#include "tst_VideoCropGeometry.moc"
