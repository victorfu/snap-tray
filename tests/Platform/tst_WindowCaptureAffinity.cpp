#include <QtTest/QtTest>
#include <QPainter>
#include <QWidget>

#include "platform/WindowLevel.h"
#include "platform/CaptureExclusionPolicy.h"
#include <Windows.h>

namespace {
class TranslucentOverlay : public QWidget
{
public:
    TranslucentOverlay()
        : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint |
                  Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput |
                  Qt::NoDropShadowWindowHint)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        resize(200, 120);
    }

    int paintCount = 0;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(rect(), QColor(100, 150, 200, 100));
        ++paintCount;
    }
};
}

class tst_WindowCaptureAffinity : public QObject
{
    Q_OBJECT
private slots:
    void translucentOverlay_data()
    {
        QTest::addColumn<bool>("alreadyPainted");
        QTest::newRow("before-show") << false;
        QTest::newRow("after-paint") << true;
    }

    void translucentOverlay()
    {
        if (!SnapTray::windowsCaptureAffinity(true, QOperatingSystemVersion::current())) {
            QSKIP("Capture exclusion requires Windows 10 version 2004 or later");
        }
        QFETCH(bool, alreadyPainted);
        TranslucentOverlay overlay;
        const HWND hwnd = reinterpret_cast<HWND>(overlay.winId());
        if (alreadyPainted) {
            overlay.show();
            QTRY_VERIFY(overlay.paintCount > 0);
        }
        const LONG_PTR originalStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        const int previousPaintCount = overlay.paintCount;
        QVERIFY(originalStyle & WS_EX_LAYERED);

        setWindowExcludedFromCapture(&overlay, true);
        DWORD affinity = 0;
        QVERIFY(GetWindowDisplayAffinity(hwnd, &affinity));
        QCOMPARE(affinity, DWORD(WDA_EXCLUDEFROMCAPTURE));
        QCOMPARE(GetWindowLongPtrW(hwnd, GWL_EXSTYLE), originalStyle);

        overlay.show();
        QTRY_VERIFY(overlay.paintCount > previousPaintCount);
        QVERIFY(GetWindowDisplayAffinity(hwnd, &affinity));
        QCOMPARE(affinity, DWORD(WDA_EXCLUDEFROMCAPTURE));

        // Repeated requests and a hide/show cycle must retain the exclusion.
        setWindowExcludedFromCapture(&overlay, true);
        overlay.hide();
        overlay.show();
        QVERIFY(GetWindowDisplayAffinity(hwnd, &affinity));
        QCOMPARE(affinity, DWORD(WDA_EXCLUDEFROMCAPTURE));

        const LONG_PTR styleBeforeReset = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        setWindowExcludedFromCapture(&overlay, false);
        QVERIFY(GetWindowDisplayAffinity(hwnd, &affinity));
        QCOMPARE(affinity, DWORD(WDA_NONE));
        QCOMPARE(GetWindowLongPtrW(hwnd, GWL_EXSTYLE), styleBeforeReset);
    }
};

QTEST_MAIN(tst_WindowCaptureAffinity)
#include "tst_WindowCaptureAffinity.moc"
