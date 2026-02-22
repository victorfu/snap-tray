#include <QtTest/QtTest>
#include <array>
#include <QImage>
#include <QPainter>

#include "annotations/EmojiStickerAnnotation.h"

namespace {
QRect nonTransparentBounds(const QImage& image)
{
    int minX = image.width();
    int minY = image.height();
    int maxX = -1;
    int maxY = -1;

    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) <= 0) {
                continue;
            }
            minX = qMin(minX, x);
            minY = qMin(minY, y);
            maxX = qMax(maxX, x);
            maxY = qMax(maxY, y);
        }
    }

    if (maxX < minX || maxY < minY) {
        return QRect();
    }

    return QRect(QPoint(minX, minY), QPoint(maxX, maxY));
}
} // namespace

class TestEmojiStickerAnnotation : public QObject
{
    Q_OBJECT

private slots:
    void testDefaultTransformState();
    void testRotationChangesBoundsButKeepsCenter();
    void testMirrorPreservesHitAtCenter();
    void testClonePreservesTransformState();
    void testDrawWithTransformProducesPixels();
    void testScaleKeepsCenterAtAnchor();
    void testGeometryMatchesRenderedPixels();
};

void TestEmojiStickerAnnotation::testDefaultTransformState()
{
    EmojiStickerAnnotation sticker(QPoint(80, 80), QStringLiteral("WW"), 1.0);
    QCOMPARE(sticker.rotation(), 0.0);
    QCOMPARE(sticker.mirrorX(), false);
    QCOMPARE(sticker.mirrorY(), false);
}

void TestEmojiStickerAnnotation::testRotationChangesBoundsButKeepsCenter()
{
    EmojiStickerAnnotation sticker(QPoint(120, 120), QStringLiteral("WWWW"), 1.0);

    const QRectF baseBounds = sticker.transformedBoundingPolygon().boundingRect();
    const QPointF baseCenter = baseBounds.center();
    QVERIFY(baseBounds.width() > baseBounds.height());

    sticker.setRotation(90.0);
    const QRectF rotatedBounds = sticker.transformedBoundingPolygon().boundingRect();
    const QPointF rotatedCenter = rotatedBounds.center();

    QVERIFY(qAbs(rotatedCenter.x() - baseCenter.x()) < 0.01);
    QVERIFY(qAbs(rotatedCenter.y() - baseCenter.y()) < 0.01);
    QVERIFY(rotatedBounds.height() > rotatedBounds.width());
}

void TestEmojiStickerAnnotation::testMirrorPreservesHitAtCenter()
{
    EmojiStickerAnnotation sticker(QPoint(100, 100), QStringLiteral("WW"), 1.0);
    const QPoint center = sticker.center().toPoint();

    QVERIFY(sticker.containsPoint(center));
    sticker.setMirror(true, false);
    QVERIFY(sticker.containsPoint(center));
    sticker.setMirror(false, true);
    QVERIFY(sticker.containsPoint(center));
    sticker.setMirror(true, true);
    QVERIFY(sticker.containsPoint(center));
}

void TestEmojiStickerAnnotation::testClonePreservesTransformState()
{
    EmojiStickerAnnotation original(QPoint(140, 90), QStringLiteral("WW"), 1.25);
    original.setRotation(270.0);
    original.setMirror(true, true);

    auto clonedBase = original.clone();
    auto* cloned = dynamic_cast<EmojiStickerAnnotation*>(clonedBase.get());
    QVERIFY(cloned != nullptr);

    QCOMPARE(cloned->position(), original.position());
    QCOMPARE(cloned->emoji(), original.emoji());
    QCOMPARE(cloned->scale(), original.scale());
    QCOMPARE(cloned->rotation(), original.rotation());
    QCOMPARE(cloned->mirrorX(), original.mirrorX());
    QCOMPARE(cloned->mirrorY(), original.mirrorY());
}

void TestEmojiStickerAnnotation::testDrawWithTransformProducesPixels()
{
    EmojiStickerAnnotation sticker(QPoint(90, 90), QStringLiteral("WW"), 1.0);
    sticker.setRotation(90.0);
    sticker.setMirror(true, false);

    QImage image(220, 220, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    sticker.draw(painter);
    painter.end();

    bool hasVisiblePixel = false;
    for (int y = 0; y < image.height() && !hasVisiblePixel; ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) > 0) {
                hasVisiblePixel = true;
                break;
            }
        }
    }

    QVERIFY(hasVisiblePixel);
}

void TestEmojiStickerAnnotation::testScaleKeepsCenterAtAnchor()
{
    const QPoint anchor(150, 110);
    EmojiStickerAnnotation sticker(anchor, QString::fromUtf8("\xF0\x9F\x98\x80"), 1.0);

    const std::array<qreal, 5> scales = {0.25, 0.75, 1.0, 2.0, 4.0};
    for (qreal scale : scales) {
        sticker.setScale(scale);
        const QPointF boundsCenter = sticker.transformedBoundingPolygon().boundingRect().center();
        QVERIFY(qAbs(boundsCenter.x() - anchor.x()) < 0.01);
        QVERIFY(qAbs(boundsCenter.y() - anchor.y()) < 0.01);
    }
}

void TestEmojiStickerAnnotation::testGeometryMatchesRenderedPixels()
{
    EmojiStickerAnnotation sticker(QPoint(140, 120), QString::fromUtf8("\xF0\x9F\x98\x80"), 1.8);

    QImage image(320, 260, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    sticker.draw(painter);
    painter.end();

    const QRect renderedBounds = nonTransparentBounds(image);
    QVERIFY(!renderedBounds.isEmpty());

    const QRect geometryBounds = sticker.transformedBoundingPolygon().boundingRect().toAlignedRect();
    QVERIFY(!geometryBounds.isEmpty());

    const QPointF renderedCenter = renderedBounds.center();
    const QPointF geometryCenter = geometryBounds.center();
    QVERIFY(qAbs(renderedCenter.x() - geometryCenter.x()) <= 3.0);
    QVERIFY(qAbs(renderedCenter.y() - geometryCenter.y()) <= 3.0);

    QVERIFY(qAbs(renderedBounds.width() - geometryBounds.width()) <= 10);
    QVERIFY(qAbs(renderedBounds.height() - geometryBounds.height()) <= 10);
}

QTEST_MAIN(TestEmojiStickerAnnotation)
#include "tst_EmojiStickerAnnotation.moc"
