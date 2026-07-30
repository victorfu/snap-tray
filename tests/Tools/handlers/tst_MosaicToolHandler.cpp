#include <QtTest/QtTest>

#include "tools/handlers/MosaicToolHandler.h"

class TestMosaicToolHandler : public QObject
{
    Q_OBJECT

private slots:
    void testToolIdAndCapabilities();
    void testCursor_UsesCenteredPixmapHotspot();
    void testCursor_DrawsSingleSquareOutline();
};

void TestMosaicToolHandler::testToolIdAndCapabilities()
{
    MosaicToolHandler handler;

    QCOMPARE(handler.toolId(), ToolId::Mosaic);
    QVERIFY(!handler.supportsColor());
    QVERIFY(handler.supportsWidth());
}

void TestMosaicToolHandler::testCursor_UsesCenteredPixmapHotspot()
{
    MosaicToolHandler handler;

    handler.setWidth(18);

    const QCursor cursor = handler.cursor();
    const QPixmap pixmap = cursor.pixmap();
    const QSizeF logicalSize = pixmap.deviceIndependentSize();

    QVERIFY(!pixmap.isNull());
    QVERIFY(pixmap.devicePixelRatio() >= 2.0);
    QCOMPARE(cursor.hotSpot(), QPoint(qRound(logicalSize.width() / 2.0), qRound(logicalSize.height() / 2.0)));
}

void TestMosaicToolHandler::testCursor_DrawsSingleSquareOutline()
{
    MosaicToolHandler handler;
    constexpr int brushWidth = 18;
    constexpr int expectedPadding = 4;
    constexpr QColor expectedColor(0x6C, 0x5C, 0xE7);

    handler.setWidth(brushWidth);

    const QPixmap pixmap = handler.cursor().pixmap();
    const qreal dpr = pixmap.devicePixelRatio();
    const QSizeF logicalSize = pixmap.deviceIndependentSize();
    const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    QVERIFY(!image.isNull());

    auto toPhysical = [dpr](qreal value) {
        return qRound(value * dpr);
    };

    const int centerX = image.width() / 2;
    const int centerY = image.height() / 2;
    const int topY = toPhysical(expectedPadding + 1.0);
    const QRgb topPixel = image.pixel(centerX, topY);

    QCOMPARE(qAlpha(topPixel), 255);
    QCOMPARE(qRed(topPixel), expectedColor.red());
    QCOMPARE(qGreen(topPixel), expectedColor.green());
    QCOMPARE(qBlue(topPixel), expectedColor.blue());
    QCOMPARE(qAlpha(image.pixel(toPhysical(logicalSize.width() / 2.0), toPhysical(expectedPadding + 4.0))), 0);
    QCOMPARE(qAlpha(image.pixel(image.width() / 2, image.height() / 2)), 0);
}

QTEST_MAIN(TestMosaicToolHandler)
#include "tst_MosaicToolHandler.moc"
