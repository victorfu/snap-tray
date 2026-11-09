#include <QtTest>
#include <QPainter>
#include <opencv2/imgproc.hpp>

#include "annotations/MosaicRectAnnotation.h"

namespace {
QPixmap sourcePixmap(qreal dpr, bool alternate = false)
{
    QImage image(QSize(qRound(120 * dpr), qRound(100 * dpr)), QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            image.setPixelColor(x, y, QColor((x * 17 + (alternate ? 90 : 0)) % 256,
                                             (y * 23) % 256, ((x + y) * 11) % 256));
        }
    }
    image.setDevicePixelRatio(dpr);
    return QPixmap::fromImage(image);
}

QImage render(const MosaicRectAnnotation& annotation, qreal targetDpr)
{
    QImage image(QSize(qRound(160 * targetDpr), qRound(140 * targetDpr)),
                 QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(targetDpr);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    annotation.draw(painter);
    return image;
}

QImage reference(const QPixmap& source, const QRect& rect, qreal targetDpr)
{
    const qreal dpr = source.devicePixelRatio();
    const QRect deviceRect(int(rect.x() * dpr), int(rect.y() * dpr),
                           int(rect.width() * dpr), int(rect.height() * dpr));
    const QRect clipped = deviceRect.intersected(source.rect());
    QImage patch = source.toImage().copy(clipped).convertToFormat(QImage::Format_RGB32);
    patch.setDevicePixelRatio(1);
    cv::Mat input(patch.height(), patch.width(), CV_8UC4, patch.bits(), patch.bytesPerLine());
    cv::Mat blurred;
    cv::GaussianBlur(input, blurred, cv::Size(), qMax(1.0, 12 * dpr / 2));
    QImage canvas(deviceRect.size(), QImage::Format_ARGB32);
    canvas.fill(Qt::transparent);
    const QPoint offset = clipped.topLeft() - deviceRect.topLeft();
    // Copy the reference pixel-by-pixel; do not repeat the production drawImage path.
    for (int y = 0; y < blurred.rows; ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(blurred.ptr(y));
        for (int x = 0; x < blurred.cols; ++x)
            canvas.setPixel(offset.x() + x, offset.y() + y, row[x]);
    }
    QPixmap overlay = QPixmap::fromImage(canvas);
    overlay.setDevicePixelRatio(dpr);
    QImage output(QSize(qRound(160 * targetDpr), qRound(140 * targetDpr)),
                  QImage::Format_ARGB32_Premultiplied);
    output.setDevicePixelRatio(targetDpr);
    output.fill(Qt::transparent);
    QPainter painter(&output);
    painter.drawPixmap(rect.topLeft(), overlay);
    return output;
}
}

class TestMosaicRectAnnotation : public QObject
{
    Q_OBJECT
private slots:
    void gaussianCoverage_data();
    void gaussianCoverage();
};

void TestMosaicRectAnnotation::gaussianCoverage_data()
{
    QTest::addColumn<qreal>("sourceDpr");
    QTest::addColumn<qreal>("targetDpr");
    QTest::addColumn<QRect>("rect");
    for (qreal sourceDpr : {1.0, 1.5, 2.0}) {
        for (qreal targetDpr : {1.0, 1.5, 2.0}) {
            for (const QRect rect : {QRect(14, 18, 80, 60), QRect(90, 70, 50, 50),
                                      QRect(-10, -8, 70, 60)}) {
                const QByteArray name = QString("%1-to-%2-%3-%4")
                    .arg(sourceDpr).arg(targetDpr).arg(rect.x()).arg(rect.y()).toLatin1();
                QTest::newRow(name.constData()) << sourceDpr << targetDpr << rect;
            }
        }
    }
}

void TestMosaicRectAnnotation::gaussianCoverage()
{
    QFETCH(qreal, sourceDpr);
    QFETCH(qreal, targetDpr);
    QFETCH(QRect, rect);
    auto source = std::make_shared<const QPixmap>(sourcePixmap(sourceDpr));
    MosaicRectAnnotation annotation(rect, source, 12, MosaicBlurType::Gaussian);
    QCOMPARE(render(annotation, sourceDpr), reference(*source, rect, sourceDpr));
    QCOMPARE(render(annotation, targetDpr), reference(*source, rect, targetDpr));
    QCOMPARE(render(annotation, targetDpr), reference(*source, rect, targetDpr));
    auto replacement = std::make_shared<const QPixmap>(sourcePixmap(sourceDpr, true));
    annotation.setSourcePixmap(replacement);
    QCOMPARE(render(annotation, targetDpr), reference(*replacement, rect, targetDpr));
}

QTEST_MAIN(TestMosaicRectAnnotation)
#include "tst_MosaicRectAnnotation.moc"
