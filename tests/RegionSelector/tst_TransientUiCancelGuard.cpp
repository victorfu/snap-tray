#include <QtTest/QtTest>

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QEvent>
#include <QKeyEvent>
#include <QSignalSpy>
#include <QtQml/qqmlextensionplugin.h>

#include "RegionSelector.h"
#include "colorwidgets/ColorPickerDialogCompat.h"
#include "detection/OCRTypes.h"
#include "qml/OCRResultViewModel.h"
#include "qml/QmlDialog.h"
#include "qml/QmlEmojiPickerPopup.h"
#include "qml/QRCodeResultViewModel.h"
#include "qml/ShareResultViewModel.h"
#include "share/ShareUploadClient.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

namespace {

class HeadlessQmlDialog final : public SnapTray::QmlDialog
{
public:
    HeadlessQmlDialog(const QUrl& qmlSource,
                      QObject* viewModel,
                      const QString& contextPropertyName,
                      QObject* parent = nullptr)
        : QmlDialog(qmlSource, viewModel, contextPropertyName, parent)
    {
    }

    void showAt(const QPoint& pos = QPoint()) override
    {
        Q_UNUSED(pos);
        m_visible = true;
    }

    void close() override
    {
        if (!m_visible) {
            return;
        }

        m_visible = false;
        emit closed();
        deleteLater();
    }

private:
    bool m_visible = false;
};

class HeadlessEmojiPickerPopup final : public SnapTray::QmlEmojiPickerPopup
{
public:
    explicit HeadlessEmojiPickerPopup(QObject* parent = nullptr)
        : QmlEmojiPickerPopup(parent)
    {
    }

    void positionAt(const QRect& anchorRect) override
    {
        const QPoint topLeft = anchorRect.isValid() ? anchorRect.topLeft() : QPoint();
        m_geometry = QRect(topLeft, QSize(1, 1));
    }

    void showAt(const QRect& anchorRect) override
    {
        positionAt(anchorRect);
        m_visible = true;
    }

    void hide() override
    {
        m_visible = false;
    }

    void close() override
    {
        m_visible = false;
    }

    bool isVisible() const override
    {
        return m_visible;
    }

    QRect geometry() const override
    {
        return m_geometry;
    }

    QWindow* window() const override
    {
        return nullptr;
    }

private:
    bool m_visible = false;
    QRect m_geometry;
};

class HeadlessColorPickerDialog final
    : public snaptray::colorwidgets::ColorPickerDialogCompat
{
public:
    HeadlessColorPickerDialog()
    {
        setAttribute(Qt::WA_DontShowOnScreen, true);
    }

protected:
    void showEvent(QShowEvent* event) override
    {
        Q_UNUSED(event);
    }

    void hideEvent(QHideEvent* event) override
    {
        Q_UNUSED(event);
    }
};

class HeadlessRegionSelector final : public RegionSelector
{
public:
    using RegionSelector::RegionSelector;

protected:
    void closeEvent(QCloseEvent* event) override
    {
        event->ignore();
    }
};

} // namespace

class tst_RegionSelectorTransientUiCancelGuard : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testAppEscapeIgnoredWhenDropdownOpen();
    void testApplicationDeactivateIgnoredWhenBlockingDialogOpen();
    void testDetachedWindowDeactivateGuardIgnoresSyntheticDeactivate();
    void testApplicationDeactivateCancelsWithoutGuard();
    void testWidgetEscapeIgnoredWhenBlockingUiOpen();
    void testEmojiPickerIsNotBlockingTransientUi();
    void testShareResultCloseKeepsCaptureSession();
    void testOCRResultCloseKeepsCaptureSession();
    void testQRCodeResultCloseKeepsCaptureSession();
    void testWidgetEscapeCancelsWithoutBlockingUi();
    void testAppEscapeCancelsWithoutBlockingUi();

private:
    static void installHeadlessTransientUi(RegionSelector& selector);
};

void tst_RegionSelectorTransientUiCancelGuard::initTestCase()
{
}

void tst_RegionSelectorTransientUiCancelGuard::installHeadlessTransientUi(RegionSelector& selector)
{
    selector.m_dialogFactory =
        [](const QUrl& qmlSource, QObject* viewModel, const QString& contextPropertyName, QObject* parent) {
            return new HeadlessQmlDialog(qmlSource, viewModel, contextPropertyName, parent);
        };
    selector.m_emojiPickerFactory =
        [](QObject* parent) { return new HeadlessEmojiPickerPopup(parent); };
    selector.m_colorPickerDialogFactory =
        []() { return std::make_unique<HeadlessColorPickerDialog>(); };
    selector.m_restoreAfterDialogCancelledHook = []() {};
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

void tst_RegionSelectorTransientUiCancelGuard::testDetachedWindowDeactivateGuardIgnoresSyntheticDeactivate()
{
#ifndef Q_OS_WIN
    QSKIP("Detached capture deactivation guard is Windows-specific.");
#else
    HeadlessRegionSelector selector;
    selector.setAttribute(Qt::WA_DeleteOnClose, false);
    selector.m_activationCount = 1;
    selector.armDetachedWindowDeactivateGuard();

    QSignalSpy cancelledSpy(&selector, &RegionSelector::selectionCancelled);
    QEvent event(QEvent::ApplicationDeactivate);

    const bool handled = selector.eventFilter(qApp, &event);

    QVERIFY(!handled);
    QCOMPARE(cancelledSpy.count(), 0);
    QVERIFY(!selector.m_detachedWindowDeactivateGuardPending);
#endif
}

void tst_RegionSelectorTransientUiCancelGuard::testApplicationDeactivateCancelsWithoutGuard()
{
    HeadlessRegionSelector selector;
    selector.setAttribute(Qt::WA_DeleteOnClose, false);
    selector.m_activationCount = 1;

    QSignalSpy cancelledSpy(&selector, &RegionSelector::selectionCancelled);
    QEvent event(QEvent::ApplicationDeactivate);

    const bool handled = selector.eventFilter(qApp, &event);

    QVERIFY(!handled);
    QCOMPARE(cancelledSpy.count(), 1);
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

void tst_RegionSelectorTransientUiCancelGuard::testEmojiPickerIsNotBlockingTransientUi()
{
    auto* selector = new HeadlessRegionSelector;
    selector->setAttribute(Qt::WA_DeleteOnClose, false);
    installHeadlessTransientUi(*selector);

    selector->m_emojiPickerPopup = new HeadlessEmojiPickerPopup();
    selector->m_emojiPickerPopup->showAt(QRect(QPoint(10, 10), QSize(1, 1)));

    QVERIFY(selector->m_emojiPickerPopup);
    QVERIFY(selector->m_emojiPickerPopup->isVisible());
    QVERIFY(!selector->hasBlockingTransientUiOpen());
}

void tst_RegionSelectorTransientUiCancelGuard::testShareResultCloseKeepsCaptureSession()
{
    RegionSelector selector;
    selector.setAttribute(Qt::WA_DeleteOnClose, false);
    installHeadlessTransientUi(selector);

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
    installHeadlessTransientUi(selector);

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
    installHeadlessTransientUi(selector);

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

void tst_RegionSelectorTransientUiCancelGuard::testWidgetEscapeCancelsWithoutBlockingUi()
{
    HeadlessRegionSelector selector;
    selector.setAttribute(Qt::WA_DeleteOnClose, false);

    QSignalSpy cancelledSpy(&selector, &RegionSelector::selectionCancelled);
    QKeyEvent event(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);

    selector.keyPressEvent(&event);

    QCOMPARE(cancelledSpy.count(), 1);
}

void tst_RegionSelectorTransientUiCancelGuard::testAppEscapeCancelsWithoutBlockingUi()
{
    HeadlessRegionSelector selector;
    selector.setAttribute(Qt::WA_DeleteOnClose, false);

    QSignalSpy cancelledSpy(&selector, &RegionSelector::selectionCancelled);
    QKeyEvent event(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);

    const bool handled = selector.eventFilter(qApp, &event);

    QVERIFY(handled);
    QCOMPARE(cancelledSpy.count(), 1);
}

QTEST_MAIN(tst_RegionSelectorTransientUiCancelGuard)
#include "tst_TransientUiCancelGuard.moc"
