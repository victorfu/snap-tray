#include <QtTest>
#include <QEnterEvent>
#include <QGuiApplication>
#include <QMenu>
#include <QScreen>
#include <QtQml/qqmlextensionplugin.h>

#include "PinWindow.h"
#include "pinwindow/PinHoverBar.h"
#include "PlatformFeatures.h"
#include "qml/PinHoverBarViewModel.h"
#include "settings/PinWindowSettingsManager.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

// #225: the hover bar inside a real pin.
class TestPinHoverIntegration : public QObject
{
    Q_OBJECT
private slots:
    void hoverShowsBarWithValues();
    void barZoomRequestZoomsPin();
    void suppressedStates_data();
    void suppressedStates();
    void moreOpensContextMenu();
    void closingPinClosesBar();
    void rotateKeepsBarInside();
    void clickedBarComesBack();
    void liveTooltipOnlyOnDot();

private:
    static QPixmap pixmap()
    {
        QPixmap p(480, 320);
        p.fill(Qt::darkCyan);
        return p;
    }
    // Shows the pin and hovers it with the controller's cursor on the pin.
    static void hover(PinWindow& window)
    {
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QEnterEvent enter(QPointF(100, 100), QPointF(100, 100), window.mapToGlobal(QPointF(100, 100)));
        QCoreApplication::sendEvent(&window, &enter);
        QVERIFY(window.m_hoverBar);
        window.m_hoverBar->setCursorPositionProvider([&window] { return window.mapToGlobal(QPoint(100, 100)); });
    }
};

void TestPinHoverIntegration::hoverShowsBarWithValues()
{
    PinWindow window(pixmap(), QPoint(150, 150), nullptr, false, false);
    hover(window);
    QTRY_VERIFY_WITH_TIMEOUT(window.m_hoverBar->isBarVisible(), 1500);
    QCOMPARE(window.m_hoverBarViewModel->zoomPercent(), 100);
    QCOMPARE(window.m_hoverBarViewModel->opacityPercent(), 100);
    QVERIFY(window.frameGeometry().contains(window.m_hoverBar->barGeometry()));
}

void TestPinHoverIntegration::barZoomRequestZoomsPin()
{
    PinWindow window(pixmap(), QPoint(150, 150), nullptr, false, false);
    hover(window);
    QTRY_VERIFY_WITH_TIMEOUT(window.m_hoverBar->isBarVisible(), 1500);
    window.m_hoverBarViewModel->openPanel(PinHoverBarViewModel::ZoomPanel);
    QTRY_VERIFY(window.m_hoverBar->isPanelVisible());
    window.m_hoverBarViewModel->requestZoomPercent(150);
    QCOMPARE(window.zoomLevel(), 1.5);
    // Zooming from the panel keeps the bar and the panel open.  [RF 2]
    QTest::qWait(PinHoverBar::kHideDelayMs + 300);
    QVERIFY(window.m_hoverBar->isBarVisible());
    QVERIFY(window.m_hoverBar->isPanelVisible());
    QCOMPARE(window.m_hoverBarViewModel->zoomPercent(), 150);
    // The bar follows the larger pin.
    QTRY_VERIFY(window.frameGeometry().contains(window.m_hoverBar->barGeometry()));
    window.m_hoverBarViewModel->requestOpacityPercent(60);
    QCOMPARE(window.opacity(), 0.6);
}

void TestPinHoverIntegration::suppressedStates_data()
{
    QTest::addColumn<QString>("state");
    QTest::newRow("dragging") << "dragging";
    QTest::newRow("toolbar") << "toolbar";
    QTest::newRow("setting-off") << "setting-off";
    QTest::newRow("click-through") << "click-through";
}

void TestPinHoverIntegration::suppressedStates()
{
    QFETCH(QString, state);
    auto& settings = PinWindowSettingsManager::instance();
    const bool savedSetting = settings.loadShowHoverControls();
    auto restore = qScopeGuard([&] { settings.saveShowHoverControls(savedSetting); });
    if (state == "click-through" && !PlatformFeatures::instance().capabilities().supportsClickThrough)
        QSKIP("Click-through is not supported on this platform");
    PinWindow window(pixmap(), QPoint(150, 150), nullptr, false, false);
    if (state == "setting-off") settings.saveShowHoverControls(false);
    hover(window);
    QTRY_VERIFY_WITH_TIMEOUT(state == "setting-off" || window.m_hoverBar->isBarVisible(), 1500);
    if (state == "dragging") {
        QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, QPoint(200, 150));
        QTest::mouseMove(&window, QPoint(230, 170));
    } else if (state == "toolbar") {
        window.showToolbar();
    } else if (state == "click-through") {
        window.setClickThrough(true);
    }
    QVERIFY(!window.m_hoverBar->isBarVisible());
    QTest::qWait(PinHoverBar::kShowDelayMs + 300);
    QVERIFY(!window.m_hoverBar->isBarVisible());
    if (state == "dragging") QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, QPoint(230, 170));
}

void TestPinHoverIntegration::moreOpensContextMenu()
{
    PinWindow window(pixmap(), QPoint(150, 150), nullptr, false, false);
    hover(window);
    QTRY_VERIFY_WITH_TIMEOUT(window.m_hoverBar->isBarVisible(), 1500);
    window.m_hoverBarViewModel->requestMore();
    QVERIFY(window.m_contextMenu);
    QTRY_VERIFY(window.m_contextMenu->isVisible());
    window.m_contextMenu->hide();
}

void TestPinHoverIntegration::closingPinClosesBar()
{
    auto* window = new PinWindow(pixmap(), QPoint(150, 150), nullptr, false, false);
    window->setAttribute(Qt::WA_DeleteOnClose);
    hover(*window);
    QTRY_VERIFY_WITH_TIMEOUT(window->m_hoverBar->isBarVisible(), 1500);
    window->m_hoverBarViewModel->openPanel(PinHoverBarViewModel::ZoomPanel);
    QPointer<QWindow> bar = window->m_hoverBar->barWindow();
    QTRY_VERIFY(window->m_hoverBar->isPanelVisible());
    QPointer<QWindow> panel = window->m_hoverBar->panelWindow();
    window->close();
    QTRY_VERIFY(bar.isNull() || !bar->isVisible());
    QVERIFY(panel.isNull() || !panel->isVisible());
    // No event filter is left behind: a later key event is harmless.
    QWindow other;
    other.show();
    QVERIFY(QTest::qWaitForWindowExposed(&other));
    QTest::keyClick(&other, Qt::Key_Escape);
}

// Review #225-3: rotating changes the pin's size without a zoom change.
void TestPinHoverIntegration::rotateKeepsBarInside()
{
    PinWindow window(pixmap(), QPoint(150, 150), nullptr, false, false);
    hover(window);
    QTRY_VERIFY_WITH_TIMEOUT(window.m_hoverBar->isBarVisible(), 1500);
    window.rotateRight();
    QTRY_VERIFY(window.frameGeometry().size() == QSize(320, 480));
    QTRY_VERIFY(window.frameGeometry().contains(window.m_hoverBar->barGeometry()));
}

// Review #225-2: a click on the image hides the bar only while pressed.
void TestPinHoverIntegration::clickedBarComesBack()
{
    PinWindow window(pixmap(), QPoint(150, 150), nullptr, false, false);
    hover(window);
    QTRY_VERIFY_WITH_TIMEOUT(window.m_hoverBar->isBarVisible(), 1500);
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
    QTRY_VERIFY_WITH_TIMEOUT(window.m_hoverBar->isBarVisible(), 1500);
}

// Review #225-5: the Live hint belongs to the dot, not the whole image.
void TestPinHoverIntegration::liveTooltipOnlyOnDot()
{
    PinWindow window(pixmap(), QPoint(150, 150), nullptr, false, false);
    window.m_isLiveMode = true;
    QVERIFY(window.liveToolTipAt(QPoint(240, 160)).isEmpty());
    QVERIFY(!window.liveToolTipAt(QPoint(PinWindow::kLiveIndicatorMargin + 3, PinWindow::kLiveIndicatorMargin + 3)).isEmpty());
    window.m_isLiveMode = false;
    QVERIFY(window.liveToolTipAt(QPoint(PinWindow::kLiveIndicatorMargin + 3, PinWindow::kLiveIndicatorMargin + 3)).isEmpty());
    QVERIFY(window.toolTip().isEmpty());
}

QTEST_MAIN(TestPinHoverIntegration)
#include "tst_PinHoverIntegration.moc"
