#include <QtTest/QtTest>

#include "recording/WindowTimeline.h"

using SnapTray::WindowFrameMapping;

// Needed by QTest::addColumn<WindowFrameMapping>() below.
Q_DECLARE_METATYPE(SnapTray::WindowFrameMapping)

class tst_WindowFrameMapping : public QObject
{
    Q_OBJECT

private slots:
    void mapsLogicalWindowsToVideoPixels_data();
    void mapsLogicalWindowsToVideoPixels();
    void invalidMappingProducesNothing();
    void fromCaptureBuildsRegionOrigin_data();
    void fromCaptureBuildsRegionOrigin();
    void mapsNativeWindows_data();
    void mapsNativeWindows();
};

void tst_WindowFrameMapping::mapsLogicalWindowsToVideoPixels_data()
{
    QTest::addColumn<WindowFrameMapping>("mapping");
    QTest::addColumn<QRect>("logicalBounds");
    QTest::addColumn<QRect>("expected");

    // macOS Retina: no native desktop bounds, 2x, screen at the logical origin.
    const WindowFrameMapping retina{QRect(0, 0, 1440, 900), QRect(), 2.0, QRect(0, 0, 2880, 1800)};
    QTest::newRow("retina window") << retina << QRect(100, 50, 300, 200) << QRect(200, 100, 600, 400);
    QTest::newRow("retina full screen") << retina << QRect(0, 0, 1440, 900) << QRect(0, 0, 2880, 1800);

    // Windows 125 %: 1920x1080 native shown as 1536x864 logical.
    const WindowFrameMapping scaled125{QRect(0, 0, 1536, 864), QRect(0, 0, 1920, 1080), 1.25, QRect(0, 0, 1920, 1080)};
    QTest::newRow("125% window") << scaled125 << QRect(16, 8, 160, 80) << QRect(20, 10, 200, 100);
    // Odd logical coordinates map outwards: floor the origin, ceil the far edge.
    QTest::newRow("125% rounds outwards") << scaled125 << QRect(1, 1, 1, 1) << QRect(1, 1, 2, 2);

    // Windows 150 % secondary screen whose logical origin (1707, 0) is not its
    // native origin (2560, 0): origins translate, sizes scale.
    const WindowFrameMapping secondary150{QRect(1707, 0, 1024, 768), QRect(2560, 0, 1536, 1152), 1.5,
                                          QRect(2560, 0, 1536, 1152)};
    QTest::newRow("150% secondary window") << secondary150 << QRect(1807, 100, 200, 100) << QRect(150, 150, 300, 150);
    // A window that starts on the primary screen and ends on this one keeps its part on this screen.
    QTest::newRow("spanning window is clipped") << secondary150 << QRect(1600, 100, 300, 100) << QRect(0, 150, 290, 150);
    // Entirely on the other screen: nothing to store.
    QTest::newRow("window on another screen is dropped") << secondary150 << QRect(0, 0, 100, 100) << QRect();
    // Hanging off the bottom-right of the recorded screen is clipped to the frame.
    QTest::newRow("overhanging window is clipped") << secondary150 << QRect(2531, 568, 400, 400) << QRect(1236, 852, 300, 300);

    // Native branch with a region that is a strict sub-rect of the screen.
    // Window (1720,10,100,40) is (13,10,100,40) relative to the logical screen; x1.5 outwards gives
    // native (2560+19, 15)-(2560+170, 75) = (2579,15)-(2730,75). Minus the region origin (2660,50)
    // that is (-81,-35)-(70,25), clipped to the 800x600 frame: (0,0,70,25).
    WindowFrameMapping nativeSubRegion = secondary150;
    nativeSubRegion.physicalRegion = QRect(2660, 50, 800, 600);
    QTest::newRow("native sub-region window is clipped") << nativeSubRegion << QRect(1720, 10, 100, 40) << QRect(0, 0, 70, 25);

    // macOS branch (no native bounds) with a sub-region: video pixels are relative to the region origin.
    // Window (200,100,100,50) x2 = (400,200,200,100); minus the region origin (400,200) = (0,0,200,100).
    const WindowFrameMapping macSubRegion{QRect(0, 0, 1440, 900), QRect(), 2.0, QRect(400, 200, 800, 600)};
    QTest::newRow("mac sub-region window") << macSubRegion << QRect(200, 100, 100, 50) << QRect(0, 0, 200, 100);
    // Window (550,300,200,200) x2 = (1100,600,400,400); minus the origin = (700,400,400,400),
    // clipped to the 800x600 frame: (700,400,100,200).
    QTest::newRow("mac sub-region window is clipped") << macSubRegion << QRect(550, 300, 200, 200) << QRect(700, 400, 100, 200);
}

void tst_WindowFrameMapping::mapsLogicalWindowsToVideoPixels()
{
    QFETCH(WindowFrameMapping, mapping);
    QFETCH(QRect, logicalBounds);
    QFETCH(QRect, expected);
    QVERIFY(mapping.isValid());
    QCOMPARE(mapping.toVideoRect(logicalBounds), expected);
}

void tst_WindowFrameMapping::invalidMappingProducesNothing()
{
    WindowFrameMapping mapping;
    QVERIFY(!mapping.isValid());
    QCOMPARE(mapping.toVideoRect(QRect(0, 0, 100, 100)), QRect());
    mapping.logicalScreen = QRect(0, 0, 100, 100);
    mapping.physicalRegion = QRect(0, 0, 100, 100);
    mapping.devicePixelRatio = 0.0;
    QVERIFY(!mapping.isValid());
    QCOMPARE(mapping.toVideoRect(QRect(0, 0, 50, 50)), QRect());
}

void tst_WindowFrameMapping::fromCaptureBuildsRegionOrigin_data()
{
    QTest::addColumn<QRect>("logicalRegion");
    QTest::addColumn<QRect>("logicalScreen");
    QTest::addColumn<QRect>("physicalScreen");
    QTest::addColumn<qreal>("dpr");
    QTest::addColumn<QRect>("mappedRegion");
    QTest::addColumn<QRect>("expectedRegion");

    // Physical screen present: the mapped region is already in native desktop pixels.
    QTest::newRow("native secondary 150%")
        << QRect(1807, 100, 400, 300) << QRect(1707, 0, 1024, 768) << QRect(2560, 0, 1536, 1152) << 1.5
        << QRect(2660, 150, 600, 450) << QRect(2660, 150, 600, 450);
    // Physical screen absent (macOS): origin restored from the logical region, size kept.
    QTest::newRow("mac retina sub-region")
        << QRect(200, 100, 400, 300) << QRect(0, 0, 1440, 900) << QRect() << 2.0
        << QRect(0, 0, 800, 600) << QRect(400, 200, 800, 600);
    QTest::newRow("mac retina offset screen")
        << QRect(1540, 100, 400, 300) << QRect(1440, 0, 1440, 900) << QRect() << 2.0
        << QRect(0, 0, 800, 600) << QRect(200, 200, 800, 600);
    QTest::newRow("mac fractional origin covers outwards")
        << QRect(1, 1, 100, 100) << QRect(0, 0, 1440, 900) << QRect() << 1.25
        << QRect(0, 0, 126, 126) << QRect(1, 1, 126, 126);
}

void tst_WindowFrameMapping::fromCaptureBuildsRegionOrigin()
{
    QFETCH(QRect, logicalRegion);
    QFETCH(QRect, logicalScreen);
    QFETCH(QRect, physicalScreen);
    QFETCH(qreal, dpr);
    QFETCH(QRect, mappedRegion);
    QFETCH(QRect, expectedRegion);
    const WindowFrameMapping mapping =
        WindowFrameMapping::fromCapture(logicalRegion, logicalScreen, physicalScreen, dpr, mappedRegion);
    QCOMPARE(mapping.logicalScreen, logicalScreen);
    QCOMPARE(mapping.physicalScreen, physicalScreen);
    QCOMPARE(mapping.devicePixelRatio, dpr);
    QCOMPARE(mapping.physicalRegion, expectedRegion);
    QCOMPARE(mapping.physicalRegion.size(), mappedRegion.size());
}

void tst_WindowFrameMapping::mapsNativeWindows_data()
{
    QTest::addColumn<WindowFrameMapping>("mapping");
    QTest::addColumn<QRect>("nativeBounds");
    QTest::addColumn<QRect>("expected");
    const WindowFrameMapping right{QRect(1920, 0, 1920, 1080), QRect(1920, 0, 3840, 2160), 2.0,
                                   QRect(1920, 0, 3840, 2160)};
    QTest::newRow("100-to-200-percent") << right << QRect(1500, 100, 600, 400) << QRect(0, 100, 180, 400);
    const WindowFrameMapping primary{QRect(0, 0, 1920, 1080), QRect(0, 0, 1920, 1080), 1.0,
                                     QRect(0, 0, 1920, 1080)};
    QTest::newRow("200-to-100-percent") << primary << QRect(1800, 100, 600, 400) << QRect(1800, 100, 120, 400);
    const WindowFrameMapping left{QRect(-2560, -200, 1280, 720), QRect(-2560, -200, 2560, 1440), 2.0,
                                  QRect(-2560, -200, 2560, 1440)};
    QTest::newRow("negative-origin") << left << QRect(-200, -100, 600, 400) << QRect(2360, 100, 200, 400);
    QTest::newRow("outside") << right << QRect(100, 100, 600, 400) << QRect();
    QTest::newRow("invalid-bounds") << right << QRect() << QRect();
    WindowFrameMapping region = right;
    region.physicalRegion = QRect(2100, 300, 800, 600);
    QTest::newRow("partial-region") << region << QRect(2000, 200, 300, 300) << QRect(0, 0, 200, 200);
    QTest::newRow("no-native-screen")
        << WindowFrameMapping{QRect(0, 0, 100, 100), QRect(), 2.0, QRect(0, 0, 200, 200)}
        << QRect(0, 0, 100, 100) << QRect();
    QTest::newRow("invalid-mapping") << WindowFrameMapping{} << QRect(0, 0, 100, 100) << QRect();
}

void tst_WindowFrameMapping::mapsNativeWindows()
{
    QFETCH(WindowFrameMapping, mapping);
    QFETCH(QRect, nativeBounds);
    QFETCH(QRect, expected);
    const QRect actual = mapping.toVideoRectFromPhysical(nativeBounds);
    if (expected.isEmpty()) QVERIFY(actual.isEmpty());
    else QCOMPARE(actual, expected);
}

QTEST_GUILESS_MAIN(tst_WindowFrameMapping)
#include "tst_WindowFrameMapping.moc"
