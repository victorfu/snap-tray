#include <QtTest>

#include <QGuiApplication>
#include <QSignalSpy>
#include <QThread>

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
    void queuedKeysKeepTheirStateAndTime();
    void internationalKeysAreRecorded();

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

// PR #200 review: keys read late (the GUI was busy) are translated with the
// state they were pressed in, and keep the time they were pressed.
void TestX11KeyProbe::queuedKeysKeepTheirStateAndTime()
{
    if (qEnvironmentVariable("SNAPTRAY_TEST_ISOLATED_X11") != QLatin1String("1"))
        QSKIP("Run through CTest's isolated Xvfb wrapper.");
    auto probe = createKeyProbe();
    QVERIFY(probe);
    Display* display = XOpenDisplay(nullptr);
    QVERIFY(display);
    const auto closeDisplay = qScopeGuard([display] { XCloseDisplay(display); });
    QSignalSpy pressed(probe.get(), &KeyProbe::keyPressed);
    // A first key read at once: it sets the probe's server-time reference.
    tap(display, XK_a);
    QTRY_COMPARE(pressed.count(), 1);
    pressed.clear();
    // Shift down, 1, Shift up: all queued before the probe reads anything.
    const KeyCode shift = XKeysymToKeycode(display, XK_Shift_L);
    XTestFakeKeyEvent(display, shift, True, CurrentTime);
    tap(display, XK_1);
    XTestFakeKeyEvent(display, shift, False, CurrentTime);
    XSync(display, False);
    constexpr int kBusyMs = 300;
    QThread::msleep(kBusyMs); // the GUI thread is busy: nothing is read
    QTRY_COMPARE(pressed.count(), 1);
    QCOMPARE(pressed.last().at(2).toString(), QStringLiteral("!")); // not "1"
    QVERIFY(pressed.last().at(1).value<Qt::KeyboardModifiers>().testFlag(Qt::ShiftModifier));
    QVERIFY2(pressed.last().at(3).toLongLong() >= kBusyMs - 100,
             qPrintable(QString::number(pressed.last().at(3).toLongLong())));
}

// PR #200 review: a key that types a character outside ASCII (a German or
// Cyrillic layout) is recorded, with that character, not dropped.
void TestX11KeyProbe::internationalKeysAreRecorded()
{
    if (qEnvironmentVariable("SNAPTRAY_TEST_ISOLATED_X11") != QLatin1String("1"))
        QSKIP("Run through CTest's isolated Xvfb wrapper.");
    Display* display = XOpenDisplay(nullptr);
    QVERIFY(display);
    // A spare key code made to type ä, then Cyrillic д.
    int minCode = 0, maxCode = 0;
    XDisplayKeycodes(display, &minCode, &maxCode);
    int perCode = 0;
    KeySym* map = XGetKeyboardMapping(display, KeyCode(minCode), maxCode - minCode + 1, &perCode);
    KeyCode spare = 0;
    for (int code = maxCode; code >= minCode && !spare; --code) {
        if (map[(code - minCode) * perCode] == NoSymbol) spare = KeyCode(code);
    }
    XFree(map);
    QVERIFY(spare);
    const auto restore = qScopeGuard([display, spare] {
        KeySym none = NoSymbol;
        XChangeKeyboardMapping(display, spare, 1, &none, 1);
        XSync(display, False);
        XCloseDisplay(display);
    });
    auto probe = createKeyProbe();
    QVERIFY(probe);
    QSignalSpy pressed(probe.get(), &KeyProbe::keyPressed);
    for (const auto& [keysym, key, text] :
         {std::tuple{KeySym(XK_adiaeresis), int(Qt::Key_Adiaeresis), QStringLiteral("\u00E4")},
          std::tuple{KeySym(XK_Cyrillic_de), int(QChar::toUpper(char32_t(0x0434))), QStringLiteral("\u0434")}}) {
        KeySym sym = keysym;
        XChangeKeyboardMapping(display, spare, 1, &sym, 1);
        XSync(display, False);
        QTest::qWait(100); // the probe reads the new keymap
        const int before = pressed.count();
        XTestFakeKeyEvent(display, spare, True, CurrentTime);
        XTestFakeKeyEvent(display, spare, False, CurrentTime);
        XSync(display, False);
        QTRY_COMPARE(pressed.count(), before + 1);
        QCOMPARE(pressed.last().at(0).toInt(), key);
        QCOMPARE(pressed.last().at(2).toString(), text);
    }
}

QTEST_MAIN(TestX11KeyProbe)
#include "tst_X11KeyProbe.moc"
