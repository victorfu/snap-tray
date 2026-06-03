#include <QtTest/QtTest>

#include "RegionSelector.h"
#include "RegionSelectorTestAccess.h"

class tst_RegionSelectorCopyToClipboard : public QObject
{
    Q_OBJECT

private slots:
    void testCopyWritesClipboardBeforeClosingSelector();
    void testCopyDoesNotBlockOnSlowClipboardWriter();
};

void tst_RegionSelectorCopyToClipboard::testCopyWritesClipboardBeforeClosingSelector()
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

    bool writerCalled = false;
    bool writerSawClosingSelector = true;
    QSize copiedSize;
    RegionSelectorTestAccess::setGuiClipboardWriter(
        selector,
        [&](const QImage& image, std::function<void(bool)> completion) {
            writerCalled = true;
            writerSawClosingSelector = RegionSelectorTestAccess::isClosing(selector);
            copiedSize = image.size();
            if (completion) {
                completion(true);
            }
        });

    RegionSelectorTestAccess::invokeCopyToClipboard(selector);

    QVERIFY(writerCalled);
    QVERIFY(!writerSawClosingSelector);
    QVERIFY(!copiedSize.isEmpty());
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
    RegionSelectorTestAccess::setGuiClipboardWriter(
        selector,
        [&](const QImage&, std::function<void(bool)> completion) {
            QTimer::singleShot(150, qApp, [&writerCalled, completion = std::move(completion)]() mutable {
                writerCalled = true;
                if (completion) {
                    completion(true);
                }
            });
        });

    QElapsedTimer timer;
    timer.start();
    RegionSelectorTestAccess::invokeCopyToClipboard(selector);

    QVERIFY2(timer.elapsed() < 80, "copyToClipboard() blocked on the clipboard writer");
    QTRY_VERIFY(writerCalled);
}

QTEST_MAIN(tst_RegionSelectorCopyToClipboard)
#include "tst_CopyToClipboard.moc"
