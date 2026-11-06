#include <QtTest>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>
#include <QWindow>
#include <QtQml/qqmlextensionplugin.h>

#include "pinwindow/PinHoverBar.h"
#include "qml/PinHoverBarViewModel.h"

#ifdef Q_OS_WIN
#include "platform/CaptureExclusionPolicy.h"
#include <Windows.h>
#endif

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

// #225: the pin hover bar's windows, timing and placement.
class tst_PinHoverBar : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void showsAfterDelayOnly();
    void followsOwnerWindowPolicy();
    void leavingOntoBarKeepsIt();
    void leavingAwayHides();
    void followsPin();
    void tinyPinNeverShows();
    void barNeverWiderThanPin();
    void compactWhenFullBarDoesNotFit();
    void reappearsWhenSuppressionLifts();
    void shortWidePinNeverShows();
    void otherKeyClosesPanel();
    void suppressedHidesImmediately();
    void panelEscClosesIt();
    void destroyWhileOpenIsClean();
    void iconButtonHoverShowsTooltip_data();
    void iconButtonHoverShowsTooltip();

private:
    QRect pinRect() const;
    std::unique_ptr<QWindow> m_owner;
    std::unique_ptr<PinHoverBarViewModel> m_vm;
    std::unique_ptr<PinHoverBar> m_bar;
    QPoint m_cursor;
};

QRect tst_PinHoverBar::pinRect() const
{
    const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
    return QRect(screen.topLeft() + QPoint(200, 200), QSize(480, 320));
}

void tst_PinHoverBar::init()
{
    m_owner = std::make_unique<QWindow>();
    m_owner->setGeometry(pinRect());
    m_owner->show();
    m_vm = std::make_unique<PinHoverBarViewModel>();
    m_bar = std::make_unique<PinHoverBar>(m_vm.get(), m_owner.get());
    m_cursor = pinRect().center();
    m_bar->setCursorPositionProvider([this] { return m_cursor; });
    m_bar->setPinGeometry(pinRect());
}

void tst_PinHoverBar::cleanup()
{
    m_bar.reset();
    m_vm.reset();
    m_owner.reset();
}

void tst_PinHoverBar::followsOwnerWindowPolicy()
{
    m_bar->showNow();
    m_vm->openPanel(PinHoverBarViewModel::ZoomPanel);
    QTRY_VERIFY(m_bar->isPanelVisible());
    const auto checkOwner = [&](QWindow* owner) {
        for (QWindow* view : {m_bar->barWindow(), m_bar->panelWindow()}) {
            QCOMPARE(view->transientParent(), owner);
            QCOMPARE(view->flags().testFlag(Qt::X11BypassWindowManagerHint),
                     owner && owner->flags().testFlag(Qt::X11BypassWindowManagerHint));
            QVERIFY(view->flags().testFlag(Qt::WindowDoesNotAcceptFocus));
        }
    };
    checkOwner(m_owner.get());
    m_bar->hideNow();
    QWindow bypassOwner;
    bypassOwner.setFlag(Qt::X11BypassWindowManagerHint);
    m_bar->setOwner(&bypassOwner);
    checkOwner(&bypassOwner);
    m_bar->setOwner(m_owner.get());
    checkOwner(m_owner.get());
    m_bar->setOwner(nullptr);
    checkOwner(nullptr);
}

void tst_PinHoverBar::showsAfterDelayOnly()
{
    QSignalSpy shown(m_bar.get(), &PinHoverBar::barShown);
    QElapsedTimer clock;
    clock.start();
    m_bar->pointerEntered();
    QVERIFY(!m_bar->isBarVisible());
    QVERIFY(shown.wait(1500));
    // Never before the delay (a pointer only crossing the pin shows nothing).
    QVERIFY2(clock.elapsed() >= PinHoverBar::kShowDelayMs - 20, qPrintable(QString::number(clock.elapsed())));
    QVERIFY(m_bar->isBarVisible());
    // Inside the pin, at the bottom, centred.
    const QRect bar = m_bar->barGeometry();
    QVERIFY(pinRect().contains(bar));
    QCOMPARE(bar.center().x(), pinRect().center().x());
    QCOMPARE(pinRect().bottom() - bar.bottom(), PinHoverBar::kBottomInset);
}

// The pin gets a leave event when the pointer moves onto the bar's window.
void tst_PinHoverBar::leavingOntoBarKeepsIt()
{
    m_bar->showNow();
    QVERIFY(m_bar->isBarVisible());
    m_cursor = m_bar->barGeometry().center();
    m_bar->pointerLeft();
    QTest::qWait(PinHoverBar::kHideDelayMs + 300);
    QVERIFY(m_bar->isBarVisible());
}

void tst_PinHoverBar::leavingAwayHides()
{
    m_bar->showNow();
    m_vm->openPanel(PinHoverBarViewModel::ZoomPanel);
    QTRY_VERIFY(m_bar->isPanelVisible());
    m_cursor = pinRect().bottomRight() + QPoint(300, 300);
    m_bar->pointerLeft();
    QTRY_VERIFY_WITH_TIMEOUT(!m_bar->isBarVisible(), 1500);
    QVERIFY(!m_bar->isPanelVisible());
    QCOMPARE(m_vm->panel(), int(PinHoverBarViewModel::NoPanel));
}

void tst_PinHoverBar::followsPin()
{
    m_bar->showNow();
    const QRect before = m_bar->barGeometry();
    m_bar->setPinGeometry(pinRect().translated(50, 30));
    QCOMPARE(m_bar->barGeometry().topLeft(), before.topLeft() + QPoint(50, 30));
}

void tst_PinHoverBar::tinyPinNeverShows()
{
    m_bar->setPinGeometry(QRect(pinRect().topLeft(), QSize(100, 80)));
    m_bar->pointerEntered();
    QTest::qWait(PinHoverBar::kShowDelayMs + 300);
    QVERIFY(!m_bar->isBarVisible());
    m_bar->showNow();
    QVERIFY(!m_bar->isBarVisible());
}

void tst_PinHoverBar::suppressedHidesImmediately()
{
    m_bar->showNow();
    m_vm->openPanel(PinHoverBarViewModel::OpacityPanel);
    QTRY_VERIFY(m_bar->isPanelVisible());
    m_bar->setSuppressed(true);
    QVERIFY(!m_bar->isBarVisible());
    QVERIFY(!m_bar->isPanelVisible());
    m_bar->pointerEntered();
    QTest::qWait(PinHoverBar::kShowDelayMs + 300);
    QVERIFY(!m_bar->isBarVisible());
    m_bar->setSuppressed(false);
    m_bar->pointerEntered();
    QTRY_VERIFY_WITH_TIMEOUT(m_bar->isBarVisible(), 1500);
}

void tst_PinHoverBar::panelEscClosesIt()
{
    m_bar->showNow();
    m_vm->openPanel(PinHoverBarViewModel::ZoomPanel);
    QTRY_VERIFY(m_bar->isPanelVisible());
    // Above the bar, inside the owner's screen.
    QVERIFY(m_bar->panelWindow()->geometry().bottom() < m_bar->barGeometry().top());
    QTest::keyClick(m_bar->panelWindow(), Qt::Key_Escape);
    QTRY_VERIFY(!m_bar->isPanelVisible());
    QCOMPARE(m_vm->panel(), int(PinHoverBarViewModel::NoPanel));
    QVERIFY(m_bar->isBarVisible());
}

void tst_PinHoverBar::destroyWhileOpenIsClean()
{
    m_bar->showNow();
    m_vm->openPanel(PinHoverBarViewModel::ZoomPanel);
    QTRY_VERIFY(m_bar->isPanelVisible());
    m_bar.reset();
    // The application-wide filter is gone: a key event after this is harmless.
    QTest::keyClick(m_owner.get(), Qt::Key_Escape);
    QVERIFY(true);
}

// The bar never sticks out of the pin, whatever the label lengths.
void tst_PinHoverBar::barNeverWiderThanPin()
{
    for (int width : {200, 260, 320, 480}) {
        m_bar->hideNow();
        const QRect pin(pinRect().topLeft(), QSize(width, 200));
        m_bar->setPinGeometry(pin);
        m_bar->showNow();
        if (m_bar->isBarVisible())
            QVERIFY2(pin.adjusted(PinHoverBar::kBottomInset, 0, -PinHoverBar::kBottomInset, 0)
                         .contains(m_bar->barGeometry()), qPrintable(QString::number(width)));
    }
    m_bar->setPinGeometry(QRect(pinRect().topLeft(), QSize(480, 200)));
    m_bar->showNow();
    QVERIFY(m_bar->isBarVisible());   // a normal pin still gets it
}

// A pin too narrow for the full bar gets the compact one, not none.
void tst_PinHoverBar::compactWhenFullBarDoesNotFit()
{
    m_vm->setStatus(PinHoverBarViewModel::Live);
    m_bar->showNow();
    QVERIFY(m_bar->isBarVisible());
    QVERIFY(!m_vm->compact());
    const int fullWidth = m_bar->barGeometry().width();
    m_bar->hideNow();
    const QRect pin(pinRect().topLeft(), QSize(fullWidth + 2 * PinHoverBar::kBottomInset - 10, 200));
    m_bar->setPinGeometry(pin);
    m_bar->showNow();
    QVERIFY(m_vm->compact());
    QVERIFY(m_bar->isBarVisible());
    QVERIFY(pin.adjusted(PinHoverBar::kBottomInset, 0, -PinHoverBar::kBottomInset, 0)
                .contains(m_bar->barGeometry()));
    // Wide again: the full bar comes back.
    m_bar->setPinGeometry(pinRect());
    QVERIFY(!m_vm->compact());
}

// Review #225-2: after a click or drag, the bar comes back while the
// pointer is still on the pin.
void tst_PinHoverBar::reappearsWhenSuppressionLifts()
{
    m_bar->showNow();
    m_bar->setSuppressed(true);
    QVERIFY(!m_bar->isBarVisible());
    m_bar->setSuppressed(false);
    QTRY_VERIFY_WITH_TIMEOUT(m_bar->isBarVisible(), 1500);
    // Not when the pointer has gone.
    m_bar->setSuppressed(true);
    m_cursor = pinRect().bottomRight() + QPoint(300, 300);
    m_bar->setSuppressed(false);
    QTest::qWait(PinHoverBar::kShowDelayMs + 300);
    QVERIFY(!m_bar->isBarVisible());
}

// Review #225-4: a wide but short pin (one line of text) gets no bar.
void tst_PinHoverBar::shortWidePinNeverShows()
{
    m_bar->setPinGeometry(QRect(pinRect().topLeft(), QSize(600, 28)));
    m_bar->showNow();
    QVERIFY(!m_bar->isBarVisible());
}

// Review #225-1: the panel is pointer-only; a key other than Esc closes it
// and reaches the pin as usual.
void tst_PinHoverBar::otherKeyClosesPanel()
{
    m_bar->showNow();
    m_vm->openPanel(PinHoverBarViewModel::ZoomPanel);
    QTRY_VERIFY(m_bar->isPanelVisible());
    QTest::keyClick(m_owner.get(), Qt::Key_1);
    QTRY_VERIFY(!m_bar->isPanelVisible());
    QCOMPARE(m_vm->panel(), int(PinHoverBarViewModel::NoPanel));
}

// PR #241 review: the icon-only buttons show their name and shortcut on hover.
void tst_PinHoverBar::iconButtonHoverShowsTooltip_data()
{
    QTest::addColumn<bool>("prepareCapture");
    QTest::newRow("lazy-tooltip") << false;
    QTest::newRow("prepared-for-live-capture") << true;
}

void tst_PinHoverBar::iconButtonHoverShowsTooltip()
{
    QFETCH(bool, prepareCapture);
    QList<WId> excludedIds;
    WId tooltipId = 0;
    if (prepareCapture) {
        excludedIds = m_bar->captureExclusionWindowIds();
        QCOMPARE(excludedIds.size(), 3);
        auto* tooltip = qobject_cast<QQuickView*>(m_bar->tooltipWindow());
        QVERIFY(tooltip);
        tooltipId = tooltip->winId();
        QVERIFY(excludedIds.contains(tooltipId));
        QVERIFY(tooltip->isVisible()); // Registered with WindowServer for SCK.
        QVERIFY(!tooltip->rootObject()->isVisible()); // No visible tooltip yet.
    }
    m_bar->showNow();
    QVERIFY(m_bar->isBarVisible());
    auto* view = qobject_cast<QQuickView*>(m_bar->barWindow());
    QVERIFY(view);
    QVERIFY(QTest::qWaitForWindowExposed(view));
    for (const QString& name : {QStringLiteral("pinHoverAnnotate"), QStringLiteral("pinHoverMore")}) {
        auto* button = view->rootObject()->findChild<QQuickItem*>(name);
        QVERIFY(button);
        const QPoint center = button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint();
        QTest::mouseMove(view, center);
        QTRY_VERIFY(m_bar->tooltipWindow() && m_bar->tooltipWindow()->isVisible());
        auto* tooltip = qobject_cast<QQuickView*>(m_bar->tooltipWindow());
        QVERIFY(tooltip);
        QTRY_VERIFY(tooltip->rootObject()->isVisible());
        QCOMPARE(m_bar->tooltipText(), button->property("tooltipText").toString());
        if (prepareCapture) {
            QCOMPARE(tooltip->winId(), tooltipId);
            QCOMPARE(m_bar->captureExclusionWindowIds(), excludedIds);
            QVERIFY(tooltip->rootObject()->isVisible());
        }
#ifdef Q_OS_WIN
        const DWORD expected = SnapTray::windowsCaptureAffinity(true, QOperatingSystemVersion::current());
        DWORD affinity = 0;
        QVERIFY(GetWindowDisplayAffinity(reinterpret_cast<HWND>(tooltip->winId()), &affinity));
        QCOMPARE(affinity, expected);
#endif
        // Leaving the button hides content without changing a prepared window's ID.
        QTest::mouseMove(view, QPoint(1, 1));
        QTRY_VERIFY(!tooltip->isVisible() || !tooltip->rootObject()->isVisible());
        if (prepareCapture) {
            QVERIFY(tooltip->isVisible());
            QCOMPARE(tooltip->winId(), tooltipId);
        }
    }
    m_bar->hideNow();
    auto* tooltip = qobject_cast<QQuickView*>(m_bar->tooltipWindow());
    QVERIFY(!tooltip->isVisible() || !tooltip->rootObject()->isVisible());
    if (prepareCapture) QCOMPARE(m_bar->captureExclusionWindowIds(), excludedIds);
}

QTEST_MAIN(tst_PinHoverBar)
#include "tst_PinHoverBar.moc"
