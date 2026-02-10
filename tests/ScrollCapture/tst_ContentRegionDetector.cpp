#include <QtTest>
#include <QPainter>

#include "scrollcapture/ContentRegionDetector.h"

#include <cmath>

namespace {
QImage buildScrollablePattern(int width, int height)
{
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(Qt::white);

    QPainter painter(&image);
    for (int y = 0; y < height; y += 24) {
        const QColor color = ((y / 24) % 2 == 0)
            ? QColor(232, 238, 255)
            : QColor(245, 248, 255);
        painter.fillRect(0, y, width, 24, color);
        painter.setPen(QColor(80, 96, 140));
        painter.drawText(16, y + 16, QStringLiteral("Line %1").arg(y));
    }
    painter.end();
    return image;
}

QImage buildWindowFrame(const QImage& content,
                        int offsetY,
                        int windowWidth,
                        int windowHeight,
                        int headerHeight,
                        int footerHeight)
{
    QImage frame(windowWidth, windowHeight, QImage::Format_ARGB32);
    frame.fill(QColor(32, 34, 40));

    QPainter painter(&frame);
    painter.fillRect(0, 0, windowWidth, headerHeight, QColor(56, 60, 72));
    painter.fillRect(0, windowHeight - footerHeight, windowWidth, footerHeight, QColor(52, 56, 66));

    const int contentHeight = windowHeight - headerHeight - footerHeight;
    const QRect sourceRect(0, offsetY, windowWidth, contentHeight);
    painter.drawImage(QRect(0, headerHeight, windowWidth, contentHeight), content, sourceRect);
    painter.end();
    return frame;
}
} // namespace

class tst_ContentRegionDetector : public QObject
{
    Q_OBJECT

private slots:
    void testDetectScrollableArea();
};

void tst_ContentRegionDetector::testDetectScrollableArea()
{
    constexpr int kWindowWidth = 620;
    constexpr int kWindowHeight = 860;
    constexpr int kHeader = 104;
    constexpr int kFooter = 88;
    constexpr int kScrollOffset = 120;

    const QImage longContent = buildScrollablePattern(kWindowWidth, 3200);
    const QImage before = buildWindowFrame(longContent, 0, kWindowWidth, kWindowHeight, kHeader, kFooter);
    const QImage after = buildWindowFrame(longContent, kScrollOffset, kWindowWidth, kWindowHeight, kHeader, kFooter);

    const QRect globalWindowRect(120, 80, kWindowWidth, kWindowHeight);
    ContentRegionDetector detector;
    const ContentRegionDetector::DetectionResult result = detector.detect(before, after, globalWindowRect);

    QVERIFY(result.success);
    QVERIFY(result.contentRect.isValid());
    QVERIFY(!result.fallbackUsed);

    const int expectedTop = globalWindowRect.top() + kHeader;
    const int expectedBottom = globalWindowRect.bottom() - kFooter;

    QVERIFY(std::abs(result.contentRect.top() - expectedTop) <= 24);
    QVERIFY(std::abs(result.contentRect.bottom() - expectedBottom) <= 24);
}

QTEST_MAIN(tst_ContentRegionDetector)
#include "tst_ContentRegionDetector.moc"
