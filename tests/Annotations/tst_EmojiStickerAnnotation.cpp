#include <QtTest/QtTest>
#include <QImage>
#include <QPainter>

#include "annotations/EmojiStickerAnnotation.h"

class TestEmojiStickerAnnotation : public QObject
{
    Q_OBJECT

private slots:
    void testDefaultTransformState();
    void testRotationChangesBoundsButKeepsCenter();
    void testMirrorPreservesHitAtCenter();
    void testClonePreservesTransformState();
    void testDrawWithTransformProducesPixels();
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

QTEST_MAIN(TestEmojiStickerAnnotation)
#include "tst_EmojiStickerAnnotation.moc"
