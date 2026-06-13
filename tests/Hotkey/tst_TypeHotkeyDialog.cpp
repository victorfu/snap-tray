#include <QtTest>
#include <QApplication>
#include <QKeyEvent>

#include "hotkey/HotkeyManager.h"
#include "widgets/TypeHotkeyDialog.h"

class tst_TypeHotkeyDialog : public QObject
{
    Q_OBJECT

private slots:
    void testPrintKeyCapture_StoresPlatformSequence();
    void testWindowsPrintScreenNativeCapture_StoresPlatformSequence();
    void testNativePrintScreenFormat_IsReadable();
};

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
