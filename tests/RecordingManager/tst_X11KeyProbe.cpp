#include <QtTest>

#include <QGuiApplication>
#include <QSignalSpy>

#include "recording/KeyProbe.h"

#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

using namespace SnapTray;

// PR #200 review: the X11 probe types what the keyboard state says (Caps
// Lock, AltGr, the active layout), not just Shift on the first layout.
// Driven through XTest on its own Xvfb display.
class TestX11KeyProbe : public QObject
{
    Q_OBJECT

private slots:
    void capsLockTypesCapitals();

private:
    static void tap(Display* display, KeySym keysym);
};

void TestX11KeyProbe::tap(Display* display, KeySym keysym)
{
    const KeyCode code = XKeysymToKeycode(display, keysym);
    XTestFakeKeyEvent(display, code, True, CurrentTime);
    XTestFakeKeyEvent(display, code, False, CurrentTime);
    XSync(display, False);
}

void TestX11KeyProbe::capsLockTypesCapitals()
{
    if (qEnvironmentVariable("SNAPTRAY_TEST_ISOLATED_X11") != QLatin1String("1"))
        QSKIP("Run through CTest's isolated Xvfb wrapper.");
    auto probe = createKeyProbe();
    QVERIFY(probe);
    Display* display = XOpenDisplay(nullptr);
    QVERIFY(display);
    const auto closeDisplay = qScopeGuard([display] {
        XkbLockModifiers(display, XkbUseCoreKbd, LockMask, 0);
        XSync(display, False);
        XCloseDisplay(display);
    });
    QSignalSpy pressed(probe.get(), &KeyProbe::keyPressed);

    tap(display, XK_a);
    QTRY_COMPARE(pressed.count(), 1);
    QCOMPARE(pressed.last().at(0).toInt(), int(Qt::Key_A));
    QCOMPARE(pressed.last().at(2).toString(), QStringLiteral("a"));

    XkbLockModifiers(display, XkbUseCoreKbd, LockMask, LockMask);
    XSync(display, False);
    tap(display, XK_a);
    QTRY_COMPARE(pressed.count(), 2);
    QCOMPARE(pressed.last().at(0).toInt(), int(Qt::Key_A));
    QCOMPARE(pressed.last().at(2).toString(), QStringLiteral("A")); // was "a"

    XkbLockModifiers(display, XkbUseCoreKbd, LockMask, 0);
    XSync(display, False);
    tap(display, XK_1);
    QTRY_COMPARE(pressed.count(), 3);
    QCOMPARE(pressed.last().at(2).toString(), QStringLiteral("1"));
}

QTEST_MAIN(TestX11KeyProbe)
#include "tst_X11KeyProbe.moc"
