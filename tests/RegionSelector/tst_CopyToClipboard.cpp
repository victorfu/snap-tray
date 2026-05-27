#include <QtTest/QtTest>

#include "RegionSelector.h"
#include "RegionSelectorTestAccess.h"

class tst_RegionSelectorCopyToClipboard : public QObject
{
    Q_OBJECT

private slots:
    void testCopyWritesClipboardBeforeClosingSelector();
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
        [&](const QImage& image) {
            writerCalled = true;
            writerSawClosingSelector = RegionSelectorTestAccess::isClosing(selector);
            copiedSize = image.size();
            return true;
        });

    RegionSelectorTestAccess::invokeCopyToClipboard(selector);

    QVERIFY(writerCalled);
    QVERIFY(!writerSawClosingSelector);
    QVERIFY(!copiedSize.isEmpty());
}

QTEST_MAIN(tst_RegionSelectorCopyToClipboard)
#include "tst_CopyToClipboard.moc"
