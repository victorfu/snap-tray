#include <QtTest/QtTest>

#include "recording/CursorProbe.h"

#include <windows.h>

using SnapTray::CursorProbe;

namespace {

bool hasVisiblePixel(const QImage& image)
{
    const QImage argb = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < argb.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(argb.constScanLine(y));
        for (int x = 0; x < argb.width(); ++x) {
            if (qAlpha(row[x]) != 0) return true;
        }
    }
    return false;
}

// A monochrome cursor of the system cursor size whose every pixel inverts the
// screen (AND and XOR both set), like the classic text I-beam, with its
// hotspot at (1, 2).
HCURSOR createInvertingCursor()
{
    const int width = GetSystemMetrics(SM_CXCURSOR);
    const int height = GetSystemMetrics(SM_CYCURSOR);
    // One-bit rows are padded to 16 bits.
    const int rowBytes = ((width + 15) / 16) * 2;
    const QByteArray bits(rowBytes * height, char(0xFF));
    const auto* data = reinterpret_cast<const BYTE*>(bits.constData());
    return CreateCursor(GetModuleHandleW(nullptr), 1, 2, width, height, data, data);
}

} // namespace

class tst_CursorProbe_win : public QObject
{
    Q_OBJECT

private slots:
    void factoryMakesTheWindowsProbe();
    void pollReportsAPhysicalPositionAndASerial();
    void currentShapeAlwaysAnswers();
    void systemCursorsBecomePictures_data();
    void systemCursorsBecomePictures();
    void invertingCursorGetsAnOutlineAndKeepsItsHotspot();
    void noCursorHandleHasNoShape();
    void probesCanBeCreatedAndDestroyedRepeatedly();
};

void tst_CursorProbe_win::factoryMakesTheWindowsProbe()
{
    const auto probe = SnapTray::createCursorProbe();
    QVERIFY(probe);
    QCOMPARE(probe->name(), QStringLiteral("windows"));
    QVERIFY(probe->capabilities().shapes);
    // The hook needs an interactive window station, which a CI runner has.
    QVERIFY(probe->capabilities().clicks);
}

void tst_CursorProbe_win::pollReportsAPhysicalPositionAndASerial()
{
    const auto probe = SnapTray::createWindowsCursorProbe();
    const auto state = probe->poll();
    if (!state) QSKIP("The pointer cannot be read on this desktop");
    QVERIFY(state->physical);
    QVERIFY(state->shapeSerial != 0);
    POINT native{};
    QVERIFY(GetCursorPos(&native));
    // Nothing moves the pointer between the two reads on a test machine.
    QCOMPARE(state->position, QPoint(native.x, native.y));
    // The same cursor keeps its serial.
    const auto again = probe->poll();
    QVERIFY(again.has_value());
    QCOMPARE(again->shapeSerial, state->shapeSerial);
}

void tst_CursorProbe_win::currentShapeAlwaysAnswers()
{
    const auto probe = SnapTray::createWindowsCursorProbe();
    if (!probe->poll()) QSKIP("The pointer cannot be read on this desktop");
    // Visible or hidden (a transparent picture), never an unreadable shape
    // for the cursor the system is showing.
    const auto shape = probe->currentShape();
    QVERIFY(shape.has_value());
    QVERIFY(!shape->image.isNull());
    QCOMPARE(shape->image.format(), QImage::Format_ARGB32_Premultiplied);
}

void tst_CursorProbe_win::systemCursorsBecomePictures_data()
{
    QTest::addColumn<quintptr>("resource");
    QTest::newRow("arrow") << quintptr(IDC_ARROW);
    QTest::newRow("ibeam") << quintptr(IDC_IBEAM);
    QTest::newRow("hand") << quintptr(IDC_HAND);
    QTest::newRow("sizewe") << quintptr(IDC_SIZEWE);
    QTest::newRow("sizens") << quintptr(IDC_SIZENS);
    QTest::newRow("sizeall") << quintptr(IDC_SIZEALL);
    QTest::newRow("cross") << quintptr(IDC_CROSS);
    QTest::newRow("wait") << quintptr(IDC_WAIT);
    QTest::newRow("no") << quintptr(IDC_NO);
}

void tst_CursorProbe_win::systemCursorsBecomePictures()
{
    QFETCH(quintptr, resource);
    const HCURSOR cursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(resource));
    QVERIFY(cursor);
    const auto shape = SnapTray::windowsCursorShape(cursor);
    QVERIFY(shape.has_value());
    QVERIFY(!shape->image.isNull());
    QCOMPARE(shape->image.format(), QImage::Format_ARGB32_Premultiplied);
    QVERIFY2(hasVisiblePixel(shape->image), "a system cursor draws something");
    QVERIFY(shape->image.rect().contains(shape->hotspot));
}

void tst_CursorProbe_win::invertingCursorGetsAnOutlineAndKeepsItsHotspot()
{
    const HCURSOR cursor = createInvertingCursor();
    QVERIFY(cursor);
    const auto shape = SnapTray::windowsCursorShape(cursor);
    DestroyCursor(cursor);
    QVERIFY(shape.has_value());
    // The picture gains a one-pixel border for the outline.
    QVERIFY(shape->image.width() >= 3 && shape->image.height() >= 3);
    QVERIFY(hasVisiblePixel(shape->image));
    // Dark inside, light outline around it.
    const QImage argb = shape->image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    bool dark = false;
    bool light = false;
    for (int y = 0; y < argb.height(); ++y) {
        for (int x = 0; x < argb.width(); ++x) {
            const QRgb pixel = argb.pixel(x, y);
            dark |= pixel == QRgb(0xff000000u);
            light |= pixel == QRgb(0xffffffffu);
        }
    }
    QVERIFY(dark);
    QVERIFY(light);
    // The corner pixel is outline: the cursor itself starts one pixel in.
    QCOMPARE(argb.pixel(0, 0), QRgb(0xffffffffu));
    QVERIFY(shape->image.rect().contains(shape->hotspot));
    QVERIFY(shape->hotspot.x() >= 1 && shape->hotspot.y() >= 1);
}

void tst_CursorProbe_win::noCursorHandleHasNoShape()
{
    QVERIFY(!SnapTray::windowsCursorShape(nullptr).has_value());
}

void tst_CursorProbe_win::probesCanBeCreatedAndDestroyedRepeatedly()
{
    // Each probe installs a hook on its own thread and must remove both.
    for (int i = 0; i < 20; ++i) {
        const auto probe = SnapTray::createWindowsCursorProbe();
        QVERIFY(probe->capabilities().clicks);
        probe->poll();
    }
    // Two at once (a preview recording started while another is stopping).
    const auto first = SnapTray::createWindowsCursorProbe();
    const auto second = SnapTray::createWindowsCursorProbe();
    QVERIFY(first->capabilities().clicks);
    QVERIFY(second->capabilities().clicks);
}

QTEST_MAIN(tst_CursorProbe_win)
#include "tst_CursorProbe_win.moc"
