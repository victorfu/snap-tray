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

QTEST_GUILESS_MAIN(tst_WindowFrameMapping)
#include "tst_WindowFrameMapping.moc"
