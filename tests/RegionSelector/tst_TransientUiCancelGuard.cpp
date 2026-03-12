#include <QtTest/QtTest>

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QSignalSpy>
#include <QtQml/qqmlextensionplugin.h>

#include "RegionSelector.h"
#include "colorwidgets/ColorPickerDialogCompat.h"
#include "detection/OCRTypes.h"
#include "qml/OCRResultViewModel.h"
#include "qml/QmlEmojiPickerPopup.h"
#include "qml/QRCodeResultViewModel.h"
#include "qml/ShareResultViewModel.h"
#include "share/ShareUploadClient.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

class tst_RegionSelectorTransientUiCancelGuard : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testAppEscapeIgnoredWhenDropdownOpen();
    void testApplicationDeactivateIgnoredWhenBlockingDialogOpen();
    void testWidgetEscapeIgnoredWhenBlockingUiOpen();
    void testEmojiPickerDoesNotBlockAppEscape();
    void testEmojiPickerDoesNotBlockWidgetEscape();
    void testMoreColorsBlocksAppEscape();
    void testShareResultCloseKeepsCaptureSession();
    void testOCRResultCloseKeepsCaptureSession();
    void testQRCodeResultCloseKeepsCaptureSession();
    void testAppEscapeCancelsWithoutBlockingUi();
};

void tst_RegionSelectorTransientUiCancelGuard::initTestCase()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("No screens available for RegionSelector transient UI tests in this environment.");
    }
}

void tst_RegionSelectorTransientUiCancelGuard::testAppEscapeIgnoredWhenDropdownOpen()
{
    RegionSelector selector;
    selector.setAttribute(Qt::WA_DeleteOnClose, false);
    selector.m_dropdownOpen = true;

    QSignalSpy cancelledSpy(&selector, &RegionSelector::selectionCancelled);
    QKeyEvent event(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);

    const bool handled = selector.eventFilter(qApp, &event);

    QVERIFY(!handled);
    QCOMPARE(cancelledSpy.count(), 0);
}

void tst_RegionSelectorTransientUiCancelGuard::testApplicationDeactivateIgnoredWhenBlockingDialogOpen()
{
    RegionSelector selector;
    selector.setAttribute(Qt::WA_DeleteOnClose, false);
    selector.m_activationCount = 1;
    selector.m_openBlockingDialogCount = 1;

    QSignalSpy cancelledSpy(&selector, &RegionSelector::selectionCancelled);
    QEvent event(QEvent::ApplicationDeactivate);

    const bool handled = selector.eventFilter(qApp, &event);

    QVERIFY(!handled);
    QCOMPARE(cancelledSpy.count(), 0);
}

void tst_RegionSelectorTransientUiCancelGuard::testWidgetEscapeIgnoredWhenBlockingUiOpen()
{
    RegionSelector selector;
    selector.setAttribute(Qt::WA_DeleteOnClose, false);
    selector.m_openBlockingDialogCount = 1;

    QSignalSpy cancelledSpy(&selector, &RegionSelector::selectionCancelled);
    QKeyEvent event(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);

    selector.keyPressEvent(&event);

    QCOMPARE(cancelledSpy.count(), 0);
    QVERIFY(!event.isAccepted());
}

void tst_RegionSelectorTransientUiCancelGuard::testEmojiPickerDoesNotBlockAppEscape()
{
    RegionSelector selector;
    selector.setAttribute(Qt::WA_DeleteOnClose, false);

    QSignalSpy cancelledSpy(&selector, &RegionSelector::selectionCancelled);

    selector.showEmojiPickerPopup();
    QCoreApplication::processEvents();

    QVERIFY(selector.m_emojiPickerPopup);
    QVERIFY(selector.m_emojiPickerPopup->isVisible());
    QVERIFY(!selector.hasBlockingTransientUiOpen());

    QKeyEvent event(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    const bool handled = selector.eventFilter(qApp, &event);

    QVERIFY(handled);
    QCOMPARE(cancelledSpy.count(), 1);
}

void tst_RegionSelectorTransientUiCancelGuard::testEmojiPickerDoesNotBlockWidgetEscape()
{
    RegionSelector selector;
    selector.setAttribute(Qt::WA_DeleteOnClose, false);

    QSignalSpy cancelledSpy(&selector, &RegionSelector::selectionCancelled);

    selector.showEmojiPickerPopup();
    QCoreApplication::processEvents();

    QVERIFY(selector.m_emojiPickerPopup);
    QVERIFY(selector.m_emojiPickerPopup->isVisible());
    QVERIFY(!selector.hasBlockingTransientUiOpen());

    QKeyEvent event(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    selector.keyPressEvent(&event);

    QCOMPARE(cancelledSpy.count(), 1);
}

void tst_RegionSelectorTransientUiCancelGuard::testMoreColorsBlocksAppEscape()
{
    RegionSelector selector;
    selector.setAttribute(Qt::WA_DeleteOnClose, false);

    QSignalSpy cancelledSpy(&selector, &RegionSelector::selectionCancelled);

    selector.onMoreColorsRequested();
    QCoreApplication::processEvents();

    QVERIFY(selector.m_colorPickerDialog);
    QVERIFY(selector.m_colorPickerDialog->isVisible());
    QVERIFY(selector.hasBlockingTransientUiOpen());

    QKeyEvent event(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    const bool handled = selector.eventFilter(qApp, &event);

    QVERIFY(!handled);
    QCOMPARE(cancelledSpy.count(), 0);

    selector.m_colorPickerDialog->close();
    QCoreApplication::processEvents();
}

void tst_RegionSelectorTransientUiCancelGuard::testShareResultCloseKeepsCaptureSession()
{
    RegionSelector selector;
    selector.setAttribute(Qt::WA_DeleteOnClose, false);

    QSignalSpy cancelledSpy(&selector, &RegionSelector::selectionCancelled);

    QVERIFY(selector.m_shareClient);
    const bool invoked = QMetaObject::invokeMethod(
        selector.m_shareClient,
        "uploadSucceeded",
        Qt::DirectConnection,
        Q_ARG(QString, QStringLiteral("https://example.com/share/abc")),
        Q_ARG(QDateTime, QDateTime::currentDateTimeUtc().addDays(1)),
        Q_ARG(bool, true));
    QVERIFY(invoked);
    QCoreApplication::processEvents();

    auto* vm = selector.findChild<ShareResultViewModel*>();
    QVERIFY(vm);
    QVERIFY(selector.m_openBlockingDialogCount > 0);

    vm->close();
    QCoreApplication::processEvents();

    QCOMPARE(cancelledSpy.count(), 0);
    QCOMPARE(selector.m_openBlockingDialogCount, 0);
}

void tst_RegionSelectorTransientUiCancelGuard::testOCRResultCloseKeepsCaptureSession()
{
    RegionSelector selector;
    selector.setAttribute(Qt::WA_DeleteOnClose, false);

    OCRResult result;
    result.success = true;
    result.text = QStringLiteral("Detected text");

    QSignalSpy cancelledSpy(&selector, &RegionSelector::selectionCancelled);

    selector.showOCRResultDialog(result);
    QCoreApplication::processEvents();

    auto* vm = selector.findChild<OCRResultViewModel*>();
    QVERIFY(vm);
    QVERIFY(selector.m_openBlockingDialogCount > 0);

    vm->close();
    QCoreApplication::processEvents();

    QCOMPARE(cancelledSpy.count(), 0);
    QCOMPARE(selector.m_openBlockingDialogCount, 0);
}

void tst_RegionSelectorTransientUiCancelGuard::testQRCodeResultCloseKeepsCaptureSession()
{
    RegionSelector selector;
    selector.setAttribute(Qt::WA_DeleteOnClose, false);

    QSignalSpy cancelledSpy(&selector, &RegionSelector::selectionCancelled);

    selector.onQRCodeComplete(true,
                              QStringLiteral("https://example.com"),
                              QStringLiteral("QR_CODE"),
                              QString(),
                              QPixmap());
    QCoreApplication::processEvents();

    auto* vm = selector.findChild<QRCodeResultViewModel*>();
    QVERIFY(vm);
    QVERIFY(selector.m_openBlockingDialogCount > 0);

    vm->close();
    QCoreApplication::processEvents();

    QCOMPARE(cancelledSpy.count(), 0);
    QCOMPARE(selector.m_openBlockingDialogCount, 0);
}

void tst_RegionSelectorTransientUiCancelGuard::testAppEscapeCancelsWithoutBlockingUi()
{
    RegionSelector selector;
    selector.setAttribute(Qt::WA_DeleteOnClose, false);

    QSignalSpy cancelledSpy(&selector, &RegionSelector::selectionCancelled);
    QKeyEvent event(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);

    const bool handled = selector.eventFilter(qApp, &event);

    QVERIFY(handled);
    QCOMPARE(cancelledSpy.count(), 1);
}

QTEST_MAIN(tst_RegionSelectorTransientUiCancelGuard)
#include "tst_TransientUiCancelGuard.moc"
