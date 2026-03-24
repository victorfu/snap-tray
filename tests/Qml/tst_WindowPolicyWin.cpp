#include <QtTest/QtTest>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <QQuickView>

#include "qml/QmlOverlayManager.h"

class tst_WindowPolicyWin : public QObject
{
    Q_OBJECT

private slots:
    void testStandardWindowPolicyHidesTitleBarIcon();
    void testOverlayPolicyKeepsFramelessToolWindowAfterTransientParentChange();
};

void tst_WindowPolicyWin::testStandardWindowPolicyHidesTitleBarIcon()
{
#ifndef Q_OS_WIN
    QSKIP("Windows-only window policy test.");
#else
    auto* view = SnapTray::QmlOverlayManager::instance().createUtilityWindow();
    QVERIFY(view != nullptr);

    view->setTitle(QStringLiteral("Window policy"));
    view->resize(320, 180);
    view->show();
    QTest::qWait(50);

    HWND hwnd = reinterpret_cast<HWND>(view->winId());
    QVERIFY(hwnd != nullptr);

    const LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    QVERIFY((exStyle & WS_EX_DLGMODALFRAME) != 0);
    QCOMPARE(static_cast<quintptr>(SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0)),
             quintptr(0));
    QCOMPARE(static_cast<quintptr>(SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0)),
             quintptr(0));

    view->close();
    delete view;
#endif
}

void tst_WindowPolicyWin::testOverlayPolicyKeepsFramelessToolWindowAfterTransientParentChange()
{
#ifndef Q_OS_WIN
    QSKIP("Windows-only window policy test.");
#else
    auto* hostView = SnapTray::QmlOverlayManager::instance().createUtilityWindow();
    auto* overlayView = SnapTray::QmlOverlayManager::instance().createScreenOverlay();
    QVERIFY(hostView != nullptr);
    QVERIFY(overlayView != nullptr);

    hostView->resize(240, 160);
    overlayView->resize(120, 80);

    hostView->show();
    overlayView->show();
    QTest::qWait(50);

    overlayView->setTransientParent(hostView);
    SnapTray::QmlOverlayManager::applyShownOverlayWindowPolicy(overlayView);

    HWND hwnd = reinterpret_cast<HWND>(overlayView->winId());
    QVERIFY(hwnd != nullptr);

    const LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    const LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);

    QVERIFY((style & WS_CAPTION) == 0);
    QVERIFY((style & WS_THICKFRAME) == 0);
    QVERIFY((style & WS_SYSMENU) == 0);
    QVERIFY((exStyle & WS_EX_TOOLWINDOW) != 0);
    QVERIFY((exStyle & WS_EX_APPWINDOW) == 0);

    overlayView->close();
    hostView->close();
    delete overlayView;
    delete hostView;
#endif
}

QTEST_MAIN(tst_WindowPolicyWin)
#include "tst_WindowPolicyWin.moc"
