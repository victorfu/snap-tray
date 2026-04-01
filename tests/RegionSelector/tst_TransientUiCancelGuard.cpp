#include <QtTest/QtTest>

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPixmap>
#include <QScreen>
#include <QSignalSpy>
#include <QtQml/qqmlextensionplugin.h>

#include "RegionSelector.h"
#include "RegionSelectorTestAccess.h"
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

QScreen* primaryOrSkip()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        return nullptr;
    }
    return screen;
}

QPixmap makePreCapture(const QSize& size, const QColor& color)
{
    QPixmap pixmap(size);
    pixmap.fill(color);
    return pixmap;
}

OCRTextBlock makeOcrBlock(const QString& text, qreal x, qreal y, qreal width = 0.08, qreal height = 0.03)
{
    OCRTextBlock block;
    block.text = text;
    block.boundingRect = QRectF(x, y, width, height);
    block.confidence = 0.9f;
    return block;
}

class HeadlessQmlDialog final : public SnapTray::QmlDialog
{
public:
    enum class ShowMode {
        None,
        Absolute,
        CenteredOnScreen
    };

    HeadlessQmlDialog(const QUrl& qmlSource,
                      QObject* viewModel,
                      const QString& contextPropertyName,
                      QObject* parent = nullptr)
        : QmlDialog(qmlSource, viewModel, contextPropertyName, parent)
        , m_qmlSource(qmlSource)
    {
    }

    void showAt(const QPoint& pos = QPoint()) override
    {
        m_showMode = ShowMode::Absolute;
        m_lastPos = pos;
        m_lastScreen = nullptr;
        m_visible = true;
    }

    void showCenteredOnScreen(QScreen* screen) override
    {
        m_showMode = ShowMode::CenteredOnScreen;
        m_lastScreen = screen;
        m_lastPos = {};
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

    ShowMode showMode() const
    {
        return m_showMode;
    }

    QUrl qmlSource() const
    {
        return m_qmlSource;
    }

    QScreen* lastScreen() const
    {
        return m_lastScreen;
    }

private:
    bool m_visible = false;
    ShowMode m_showMode = ShowMode::None;
    QUrl m_qmlSource;
    QPoint m_lastPos;
    QScreen* m_lastScreen = nullptr;
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
    void testSharePasswordUsesCenteredScreenApi();
    void testShareResultCloseKeepsCaptureSession();
    void testOCRResultCloseKeepsCaptureSession();
    void testOCRResultCopyTextClosesViewModel();
    void testOCRResultCopyAsTsvClosesViewModel();
    void testQRCodeResultCloseKeepsCaptureSession();
    void testWidgetEscapeCancelsWithoutBlockingUi();
    void testAppEscapeCancelsWithoutBlockingUi();

private:
    static void installHeadlessTransientUi(RegionSelector& selector);
    static HeadlessQmlDialog* findHeadlessDialog(const RegionSelector& selector, const QString& suffix);
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

HeadlessQmlDialog* tst_RegionSelectorTransientUiCancelGuard::findHeadlessDialog(
    const RegionSelector& selector,
    const QString& suffix)
{
    const auto dialogs = selector.findChildren<SnapTray::QmlDialog*>();
    for (auto it = dialogs.crbegin(); it != dialogs.crend(); ++it) {
        if (auto* dialog = dynamic_cast<HeadlessQmlDialog*>(*it);
            dialog && dialog->qmlSource().toString().endsWith(suffix)) {
            return dialog;
        }
    }
    return nullptr;
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

void tst_RegionSelectorTransientUiCancelGuard::testSharePasswordUsesCenteredScreenApi()
{
    QScreen* screen = primaryOrSkip();
    if (!screen) {
        QSKIP("No screens available for RegionSelector share password test.");
    }

    RegionSelector selector;
    selector.setAttribute(Qt::WA_DeleteOnClose, false);
    installHeadlessTransientUi(selector);

    const QSize preCaptureSize = screen->geometry().size().boundedTo(QSize(320, 240));
    selector.initializeForScreen(screen, makePreCapture(preCaptureSize, Qt::darkGreen));
    RegionSelectorTestAccess::setSelectionRect(selector, QRect(20, 20, 80, 60));

    selector.shareToUrl();
    QCoreApplication::processEvents();

    auto* dialog = findHeadlessDialog(selector, QStringLiteral("SharePasswordDialog.qml"));
    QVERIFY(dialog);
    QCOMPARE(dialog->showMode(), HeadlessQmlDialog::ShowMode::CenteredOnScreen);
    QCOMPARE(dialog->lastScreen(), screen);
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
    auto* dialog = findHeadlessDialog(selector, QStringLiteral("ShareResultDialog.qml"));
    QVERIFY(dialog);
    QCOMPARE(dialog->showMode(), HeadlessQmlDialog::ShowMode::CenteredOnScreen);
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
    auto* dialog = findHeadlessDialog(selector, QStringLiteral("OCRResultDialog.qml"));
    QVERIFY(dialog);
    QCOMPARE(dialog->showMode(), HeadlessQmlDialog::ShowMode::CenteredOnScreen);
    QVERIFY(selector.m_openBlockingDialogCount > 0);

    vm->close();
    QCoreApplication::processEvents();

    QCOMPARE(cancelledSpy.count(), 0);
    QCOMPARE(selector.m_openBlockingDialogCount, 0);
}

void tst_RegionSelectorTransientUiCancelGuard::testOCRResultCopyTextClosesViewModel()
{
    OCRResult result;
    result.success = true;
    result.text = QStringLiteral("Detected text");

    OCRResultViewModel vm;
    vm.setOCRResult(result);

    QSignalSpy copiedSpy(&vm, &OCRResultViewModel::textCopied);
    QSignalSpy closedSpy(&vm, &OCRResultViewModel::dialogClosed);

    vm.copyText();

    QCOMPARE(copiedSpy.count(), 1);
    QCOMPARE(copiedSpy.takeFirst().at(0).toString(), QStringLiteral("Detected text"));
    QCOMPARE(closedSpy.count(), 1);
}

void tst_RegionSelectorTransientUiCancelGuard::testOCRResultCopyAsTsvClosesViewModel()
{
    OCRResult result;
    result.success = true;
    result.text = QStringLiteral("Name Score Rank\nAlice 95 1\nBob 88 2");
    result.blocks = {
        makeOcrBlock(QStringLiteral("Name"), 0.10, 0.10),
        makeOcrBlock(QStringLiteral("Score"), 0.315, 0.102),
        makeOcrBlock(QStringLiteral("Rank"), 0.57, 0.099),
        makeOcrBlock(QStringLiteral("Alice"), 0.102, 0.145),
        makeOcrBlock(QStringLiteral("95"), 0.318, 0.147),
        makeOcrBlock(QStringLiteral("1"), 0.572, 0.143),
        makeOcrBlock(QStringLiteral("Bob"), 0.098, 0.19),
        makeOcrBlock(QStringLiteral("88"), 0.314, 0.191),
        makeOcrBlock(QStringLiteral("2"), 0.568, 0.189),
    };

    OCRResultViewModel vm;
    vm.setOCRResult(result);
    QVERIFY(vm.hasTsv());

    QSignalSpy copiedSpy(&vm, &OCRResultViewModel::textCopied);
    QSignalSpy closedSpy(&vm, &OCRResultViewModel::dialogClosed);

    vm.copyAsTsv();

    QCOMPARE(copiedSpy.count(), 1);
    QCOMPARE(copiedSpy.takeFirst().at(0).toString(),
             QStringLiteral("Name\tScore\tRank\nAlice\t95\t1\nBob\t88\t2"));
    QCOMPARE(closedSpy.count(), 1);
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
    auto* dialog = findHeadlessDialog(selector, QStringLiteral("QRCodeResultDialog.qml"));
    QVERIFY(dialog);
    QCOMPARE(dialog->showMode(), HeadlessQmlDialog::ShowMode::CenteredOnScreen);
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
