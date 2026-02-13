#include <QtTest/QtTest>
#include <QImage>
#include <QPainter>
#include <QFont>

#include "annotations/AnnotationLayer.h"
#include "annotations/PolylineAnnotation.h"
#include "annotations/TextBoxAnnotation.h"
#include "annotations/EmojiStickerAnnotation.h"
#include "annotations/ErasedItemsGroup.h"

class TestAnnotationLayer : public QObject
{
    Q_OBJECT

private slots:
    void testDrawWithDirtyRegion_ExcludesDraggedItemFromFullCache();
    void testDrawCached_RebuildsAfterExcludeCache();
    void testHitTestText_IgnoresHiddenItems();
    void testHitTestEmojiSticker_IgnoresHiddenItems();
    void testHitTestEmojiSticker_ReturnsTopMostVisible();
    void testSetSelectedIndex_InvalidOrHiddenClearsSelection();
    void testTranslateAll_AlsoTranslatesRedoStackItems();
    void testTranslateAll_TranslatesErasedItemsGroupContents();
    void testRedo_ErasedItemsGroup_ReappliesDeletion();

private:
    static bool hasVisiblePixel(const QImage& image, const QRect& probe);
    static std::unique_ptr<PolylineAnnotation> createPolyline(int y);
    static std::unique_ptr<TextBoxAnnotation> createTextBox(const QPointF& pos, const QString& text);
};

bool TestAnnotationLayer::hasVisiblePixel(const QImage& image, const QRect& probe)
{
    const QRect bounds = probe.intersected(image.rect());
    for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
        for (int x = bounds.left(); x <= bounds.right(); ++x) {
            if (qAlpha(image.pixel(x, y)) > 0) {
                return true;
            }
        }
    }
    return false;
}

std::unique_ptr<PolylineAnnotation> TestAnnotationLayer::createPolyline(int y)
{
    QVector<QPoint> points = {
        QPoint(20, y),
        QPoint(140, y)
    };
    return std::make_unique<PolylineAnnotation>(
        points, QColor(Qt::red), 4, LineEndStyle::None, LineStyle::Solid);
}

std::unique_ptr<TextBoxAnnotation> TestAnnotationLayer::createTextBox(const QPointF& pos,
                                                                      const QString& text)
{
    QFont font;
    font.setPointSize(16);
    font.setBold(true);
    return std::make_unique<TextBoxAnnotation>(pos, text, font, QColor(Qt::red));
}

void TestAnnotationLayer::testDrawWithDirtyRegion_ExcludesDraggedItemFromFullCache()
{
    const QSize canvasSize(180, 100);

    AnnotationLayer layer;
    layer.addItem(createPolyline(20));
    layer.setSelectedIndex(0);

    // Build an initial full cache that includes the polyline at y=20.
    QImage initialFrame(canvasSize, QImage::Format_ARGB32_Premultiplied);
    initialFrame.fill(Qt::transparent);
    {
        QPainter painter(&initialFrame);
        layer.drawCached(painter, canvasSize, 1.0);
    }

    auto* selectedPolyline = dynamic_cast<PolylineAnnotation*>(layer.itemAt(0));
    QVERIFY(selectedPolyline != nullptr);

    // Simulate drag update: move selected polyline down to y=50.
    selectedPolyline->moveBy(QPoint(0, 30));

    QImage dragFrame(canvasSize, QImage::Format_ARGB32_Premultiplied);
    dragFrame.fill(Qt::transparent);
    {
        QPainter painter(&dragFrame);
        layer.drawWithDirtyRegion(painter, canvasSize, 1.0, 0);
    }

    // Old location should not remain visible; only the moved polyline should be drawn.
    QVERIFY(!hasVisiblePixel(dragFrame, QRect(70, 12, 24, 16)));
    QVERIFY(hasVisiblePixel(dragFrame, QRect(70, 42, 24, 16)));
}

void TestAnnotationLayer::testDrawCached_RebuildsAfterExcludeCache()
{
    const QSize canvasSize(180, 100);

    AnnotationLayer layer;
    layer.addItem(createPolyline(30));
    layer.setSelectedIndex(0);

    // Build cache in "exclude selected item" mode.
    QImage dragFrame(canvasSize, QImage::Format_ARGB32_Premultiplied);
    dragFrame.fill(Qt::transparent);
    {
        QPainter painter(&dragFrame);
        layer.drawWithDirtyRegion(painter, canvasSize, 1.0, 0);
    }

    // drawCached must rebuild a full cache and include the selected polyline again.
    QImage cachedFrame(canvasSize, QImage::Format_ARGB32_Premultiplied);
    cachedFrame.fill(Qt::transparent);
    {
        QPainter painter(&cachedFrame);
        layer.drawCached(painter, canvasSize, 1.0);
    }

    QVERIFY(hasVisiblePixel(cachedFrame, QRect(70, 22, 24, 16)));
}

void TestAnnotationLayer::testHitTestText_IgnoresHiddenItems()
{
    AnnotationLayer layer;

    auto hiddenText = createTextBox(QPointF(40, 40), "hidden");
    hiddenText->setVisible(false);
    layer.addItem(std::move(hiddenText));

    layer.addItem(createTextBox(QPointF(40, 40), "visible"));

    const int hitIndex = layer.hitTestText(QPoint(60, 60));
    QCOMPARE(hitIndex, 1);
}

void TestAnnotationLayer::testHitTestEmojiSticker_IgnoresHiddenItems()
{
    AnnotationLayer layer;

    auto hiddenEmoji = std::make_unique<EmojiStickerAnnotation>(
        QPoint(80, 80), QStringLiteral("WW"), 1.0);
    hiddenEmoji->setVisible(false);
    layer.addItem(std::move(hiddenEmoji));

    layer.addItem(std::make_unique<EmojiStickerAnnotation>(
        QPoint(80, 80), QStringLiteral("WW"), 1.0));

    auto* visibleEmoji = dynamic_cast<EmojiStickerAnnotation*>(layer.itemAt(1));
    QVERIFY(visibleEmoji != nullptr);
    const QPoint probe = visibleEmoji->center().toPoint();

    const int hitIndex = layer.hitTestEmojiSticker(probe);
    QCOMPARE(hitIndex, 1);
}

void TestAnnotationLayer::testHitTestEmojiSticker_ReturnsTopMostVisible()
{
    AnnotationLayer layer;

    layer.addItem(std::make_unique<EmojiStickerAnnotation>(
        QPoint(100, 100), QStringLiteral("WW"), 1.0));
    layer.addItem(std::make_unique<EmojiStickerAnnotation>(
        QPoint(100, 100), QStringLiteral("WW"), 1.0));

    auto* topEmoji = dynamic_cast<EmojiStickerAnnotation*>(layer.itemAt(1));
    QVERIFY(topEmoji != nullptr);
    const QPoint probe = topEmoji->center().toPoint();

    const int hitIndex = layer.hitTestEmojiSticker(probe);
    QCOMPARE(hitIndex, 1);
}

void TestAnnotationLayer::testSetSelectedIndex_InvalidOrHiddenClearsSelection()
{
    AnnotationLayer layer;
    layer.addItem(createTextBox(QPointF(20, 20), "visible"));

    auto hiddenText = createTextBox(QPointF(80, 80), "hidden");
    hiddenText->setVisible(false);
    layer.addItem(std::move(hiddenText));

    layer.setSelectedIndex(0);
    QCOMPARE(layer.selectedIndex(), 0);
    QVERIFY(layer.selectedItem() != nullptr);

    layer.setSelectedIndex(1);
    QCOMPARE(layer.selectedIndex(), -1);
    QVERIFY(layer.selectedItem() == nullptr);

    layer.setSelectedIndex(999);
    QCOMPARE(layer.selectedIndex(), -1);
    QVERIFY(layer.selectedItem() == nullptr);
}

void TestAnnotationLayer::testTranslateAll_AlsoTranslatesRedoStackItems()
{
    AnnotationLayer layer;

    auto polyline = createPolyline(30);
    const QVector<QPoint> originalPoints = polyline->points();
    layer.addItem(std::move(polyline));

    layer.undo();
    QVERIFY(layer.canRedo());

    layer.translateAll(QPointF(7.0, 11.0));
    layer.redo();

    auto* restored = dynamic_cast<PolylineAnnotation*>(layer.itemAt(0));
    QVERIFY(restored != nullptr);
    const QVector<QPoint> translatedPoints = restored->points();
    QCOMPARE(translatedPoints.size(), originalPoints.size());
    QCOMPARE(translatedPoints[0], originalPoints[0] + QPoint(7, 11));
    QCOMPARE(translatedPoints[1], originalPoints[1] + QPoint(7, 11));
}

void TestAnnotationLayer::testTranslateAll_TranslatesErasedItemsGroupContents()
{
    AnnotationLayer layer;

    QVector<QPoint> firstPoints = {
        QPoint(20, 20),
        QPoint(120, 20)
    };
    QVector<QPoint> secondPoints = {
        QPoint(20, 60),
        QPoint(120, 60)
    };

    layer.addItem(std::make_unique<PolylineAnnotation>(
        firstPoints, QColor(Qt::red), 3, LineEndStyle::None, LineStyle::Solid));
    layer.addItem(std::make_unique<PolylineAnnotation>(
        secondPoints, QColor(Qt::blue), 3, LineEndStyle::None, LineStyle::Solid));

    layer.setSelectedIndex(0);
    QVERIFY(layer.removeSelectedItem());

    layer.translateAll(QPointF(9.0, 5.0));
    layer.undo();  // Restores erased item from ErasedItemsGroup

    auto* restored = dynamic_cast<PolylineAnnotation*>(layer.itemAt(0));
    QVERIFY(restored != nullptr);

    const QVector<QPoint> restoredPoints = restored->points();
    QCOMPARE(restoredPoints.size(), firstPoints.size());
    QCOMPARE(restoredPoints[0], firstPoints[0] + QPoint(9, 5));
    QCOMPARE(restoredPoints[1], firstPoints[1] + QPoint(9, 5));
}

void TestAnnotationLayer::testRedo_ErasedItemsGroup_ReappliesDeletion()
{
    AnnotationLayer layer;

    auto first = createPolyline(20);
    auto second = createPolyline(60);
    const QVector<QPoint> secondPoints = second->points();

    layer.addItem(std::move(first));
    layer.addItem(std::move(second));

    layer.setSelectedIndex(0);
    QVERIFY(layer.removeSelectedItem());
    QVERIFY(dynamic_cast<ErasedItemsGroup*>(layer.itemAt(1)) != nullptr);

    layer.undo();
    QVERIFY(layer.canRedo());
    QCOMPARE(layer.itemCount(), static_cast<size_t>(2));

    layer.redo();

    QCOMPARE(layer.itemCount(), static_cast<size_t>(2));
    QVERIFY(!layer.canRedo());
    auto* remaining = dynamic_cast<PolylineAnnotation*>(layer.itemAt(0));
    QVERIFY(remaining != nullptr);
    QCOMPARE(remaining->points(), secondPoints);
    QVERIFY(dynamic_cast<ErasedItemsGroup*>(layer.itemAt(1)) != nullptr);
}

QTEST_MAIN(TestAnnotationLayer)
#include "tst_AnnotationLayer.moc"
