#include <QtTest/QtTest>

#include <QImage>
#include <QPainter>
#include <QTemporaryDir>

#include "WatermarkRenderer.h"

class TestWatermarkRenderer : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void renderRestoresOriginalPainterOpacity();
    void renderDoesNotDimSubsequentDrawing();

private:
    QTemporaryDir m_tempDir;
    QString m_watermarkPath;
};

void TestWatermarkRenderer::initTestCase()
{
    QVERIFY(m_tempDir.isValid());

    m_watermarkPath = m_tempDir.filePath("watermark.png");
    QImage watermark(4, 4, QImage::Format_ARGB32_Premultiplied);
    watermark.fill(Qt::red);
    QVERIFY(watermark.save(m_watermarkPath));
}

void TestWatermarkRenderer::renderRestoresOriginalPainterOpacity()
{
    QImage canvas(24, 12, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);

    WatermarkRenderer::Settings settings;
    settings.enabled = true;
    settings.imagePath = m_watermarkPath;
    settings.opacity = 0.25;
    settings.position = WatermarkRenderer::TopLeft;
    settings.margin = 0;

    QPainter painter(&canvas);
    painter.setOpacity(0.75);

    WatermarkRenderer::render(painter, canvas.rect(), settings);

    QCOMPARE(painter.opacity(), 0.75);
}

void TestWatermarkRenderer::renderDoesNotDimSubsequentDrawing()
{
    QImage canvas(24, 12, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);

    WatermarkRenderer::Settings settings;
    settings.enabled = true;
    settings.imagePath = m_watermarkPath;
    settings.opacity = 0.25;
    settings.position = WatermarkRenderer::TopLeft;
    settings.margin = 0;

    QPainter painter(&canvas);
    QVERIFY(painter.isActive());

    WatermarkRenderer::render(painter, canvas.rect(), settings);

    QCOMPARE(painter.opacity(), 1.0);
    painter.fillRect(QRect(16, 4, 4, 4), Qt::blue);
    painter.end();

    const QColor watermarkPixel = canvas.pixelColor(1, 1);
    QCOMPARE(watermarkPixel.red(), 255);
    QVERIFY(watermarkPixel.alpha() >= 63 && watermarkPixel.alpha() <= 64);
    QCOMPARE(canvas.pixelColor(17, 5), QColor(Qt::blue));
}

QTEST_MAIN(TestWatermarkRenderer)
#include "tst_WatermarkRenderer.moc"
