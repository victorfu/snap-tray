#include <QtTest/QtTest>
#include <QPainter>
#include <QQuickItem>
#include <QQuickView>
#include <QWidget>

#include "platform/WindowLevel.h"
#include "platform/CaptureExclusionPolicy.h"
#include "platform/QtQuickBackendPolicy.h"
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
    void initTestCase()
    {
        // Same scene graph as the app: software on Windows 10, where a
        // transparent Qt Quick window is then a layered window.
        SnapTray::applyQtQuickGraphicsBackendPolicy(SnapTray::currentQtQuickGraphicsBackendPolicy());
    }

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
        // Setting affinity can succeed without invalidating the surface, and
        // show() is a no-op for the already-painted row. Explicitly request a
        // repaint so both paths verify that exclusion survives painting.
        overlay.update();
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

    // Recording overlays (control bar, area boundary) are transparent Qt Quick
    // windows. Windows 10 rejects their layered surface with error 8 too.
    void translucentQuickOverlay_data()
    {
        QTest::addColumn<bool>("transparentForInput");
        QTest::newRow("interactive") << false;
        QTest::newRow("click-through") << true;
    }

    void translucentQuickOverlay()
    {
        if (!SnapTray::windowsCaptureAffinity(true, QOperatingSystemVersion::current())) {
            QSKIP("Capture exclusion requires Windows 10 version 2004 or later");
        }
        QFETCH(bool, transparentForInput);
        QQuickView view;
        view.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        view.setFlag(Qt::WindowTransparentForInput, transparentForInput);
        view.setColor(Qt::transparent);
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.loadFromModule("QtQuick", "Rectangle");
        QVERIFY(view.rootObject());
        view.rootObject()->setProperty("color", QColor(100, 150, 200, 100));
        view.resize(200, 120);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const HWND hwnd = reinterpret_cast<HWND>(view.winId());
        const LONG_PTR originalStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);

        setWindowExcludedFromCapture(&view, true);
        DWORD affinity = 0;
        QVERIFY(GetWindowDisplayAffinity(hwnd, &affinity));
        QCOMPARE(affinity, DWORD(WDA_EXCLUDEFROMCAPTURE));
        QCOMPARE(GetWindowLongPtrW(hwnd, GWL_EXSTYLE), originalStyle);

        // The window keeps rendering and stays excluded.
        QSignalSpy swapped(&view, &QQuickWindow::frameSwapped);
        view.update();
        QTRY_VERIFY(swapped.count() > 0);
        QVERIFY(IsWindowVisible(hwnd));
        QVERIFY(GetWindowDisplayAffinity(hwnd, &affinity));
        QCOMPARE(affinity, DWORD(WDA_EXCLUDEFROMCAPTURE));

        setWindowExcludedFromCapture(&view, false);
        QVERIFY(GetWindowDisplayAffinity(hwnd, &affinity));
        QCOMPARE(affinity, DWORD(WDA_NONE));
        QCOMPARE(GetWindowLongPtrW(hwnd, GWL_EXSTYLE), originalStyle);
    }
};

QTEST_MAIN(tst_WindowCaptureAffinity)
#include "tst_WindowCaptureAffinity.moc"
