#include <QtTest/QtTest>
#include <memory>

#include "RegionSelector.h"
#include "RegionSelectorTestAccess.h"

class tst_RegionSelectorCopyToClipboard : public QObject
{
    Q_OBJECT

private slots:
    void testCopyWaitsForClipboardCompletionBeforeClosingSelector();
    void testCopyDoesNotBlockOnSlowClipboardWriter();
    void testClipboardFailureKeepsSelectorOpenAndAllowsRetry();
    void testCompletionAfterSelectorDestructionIsIgnored();
};

void tst_RegionSelectorCopyToClipboard::testCopyWaitsForClipboardCompletionBeforeClosingSelector()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No screens available for RegionSelector copy test.");
    }

    RegionSelector selector;
    selector.setAttribute(Qt::WA_DeleteOnClose, false);

    const QSize captureSize = screen->geometry().size().boundedTo(QSize(320, 240));
    QPixmap preCapture(captureSize);
    preCapture.fill(Qt::red);
    selector.initializeForScreen(screen, preCapture);
    RegionSelectorTestAccess::setSelectionRect(selector, QRect(10, 10, 40, 30));

    int writerCallCount = 0;
    bool writerSawClosingSelector = true;
    QSize copiedSize;
    std::function<void(bool)> pendingCompletion;
    QSignalSpy copyRequestedSpy(&selector, &RegionSelector::copyRequested);
    RegionSelectorTestAccess::setGuiClipboardWriter(
        selector,
        [&](const QImage& image, std::function<void(bool)> completion) {
            ++writerCallCount;
            writerSawClosingSelector = RegionSelectorTestAccess::isClosing(selector);
            copiedSize = image.size();
            pendingCompletion = std::move(completion);
        });

    RegionSelectorTestAccess::invokeCopyToClipboard(selector);

    QCOMPARE(writerCallCount, 1);
    QVERIFY(!writerSawClosingSelector);
    QVERIFY(!copiedSize.isEmpty());
    QVERIFY(pendingCompletion);
    QVERIFY(!RegionSelectorTestAccess::isClosing(selector));
    QCOMPARE(copyRequestedSpy.count(), 0);

    RegionSelectorTestAccess::invokeCopyToClipboard(selector);
    QCOMPARE(writerCallCount, 1);

    pendingCompletion(true);

    QTRY_VERIFY(RegionSelectorTestAccess::isClosing(selector));
    QCOMPARE(copyRequestedSpy.count(), 1);
}

void tst_RegionSelectorCopyToClipboard::testCopyDoesNotBlockOnSlowClipboardWriter()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No screens available for RegionSelector copy test.");
    }

    RegionSelector selector;
    selector.setAttribute(Qt::WA_DeleteOnClose, false);

    const QSize captureSize = screen->geometry().size().boundedTo(QSize(320, 240));
    QPixmap preCapture(captureSize);
    preCapture.fill(Qt::blue);
    selector.initializeForScreen(screen, preCapture);
    RegionSelectorTestAccess::setSelectionRect(selector, QRect(10, 10, 40, 30));

    bool writerCalled = false;
    bool completionCalled = false;
    QObject completionContext;
    RegionSelectorTestAccess::setGuiClipboardWriter(
        selector,
        [&](const QImage&, std::function<void(bool)> completion) {
            writerCalled = true;
            // Bind the pending callback to this test invocation. If an
            // assertion exits early it must not write a dead stack variable
            // during the next test's event processing.
            QTimer::singleShot(0, &completionContext, [&completionCalled, completion = std::move(completion)]() mutable {
                completionCalled = true;
                if (completion) {
                    completion(true);
                }
            });
        });

    RegionSelectorTestAccess::invokeCopyToClipboard(selector);

    QVERIFY(writerCalled);
    // copyToClipboard must return before dispatching the queued writer. This
    // detects a nested completion wait without a machine-speed threshold.
    QVERIFY2(!completionCalled, "copyToClipboard() waited for the clipboard writer");
    QVERIFY(!RegionSelectorTestAccess::isClosing(selector));
    QTRY_VERIFY(completionCalled);
    QTRY_VERIFY(RegionSelectorTestAccess::isClosing(selector));
}

void tst_RegionSelectorCopyToClipboard::testClipboardFailureKeepsSelectorOpenAndAllowsRetry()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No screens available for RegionSelector copy test.");
    }

    RegionSelector selector;
    selector.setAttribute(Qt::WA_DeleteOnClose, false);

    const QSize captureSize = screen->geometry().size().boundedTo(QSize(320, 240));
    QPixmap preCapture(captureSize);
    preCapture.fill(Qt::green);
    selector.initializeForScreen(screen, preCapture);
    RegionSelectorTestAccess::setSelectionRect(selector, QRect(10, 10, 40, 30));
    RegionSelectorTestAccess::setRestoreAfterDialogCancelledHook(selector, []() {});

    int writerCallCount = 0;
    std::function<void(bool)> pendingCompletion;
    QSignalSpy copyRequestedSpy(&selector, &RegionSelector::copyRequested);
    RegionSelectorTestAccess::setGuiClipboardWriter(
        selector,
        [&](const QImage&, std::function<void(bool)> completion) {
            ++writerCallCount;
            pendingCompletion = std::move(completion);
        });

    RegionSelectorTestAccess::invokeCopyToClipboard(selector);
    QCOMPARE(writerCallCount, 1);
    QVERIFY(pendingCompletion);

    pendingCompletion(false);
    QCoreApplication::processEvents();

    QVERIFY(!RegionSelectorTestAccess::isClosing(selector));
    QCOMPARE(copyRequestedSpy.count(), 0);

    RegionSelectorTestAccess::invokeCopyToClipboard(selector);
    QCOMPARE(writerCallCount, 2);
    QVERIFY(pendingCompletion);

    pendingCompletion(true);
    QTRY_VERIFY(RegionSelectorTestAccess::isClosing(selector));
    QCOMPARE(copyRequestedSpy.count(), 1);
}

void tst_RegionSelectorCopyToClipboard::testCompletionAfterSelectorDestructionIsIgnored()
{
    auto* screen = QGuiApplication::primaryScreen();
    if (!screen) QSKIP("No screens available for RegionSelector copy test.");
    auto selector = std::make_unique<RegionSelector>();
    selector->setAttribute(Qt::WA_DeleteOnClose, false);
    QPixmap preCapture(screen->geometry().size().boundedTo(QSize(320, 240)));
    preCapture.fill(Qt::red);
    selector->initializeForScreen(screen, preCapture);
    RegionSelectorTestAccess::setSelectionRect(*selector, QRect(10, 10, 40, 30));
    std::function<void(bool)> pending;
    RegionSelectorTestAccess::setGuiClipboardWriter(*selector,
        [&](const QImage&, std::function<void(bool)> completion) {
            pending = std::move(completion);
        });
    RegionSelectorTestAccess::invokeCopyToClipboard(*selector);
    QVERIFY(pending);
    selector.reset();
    pending(false);
    pending(true);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

QTEST_MAIN(tst_RegionSelectorCopyToClipboard)
#include "tst_CopyToClipboard.moc"
