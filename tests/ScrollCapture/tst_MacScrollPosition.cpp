#include "scrollcapture/MacScrollPosition.h"

#include <QtTest>

using namespace snaptray::scrollcapture;

class tst_MacScrollPosition : public QObject
{
    Q_OBJECT
private slots:
    void webViewportDoesNotProveEndpoints();
    void webScrollBarStillReportsEndpoints_data();
    void webScrollBarStillReportsEndpoints();
    void nativeDocumentGeometry_data();
    void nativeDocumentGeometry();
    void viewportGeometryWithScrollBar_data();
    void viewportGeometryWithScrollBar();
    void viewportGeometryWithOnlyThumbRatio();
};

void tst_MacScrollPosition::webViewportDoesNotProveEndpoints()
{
    // Chrome #265: both frames are the viewport even after the page scrolls.
    const QRectF viewport(0, 120, 1470, 836);
    ScrollPosition position;
    resolveMacScrollPosition(position, viewport, QStringLiteral("AXWebArea"), viewport);
    QVERIFY(!position.contentHeight);
    QVERIFY(!position.offset);
    QVERIFY(!position.viewportRatio);
    QVERIFY(!position.fraction);
    QVERIFY(!position.atTop);
    QVERIFY(!position.atBottom);
    QVERIFY(position.sources.isEmpty());
    // Unknown end must not activate the probe's early-exit guard.
    QVERIFY(!position.atBottom.value_or(false));
}

void tst_MacScrollPosition::webScrollBarStillReportsEndpoints_data()
{
    QTest::addColumn<double>("fraction");
    QTest::addColumn<bool>("top");
    QTest::addColumn<bool>("bottom");
    QTest::newRow("top") << 0.0 << true << false;
    QTest::newRow("middle") << 0.5 << false << false;
    QTest::newRow("bottom") << 1.0 << false << true;
}

void tst_MacScrollPosition::webScrollBarStillReportsEndpoints()
{
    QFETCH(double, fraction);
    QFETCH(bool, top);
    QFETCH(bool, bottom);
    const QRectF viewport(0, 120, 1470, 836);
    ScrollPosition position;
    position.fraction = fraction;
    position.sources << QStringLiteral("AXVerticalScrollBar.AXValue");
    resolveMacScrollPosition(position, viewport, QStringLiteral("AXWebArea"), viewport);
    QCOMPARE(position.atTop.value(), top);
    QCOMPARE(position.atBottom.value(), bottom);
    QVERIFY(!position.offset);
    QVERIFY(!position.contentHeight);
    QCOMPARE(position.sources.size(), 1);

    // If a real scrollbar also supplies a visible/content ratio, keep its
    // geometry rather than replacing it with the AXWebArea viewport height.
    position.viewportRatio = 0.25;
    resolveMacScrollPosition(position, viewport, QStringLiteral("AXWebArea"), viewport);
    QCOMPARE(position.contentHeight.value(), 3344.0);
    QCOMPARE(position.offset.value(), fraction * 2508.0);
    QCOMPARE(position.atTop.value(), top);
    QCOMPARE(position.atBottom.value(), bottom);
}

void tst_MacScrollPosition::nativeDocumentGeometry_data()
{
    QTest::addColumn<double>("height");
    QTest::addColumn<double>("offset");
    QTest::addColumn<bool>("top");
    QTest::addColumn<bool>("bottom");
    QTest::newRow("top") << 2000.0 << 0.0 << true << false;
    QTest::newRow("middle") << 2000.0 << 120.0 << false << false;
    QTest::newRow("bottom") << 2000.0 << 1500.0 << false << true;
    QTest::newRow("fits") << 500.0 << 0.0 << true << true;
}

void tst_MacScrollPosition::nativeDocumentGeometry()
{
    QFETCH(double, height);
    QFETCH(double, offset);
    QFETCH(bool, top);
    QFETCH(bool, bottom);
    ScrollPosition position;
    resolveMacScrollPosition(position, QRectF(0, 100, 800, 500), QStringLiteral("AXOutline"),
                             QRectF(0, 100 - offset, 800, height));
    QCOMPARE(position.contentHeight.value(), height);
    QCOMPARE(position.offset.value(), offset);
    QCOMPARE(position.atTop.value(), top);
    QCOMPARE(position.atBottom.value(), bottom);
    QCOMPARE(position.sources, QStringList{QStringLiteral("AXContents")});
}

void tst_MacScrollPosition::viewportGeometryWithScrollBar_data()
{
    QTest::addColumn<double>("fraction");
    QTest::addColumn<bool>("hasRatio");
    QTest::newRow("settings-top") << 0.0 << true;
    QTest::newRow("settings-scrolled") << 0.9532871972318339 << true;
    QTest::newRow("settings-bottom") << 1.0 << true;
    QTest::newRow("fraction-only-top") << 0.0 << false;
    QTest::newRow("fraction-only-middle") << 0.5 << false;
    QTest::newRow("fraction-only-bottom") << 1.0 << false;
}

void tst_MacScrollPosition::viewportGeometryWithScrollBar()
{
    QFETCH(double, fraction);
    QFETCH(bool, hasRatio);
    const QRectF viewport(695, 211, 232, 527);
    constexpr double ratio = 0.5113421550094518;
    ScrollPosition position;
    position.fraction = fraction;
    position.sources << QStringLiteral("AXVerticalScrollBar.AXValue");
    if (hasRatio) {
        position.viewportRatio = ratio;
        position.sources << QStringLiteral("AXValueIndicator");
    }
    const QStringList sources = position.sources;
    resolveMacScrollPosition(position, viewport, QStringLiteral("AXOutline"), viewport);
    QCOMPARE(position.atTop.value(), fraction == 0.0);
    QCOMPARE(position.atBottom.value(), fraction == 1.0);
    QCOMPARE(position.sources, sources);
    if (hasRatio) {
        QCOMPARE(position.contentHeight.value(), viewport.height() / ratio);
        QCOMPARE(position.offset.value(), fraction * (viewport.height() / ratio - viewport.height()));
    } else {
        QVERIFY(!position.contentHeight);
        QVERIFY(!position.offset);
        QVERIFY(!position.viewportRatio);
    }
}

void tst_MacScrollPosition::viewportGeometryWithOnlyThumbRatio()
{
    const QRectF viewport(695, 211, 232, 527);
    ScrollPosition position;
    position.viewportRatio = 0.5;
    position.sources << QStringLiteral("AXValueIndicator");
    resolveMacScrollPosition(position, viewport, QStringLiteral("AXOutline"), viewport);
    QCOMPARE(position.contentHeight.value(), 1054.0);
    QVERIFY(!position.offset);
    QVERIFY(!position.fraction);
    QVERIFY(!position.atTop);
    QVERIFY(!position.atBottom);
    QCOMPARE(position.sources, QStringList{QStringLiteral("AXValueIndicator")});
}

QTEST_GUILESS_MAIN(tst_MacScrollPosition)
#include "tst_MacScrollPosition.moc"
