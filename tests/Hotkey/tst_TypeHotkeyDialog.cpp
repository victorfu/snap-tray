#include <QtTest>
#include <QApplication>
#include <QKeyEvent>

#include "hotkey/HotkeyManager.h"
#include "widgets/TypeHotkeyDialog.h"
#include "platform/KeyboardModifiers.h"
#ifdef Q_OS_WIN
#include "platform/KeyboardModifiers_win.h"
#endif

class tst_TypeHotkeyDialog : public QObject
{
    Q_OBJECT

private slots:
    void testPhysicalModifiersExcludeLayoutFlags();
    void testWindowsPhysicalModifierBits();
    void testPrintKeyCapture_StoresPlatformSequence();
    void testWindowsPrintScreenNativeCapture_StoresPlatformSequence();
    void testNativePrintScreenFormat_IsReadable();
};

void tst_TypeHotkeyDialog::testPhysicalModifiersExcludeLayoutFlags()
{
    QCOMPARE(SnapTray::shortcutModifiers(Qt::KeypadModifier | Qt::GroupSwitchModifier), Qt::NoModifier);
    QCOMPARE(SnapTray::shortcutModifiers(Qt::ControlModifier | Qt::KeypadModifier), Qt::ControlModifier);
}

void tst_TypeHotkeyDialog::testWindowsPhysicalModifierBits()
{
#ifndef Q_OS_WIN
    QSKIP("Windows asynchronous key-state mapping");
#else
    const auto held = [](int key) -> SHORT {
        return (key == VK_RCONTROL || key == VK_LMENU || key == VK_RSHIFT || key == VK_RWIN)
            ? SHORT(0x8000) : 0;
    };
    QCOMPARE(SnapTray::detail::windowsKeyboardModifiers(held),
        Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier);
    // The low bit means previously pressed, not physically held now.
    QCOMPARE(SnapTray::detail::windowsKeyboardModifiers([](int) -> SHORT { return 1; }), Qt::NoModifier);
#endif
}

void tst_TypeHotkeyDialog::testPrintKeyCapture_StoresPlatformSequence()
{
    SnapTray::TypeHotkeyDialog dialog;

    QKeyEvent press(QEvent::KeyPress, Qt::Key_Print, Qt::NoModifier);
    QApplication::sendEvent(&dialog, &press);

    QCOMPARE(dialog.keySequence(), QStringLiteral("Print"));
}

void tst_TypeHotkeyDialog::testWindowsPrintScreenNativeCapture_StoresPlatformSequence()
{
#ifndef Q_OS_WIN
    QSKIP("Windows native Print Screen capture is only available on Windows.");
#else
    SnapTray::TypeHotkeyDialog dialog;

    dialog.captureWindowsPrintScreen(Qt::ControlModifier | Qt::ShiftModifier);

    QCOMPARE(dialog.keySequence(), QStringLiteral("Ctrl+Shift+Print"));
#endif
}

void tst_TypeHotkeyDialog::testNativePrintScreenFormat_IsReadable()
{
    QCOMPARE(SnapTray::HotkeyManager::formatKeySequence(QStringLiteral("Native:0x2C")),
             QStringLiteral("Print"));
}

QTEST_MAIN(tst_TypeHotkeyDialog)
#include "tst_TypeHotkeyDialog.moc"
