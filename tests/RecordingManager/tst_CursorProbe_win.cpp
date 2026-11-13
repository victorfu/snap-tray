#include <QtTest/QtTest>

#include "recording/CursorProbe.h"
#include "recording/WindowsCursorSupport.h"

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
// hotspot at kInvertingHotspot.
constexpr QPoint kInvertingHotspot(1, 2);

HCURSOR createInvertingCursor()
{
    const int width = GetSystemMetrics(SM_CXCURSOR);
    const int height = GetSystemMetrics(SM_CYCURSOR);
    // One-bit rows are padded to 16 bits.
    const int rowBytes = ((width + 15) / 16) * 2;
    const QByteArray bits(rowBytes * height, char(0xFF));
    const auto* data = reinterpret_cast<const BYTE*>(bits.constData());
    return CreateCursor(GetModuleHandleW(nullptr), kInvertingHotspot.x(), kInvertingHotspot.y(), width, height,
                        data, data);
}

} // namespace

class tst_CursorProbe_win : public QObject
{
    Q_OBJECT

private slots:
    void factoryMakesTheWindowsProbe();
    void pollReportsAPhysicalPositionAndASerial();
    void currentShapeAlwaysAnswers();
    void noShapeBeforeTheFirstPoll();
    void systemCursorsBecomePictures_data();
    void systemCursorsBecomePictures();
    void invertingCursorGetsAnOutlineAndKeepsItsHotspot();
    void noCursorHandleHasNoShape();
    void systemCursorPicturesAreValidPremultiplied();
    void reportTheAlphaConventionOfSystemCursors();
    void probesCanBeCreatedAndDestroyedRepeatedly();
};

void tst_CursorProbe_win::factoryMakesTheWindowsProbe()
{
    const auto probe = SnapTray::createCursorProbe();
    QVERIFY(probe);
    QCOMPARE(probe->name(), QStringLiteral("windows"));
    QVERIFY(probe->capabilities().shapes);
    // The hook needs an interactive window station. Without one the probe
    // still works, without clicks, and says why in its warning.
    if (!probe->capabilities().clicks) QSKIP("The mouse hook was refused on this desktop");
}

void tst_CursorProbe_win::pollReportsAPhysicalPositionAndASerial()
{
    const auto probe = SnapTray::createWindowsCursorProbe();
    const auto state = probe->poll();
    if (!state) QSKIP("The pointer cannot be read on this desktop");
    QVERIFY(state->physical);
    QVERIFY(state->shapeSerial != 0);
    // The same coordinates GetCursorPos reports. The pointer may be moving
    // on a developer's machine: a few tries find two reads that agree.
    bool agreed = false;
    for (int attempt = 0; attempt < 20 && !agreed; ++attempt) {
        POINT before{};
        POINT after{};
        QVERIFY(GetCursorPos(&before));
        const auto polled = probe->poll();
        QVERIFY(GetCursorPos(&after));
        QVERIFY(polled.has_value());
        agreed = before.x == after.x && before.y == after.y && polled->position == QPoint(before.x, before.y);
    }
    QVERIFY2(agreed, "poll() reports the position GetCursorPos does");
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

void tst_CursorProbe_win::noShapeBeforeTheFirstPoll()
{
    // Before poll() has looked there is no cursor to describe: unknown, not hidden.
    const auto probe = SnapTray::createWindowsCursorProbe();
    QVERIFY(!probe->currentShape().has_value());
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
    // The picture gains a border for the outline on every side.
    const int border = SnapTray::WindowsCursor::kOutlineBorder;
    QCOMPARE(shape->image.size(),
             QSize(GetSystemMetrics(SM_CXCURSOR) + 2 * border, GetSystemMetrics(SM_CYCURSOR) + 2 * border));
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
    // The hotspot moves with the picture.
    QCOMPARE(shape->hotspot, kInvertingHotspot + QPoint(border, border));
}

void tst_CursorProbe_win::noCursorHandleHasNoShape()
{
    QVERIFY(!SnapTray::windowsCursorShape(nullptr).has_value());
}

void tst_CursorProbe_win::systemCursorPicturesAreValidPremultiplied()
{
    // Whatever Windows hands out, no colour channel of a picture may exceed its alpha.
    for (const auto resource : {IDC_ARROW, IDC_HAND, IDC_IBEAM, IDC_WAIT}) {
        const auto shape = SnapTray::windowsCursorShape(LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(resource)));
        QVERIFY(shape.has_value());
        const QImage& image = shape->image;
        for (int y = 0; y < image.height(); ++y) {
            const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                const int alpha = qAlpha(row[x]);
                QVERIFY(qRed(row[x]) <= alpha && qGreen(row[x]) <= alpha && qBlue(row[x]) <= alpha);
            }
        }
    }
}

void tst_CursorProbe_win::reportTheAlphaConventionOfSystemCursors()
{
    // Not an assertion: prints what GetIconInfo's colour bitmap holds, to
    // settle whether Windows stores cursor colours straight or premultiplied
    // (#341 review). A translucent pixel with a channel above its alpha can
    // only be straight.
    for (const auto resource : {IDC_ARROW, IDC_HAND, IDC_WAIT, IDC_SIZEALL}) {
        const HICON copy = CopyIcon(static_cast<HICON>(LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(resource))));
        QVERIFY(copy);
        ICONINFO icon{};
        QVERIFY(GetIconInfo(copy, &icon));
        DestroyIcon(copy);
        if (!icon.hbmColor) {
            if (icon.hbmMask) DeleteObject(icon.hbmMask);
            qInfo() << "cursor" << quintptr(resource) << "is monochrome";
            continue;
        }
        BITMAP header{};
        QVERIFY(GetObjectW(icon.hbmColor, sizeof(header), &header));
        QList<quint32> pixels(qsizetype(header.bmWidth) * header.bmHeight);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = header.bmWidth;
        info.bmiHeader.biHeight = -header.bmHeight;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        const HDC screen = GetDC(nullptr);
        const int lines = GetDIBits(screen, icon.hbmColor, 0, UINT(header.bmHeight), pixels.data(), &info,
                                    DIB_RGB_COLORS);
        ReleaseDC(nullptr, screen);
        DeleteObject(icon.hbmColor);
        if (icon.hbmMask) DeleteObject(icon.hbmMask);
        QCOMPARE(lines, int(header.bmHeight));
        int translucent = 0;
        int aboveAlpha = 0;
        int maxExcess = 0;
        for (const quint32 value : pixels) {
            const QRgb argb = QRgb(value);
            const int alpha = qAlpha(argb);
            if (alpha == 0 || alpha == 255) continue;
            ++translucent;
            const int brightest = qMax(qRed(argb), qMax(qGreen(argb), qBlue(argb)));
            if (brightest > alpha) {
                ++aboveAlpha;
                maxExcess = qMax(maxExcess, brightest - alpha);
            }
        }
        qInfo().noquote() << QStringLiteral("ALPHA-CONVENTION cursor %1: %2x%3, %4 translucent pixels, "
                                            "%5 with a channel above alpha (largest excess %6)")
                                 .arg(quintptr(resource)).arg(header.bmWidth).arg(header.bmHeight)
                                 .arg(translucent).arg(aboveAlpha).arg(maxExcess);
    }
}

void tst_CursorProbe_win::probesCanBeCreatedAndDestroyedRepeatedly()
{
    // Each probe installs a hook on its own thread and must remove both.
    for (int i = 0; i < 20; ++i) {
        const auto probe = SnapTray::createWindowsCursorProbe();
        probe->poll();
    }
    // Two at once (a preview recording started while another is stopping).
    const auto first = SnapTray::createWindowsCursorProbe();
    const auto second = SnapTray::createWindowsCursorProbe();
    QCOMPARE(first->capabilities().clicks, second->capabilities().clicks);
}

QTEST_MAIN(tst_CursorProbe_win)
#include "tst_CursorProbe_win.moc"
