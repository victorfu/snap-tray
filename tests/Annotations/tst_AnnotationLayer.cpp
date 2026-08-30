#include <QtTest/QtTest>
#include <QImage>
#include <QPainter>
#include <QFont>

#include "annotations/AnnotationLayer.h"
#include "annotations/MarkerStroke.h"
#include "annotations/PencilStroke.h"
#include "annotations/PolylineAnnotation.h"
#include "annotations/TextBoxAnnotation.h"
#include "annotations/EmojiStickerAnnotation.h"
#include "annotations/ShapeAnnotation.h"

namespace {

struct CountingAnnotationState {
    int drawCalls = 0;
    int boundingRectCalls = 0;
};

class CountingAnnotation final : public AnnotationItem
{
public:
    CountingAnnotation(const QRect& bounds, std::shared_ptr<CountingAnnotationState> state)
        : m_bounds(bounds)
        , m_state(std::move(state))
    {
    }

    void draw(QPainter&) const override { ++m_state->drawCalls; }
    QRect boundingRect() const override
    {
        ++m_state->boundingRectCalls;
        return m_bounds;
    }
    std::unique_ptr<AnnotationItem> clone() const override
    {
        return std::make_unique<CountingAnnotation>(m_bounds, m_state);
    }
    void setBounds(const QRect& bounds) { m_bounds = bounds; }

private:
    QRect m_bounds;
    std::shared_ptr<CountingAnnotationState> m_state;
};

class CountingPencilStroke final : public PencilStroke
{
public:
    CountingPencilStroke(const QVector<QPointF>& points,
                         std::shared_ptr<CountingAnnotationState> state)
        : PencilStroke(points, Qt::red, 3, LineStyle::Solid)
        , m_state(std::move(state))
    {
    }

    void draw(QPainter&) const override { ++m_state->drawCalls; }

private:
    std::shared_ptr<CountingAnnotationState> m_state;
};

class SizedAnnotation final : public AnnotationItem
{
public:
    explicit SizedAnnotation(std::size_t retainedBytes)
        : m_retainedBytes(retainedBytes)
    {
    }

    void draw(QPainter&) const override {}
    QRect boundingRect() const override { return QRect(0, 0, 1, 1); }
    std::unique_ptr<AnnotationItem> clone() const override
    {
        return std::make_unique<SizedAnnotation>(m_retainedBytes);
    }
    std::size_t estimatedRetainedBytes() const override { return m_retainedBytes; }
    void setRetainedBytes(std::size_t bytes) { m_retainedBytes = bytes; }

private:
    std::size_t m_retainedBytes;
};

} // namespace

class TestAnnotationLayer : public QObject
{
    Q_OBJECT

private slots:
    void testAddItem_PreservesAnnotationsBeyondFifty();
    void testDraw_CullsPencilStrokesOutsidePainterClip();
    void testDrawCached_AppendsWithoutRebuildingExistingItems();
    void testContentBoundingRect_ReflectsInPlaceMutation();
    void testDrawWithDirtyRegion_ExcludesDraggedItemFromFullCache();
    void testDrawCached_RebuildsAfterExcludeCache();
    void testDrawCached_RebuildsAfterDraggedItemMoved();
    void testDrawCached_AppliesViewportOrigin();
    void testHitTestText_IgnoresHiddenItems();
    void testHitTestEmojiSticker_IgnoresHiddenItems();
    void testHitTestEmojiSticker_ReturnsTopMostVisible();
    void testHitTestShape_IgnoresHiddenItems();
    void testHitTestShape_ReturnsTopMostVisible();
    void testSetSelectedIndex_InvalidOrHiddenClearsSelection();
    void testTranslateAll_AlsoTranslatesRedoStackItems();
    void testTranslateAll_TranslatesRemoveCommandContents();
    void testRedo_RemoveCommand_ReappliesDeletion();
    void testRemoveItemsIntersecting_AdjacentRemovals_TrackOriginalIndices();
    void testRemoveItemsIntersecting_MarkerRemovals_TrackOriginalIndices();
    void testRedo_RemoveCommand_AdjacentRemovals_PreservesOrder();
    void testRedo_RemoveCommand_UsesStableIds();
    void testHistoryCountLimit_CommitsOldestVisibleItem();
    void testHistoryByteLimit_PreservesLatestOversizeCommand();
    void testHistoryByteLimit_PreservesNearestUndoAndRedo();
    void testHistoryBranch_DropsRedoOwnedBytes();
    void testHistoryStateToken_TracksUndoRedoAndBranches();
    void testRemoveOutsideEraseTransaction_IsNoOp();
    void testDeleteDuringEraseTransaction_IsRejected();
    void testAddDuringEraseTransaction_DefersUntilCommit();
    void testAddDuringEraseTransaction_DefersUntilEmptyCommit();
    void testAddDuringEraseTransaction_DefersUntilCancel();
    void testDeferredAdds_FlushAsSingleBatch();
    void testDeferredAddAfterCancel_ClearsRedoBranch();
    void testClearDuringEraseTransaction_DiscardsDeferredItems();
    void testOwnedVisitor_RefreshesHistoryBytes();

private:
    static bool hasVisiblePixel(const QImage& image, const QRect& probe);
    static std::unique_ptr<PolylineAnnotation> createPolyline(int y);
    static std::unique_ptr<PencilStroke> createPencil(int y);
    static std::unique_ptr<MarkerStroke> createMarker(int y);
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

std::unique_ptr<PencilStroke> TestAnnotationLayer::createPencil(int y)
{
    QVector<QPointF> points = {
        QPointF(20.0, static_cast<qreal>(y)),
        QPointF(140.0, static_cast<qreal>(y))
    };
    return std::make_unique<PencilStroke>(points, QColor(Qt::red), 2, LineStyle::Solid);
}

std::unique_ptr<MarkerStroke> TestAnnotationLayer::createMarker(int y)
{
    QVector<QPointF> points = {
        QPointF(20.0, static_cast<qreal>(y)),
        QPointF(140.0, static_cast<qreal>(y))
    };
    return std::make_unique<MarkerStroke>(points, QColor(Qt::yellow), 20);
}

std::unique_ptr<TextBoxAnnotation> TestAnnotationLayer::createTextBox(const QPointF& pos,
                                                                      const QString& text)
{
    QFont font;
    font.setPointSize(16);
    font.setBold(true);
    return std::make_unique<TextBoxAnnotation>(pos, text, font, QColor(Qt::red));
}

void TestAnnotationLayer::testAddItem_PreservesAnnotationsBeyondFifty()
{
    AnnotationLayer layer;

    for (int i = 0; i < 52; ++i) {
        layer.addItem(createPencil(i));
    }

    QCOMPARE(layer.itemCount(), static_cast<size_t>(52));

    auto* firstStroke = dynamic_cast<PencilStroke*>(layer.itemAt(0));
    auto* secondStroke = dynamic_cast<PencilStroke*>(layer.itemAt(1));
    auto* lastStroke = dynamic_cast<PencilStroke*>(layer.itemAt(51));
    QVERIFY(firstStroke != nullptr);
    QVERIFY(secondStroke != nullptr);
    QVERIFY(lastStroke != nullptr);
    QCOMPARE(firstStroke->points().first(), QPointF(20.0, 0.0));
    QCOMPARE(secondStroke->points().first(), QPointF(20.0, 1.0));
    QCOMPARE(lastStroke->points().first(), QPointF(20.0, 51.0));

    layer.undo();
    layer.undo();
    QCOMPARE(layer.itemCount(), static_cast<size_t>(50));
    QCOMPARE(layer.itemAt(0), firstStroke);

    layer.redo();
    layer.redo();
    QCOMPARE(layer.itemCount(), static_cast<size_t>(52));
    QCOMPARE(layer.itemAt(0), firstStroke);
}

void TestAnnotationLayer::testDraw_CullsPencilStrokesOutsidePainterClip()
{
    AnnotationLayer layer;
    auto inside = std::make_shared<CountingAnnotationState>();
    auto outside = std::make_shared<CountingAnnotationState>();
    layer.addItem(std::make_unique<CountingPencilStroke>(
        QVector<QPointF>{QPointF(10, 20), QPointF(30, 20)}, inside));
    layer.addItem(std::make_unique<CountingPencilStroke>(
        QVector<QPointF>{QPointF(200, 210), QPointF(220, 210)}, outside));

    QImage image(260, 260, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setClipRect(QRect(0, 0, 80, 80));
    layer.draw(painter);
    painter.end();

    QCOMPARE(inside->drawCalls, 1);
    QCOMPARE(outside->drawCalls, 0);
}

void TestAnnotationLayer::testDrawCached_AppendsWithoutRebuildingExistingItems()
{
    AnnotationLayer layer;
    auto first = std::make_shared<CountingAnnotationState>();
    layer.addItem(std::make_unique<CountingAnnotation>(QRect(10, 10, 20, 20), first));

    QImage image(120, 120, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    layer.drawCached(painter, image.size(), 1.0);
    QCOMPARE(first->drawCalls, 1);

    layer.addItem(createPencil(60));
    QCOMPARE(first->drawCalls, 1);

    layer.drawCached(painter, image.size(), 1.0);
    QCOMPARE(first->drawCalls, 1);
}

void TestAnnotationLayer::testContentBoundingRect_ReflectsInPlaceMutation()
{
    AnnotationLayer layer;
    auto first = std::make_shared<CountingAnnotationState>();
    auto second = std::make_shared<CountingAnnotationState>();
    layer.addItem(std::make_unique<CountingAnnotation>(QRect(10, 10, 20, 20), first));
    layer.addItem(std::make_unique<CountingAnnotation>(QRect(80, 70, 30, 40), second));

    QCOMPARE(layer.contentBoundingRect(), QRect(10, 10, 100, 100));
    auto* mutableFirst = dynamic_cast<CountingAnnotation*>(layer.itemAt(0));
    QVERIFY(mutableFirst != nullptr);
    mutableFirst->setBounds(QRect(140, 130, 20, 20));

    QCOMPARE(layer.contentBoundingRect(), QRect(80, 70, 80, 80));
    QCOMPARE(first->boundingRectCalls, 2);
    QCOMPARE(second->boundingRectCalls, 2);
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

void TestAnnotationLayer::testDrawCached_RebuildsAfterDraggedItemMoved()
{
    const QSize canvasSize(180, 100);

    AnnotationLayer layer;
    layer.addItem(createPolyline(20));
    layer.setSelectedIndex(0);

    QImage initialFrame(canvasSize, QImage::Format_ARGB32_Premultiplied);
    initialFrame.fill(Qt::transparent);
    {
        QPainter painter(&initialFrame);
        layer.drawCached(painter, canvasSize, 1.0);
    }

    auto* selectedPolyline = dynamic_cast<PolylineAnnotation*>(layer.itemAt(0));
    QVERIFY(selectedPolyline != nullptr);
    selectedPolyline->moveBy(QPoint(0, 30));

    QImage dragFrame(canvasSize, QImage::Format_ARGB32_Premultiplied);
    dragFrame.fill(Qt::transparent);
    {
        QPainter painter(&dragFrame);
        layer.drawWithDirtyRegion(painter, canvasSize, 1.0, 0);
    }

    QImage cachedFrame(canvasSize, QImage::Format_ARGB32_Premultiplied);
    cachedFrame.fill(Qt::transparent);
    {
        QPainter painter(&cachedFrame);
        layer.drawCached(painter, canvasSize, 1.0);
    }

    QVERIFY(!hasVisiblePixel(cachedFrame, QRect(70, 12, 24, 16)));
    QVERIFY(hasVisiblePixel(cachedFrame, QRect(70, 42, 24, 16)));
}

void TestAnnotationLayer::testDrawCached_AppliesViewportOrigin()
{
    const QSize canvasSize(180, 100);

    AnnotationLayer layer;
    QVector<QPoint> points = {
        QPoint(220, 40),
        QPoint(340, 40)
    };
    layer.addItem(std::make_unique<PolylineAnnotation>(
        points, QColor(Qt::red), 4, LineEndStyle::None, LineStyle::Solid));

    QImage cachedFrame(canvasSize, QImage::Format_ARGB32_Premultiplied);
    cachedFrame.fill(Qt::transparent);
    {
        QPainter painter(&cachedFrame);
        layer.drawCached(painter, canvasSize, 1.0, QPoint(200, 0));
    }

    QVERIFY(hasVisiblePixel(cachedFrame, QRect(70, 32, 24, 16)));
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

void TestAnnotationLayer::testHitTestShape_IgnoresHiddenItems()
{
    AnnotationLayer layer;

    auto hiddenShape = std::make_unique<ShapeAnnotation>(
        QRect(40, 40, 120, 80), ShapeType::Rectangle, Qt::red, 3);
    hiddenShape->setVisible(false);
    layer.addItem(std::move(hiddenShape));

    layer.addItem(std::make_unique<ShapeAnnotation>(
        QRect(40, 40, 120, 80), ShapeType::Rectangle, Qt::red, 3));

    const int hitIndex = layer.hitTestShape(QPoint(100, 80));
    QCOMPARE(hitIndex, 1);
}

void TestAnnotationLayer::testHitTestShape_ReturnsTopMostVisible()
{
    AnnotationLayer layer;

    layer.addItem(std::make_unique<ShapeAnnotation>(
        QRect(40, 40, 120, 80), ShapeType::Rectangle, Qt::red, 3));
    layer.addItem(std::make_unique<ShapeAnnotation>(
        QRect(40, 40, 120, 80), ShapeType::Rectangle, Qt::blue, 3));

    const int hitIndex = layer.hitTestShape(QPoint(100, 80));
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

void TestAnnotationLayer::testTranslateAll_TranslatesRemoveCommandContents()
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
    layer.undo();

    auto* restored = dynamic_cast<PolylineAnnotation*>(layer.itemAt(0));
    QVERIFY(restored != nullptr);

    const QVector<QPoint> restoredPoints = restored->points();
    QCOMPARE(restoredPoints.size(), firstPoints.size());
    QCOMPARE(restoredPoints[0], firstPoints[0] + QPoint(9, 5));
    QCOMPARE(restoredPoints[1], firstPoints[1] + QPoint(9, 5));
}

void TestAnnotationLayer::testRedo_RemoveCommand_ReappliesDeletion()
{
    AnnotationLayer layer;

    auto first = createPolyline(20);
    auto second = createPolyline(60);
    const QVector<QPoint> secondPoints = second->points();

    layer.addItem(std::move(first));
    layer.addItem(std::move(second));

    layer.setSelectedIndex(0);
    QVERIFY(layer.removeSelectedItem());
    QCOMPARE(layer.itemCount(), static_cast<size_t>(1));
    QVERIFY(layer.canUndo());

    layer.undo();
    QVERIFY(layer.canRedo());
    QCOMPARE(layer.itemCount(), static_cast<size_t>(2));

    layer.redo();

    QCOMPARE(layer.itemCount(), static_cast<size_t>(1));
    QVERIFY(!layer.canRedo());
    auto* remaining = dynamic_cast<PolylineAnnotation*>(layer.itemAt(0));
    QVERIFY(remaining != nullptr);
    QCOMPARE(remaining->points(), secondPoints);
}

void TestAnnotationLayer::testRemoveItemsIntersecting_AdjacentRemovals_TrackOriginalIndices()
{
    AnnotationLayer layer;
    layer.addItem(createPencil(20));
    layer.addItem(createPencil(40));
    layer.addItem(createPencil(60));
    layer.addItem(createPencil(80));

    layer.beginEraseTransaction();
    auto removed = layer.removeItemsIntersecting(QPoint(80, 50), 24);

    QCOMPARE(removed.size(), static_cast<size_t>(2));
    QCOMPARE(removed[0].originalIndex, static_cast<size_t>(1));
    QCOMPARE(removed[1].originalIndex, static_cast<size_t>(2));
    QCOMPARE(layer.itemCount(), static_cast<size_t>(2));

    auto* firstRemaining = dynamic_cast<PencilStroke*>(layer.itemAt(0));
    auto* secondRemaining = dynamic_cast<PencilStroke*>(layer.itemAt(1));
    QVERIFY(firstRemaining != nullptr);
    QVERIFY(secondRemaining != nullptr);
    QVERIFY(firstRemaining->intersectsCircle(QPoint(80, 20), 1));
    QVERIFY(secondRemaining->intersectsCircle(QPoint(80, 80), 1));
    QVERIFY(layer.commitEraseTransaction(std::move(removed)));
}

void TestAnnotationLayer::testRemoveItemsIntersecting_MarkerRemovals_TrackOriginalIndices()
{
    AnnotationLayer layer;
    layer.addItem(createMarker(20));
    layer.addItem(createMarker(40));
    layer.addItem(createMarker(60));
    layer.addItem(createMarker(80));

    layer.beginEraseTransaction();
    auto removed = layer.removeItemsIntersecting(QPoint(80, 50), 24);

    QCOMPARE(removed.size(), static_cast<size_t>(2));
    QCOMPARE(removed[0].originalIndex, static_cast<size_t>(1));
    QCOMPARE(removed[1].originalIndex, static_cast<size_t>(2));
    QCOMPARE(layer.itemCount(), static_cast<size_t>(2));

    auto* firstRemaining = dynamic_cast<MarkerStroke*>(layer.itemAt(0));
    auto* secondRemaining = dynamic_cast<MarkerStroke*>(layer.itemAt(1));
    QVERIFY(firstRemaining != nullptr);
    QVERIFY(secondRemaining != nullptr);
    QVERIFY(firstRemaining->intersectsCircle(QPoint(80, 20), 1));
    QVERIFY(secondRemaining->intersectsCircle(QPoint(80, 80), 1));
    QVERIFY(layer.commitEraseTransaction(std::move(removed)));
}

void TestAnnotationLayer::testRedo_RemoveCommand_AdjacentRemovals_PreservesOrder()
{
    AnnotationLayer layer;
    layer.addItem(createPencil(20));
    layer.addItem(createPencil(40));
    layer.addItem(createPencil(60));
    layer.addItem(createPencil(80));

    layer.beginEraseTransaction();
    auto removed = layer.removeItemsIntersecting(QPoint(80, 50), 24);
    QCOMPARE(removed.size(), static_cast<size_t>(2));
    QVERIFY(layer.commitEraseTransaction(std::move(removed)));

    QCOMPARE(layer.itemCount(), static_cast<size_t>(2));

    layer.undo();

    QVERIFY(layer.canRedo());
    QCOMPARE(layer.itemCount(), static_cast<size_t>(4));
    auto* restored0 = dynamic_cast<PencilStroke*>(layer.itemAt(0));
    auto* restored1 = dynamic_cast<PencilStroke*>(layer.itemAt(1));
    auto* restored2 = dynamic_cast<PencilStroke*>(layer.itemAt(2));
    auto* restored3 = dynamic_cast<PencilStroke*>(layer.itemAt(3));
    QVERIFY(restored0 != nullptr);
    QVERIFY(restored1 != nullptr);
    QVERIFY(restored2 != nullptr);
    QVERIFY(restored3 != nullptr);
    QVERIFY(restored0->intersectsCircle(QPoint(80, 20), 1));
    QVERIFY(restored1->intersectsCircle(QPoint(80, 40), 1));
    QVERIFY(restored2->intersectsCircle(QPoint(80, 60), 1));
    QVERIFY(restored3->intersectsCircle(QPoint(80, 80), 1));

    layer.redo();

    QCOMPARE(layer.itemCount(), static_cast<size_t>(2));
    QVERIFY(!layer.canRedo());

    auto* firstRemaining = dynamic_cast<PencilStroke*>(layer.itemAt(0));
    auto* secondRemaining = dynamic_cast<PencilStroke*>(layer.itemAt(1));
    QVERIFY(firstRemaining != nullptr);
    QVERIFY(secondRemaining != nullptr);
    QVERIFY(firstRemaining->intersectsCircle(QPoint(80, 20), 1));
    QVERIFY(secondRemaining->intersectsCircle(QPoint(80, 80), 1));
}

void TestAnnotationLayer::testRedo_RemoveCommand_UsesStableIds()
{
    AnnotationLayer layer;
    layer.addItem(createPencil(20));
    layer.addItem(createPencil(40));
    layer.addItem(createPencil(60));
    layer.addItem(createPencil(80));

    AnnotationItem* erasedFirst = layer.itemAt(1);
    AnnotationItem* erasedSecond = layer.itemAt(2);

    layer.beginEraseTransaction();
    auto removed = layer.removeItemsIntersecting(QPoint(80, 50), 24);
    QCOMPARE(removed.size(), static_cast<size_t>(2));
    QVERIFY(removed[0].id != 0);
    QVERIFY(removed[1].id != 0);
    QVERIFY(removed[0].id != removed[1].id);
    QVERIFY(layer.commitEraseTransaction(std::move(removed)));

    layer.undo();
    QCOMPARE(layer.itemCount(), static_cast<size_t>(4));
    QVERIFY(layer.canRedo());
    QCOMPARE(layer.itemAt(1), erasedFirst);
    QCOMPARE(layer.itemAt(2), erasedSecond);

    layer.redo();

    QCOMPARE(layer.itemCount(), static_cast<size_t>(2));
    auto* firstRemaining = dynamic_cast<PencilStroke*>(layer.itemAt(0));
    auto* secondRemaining = dynamic_cast<PencilStroke*>(layer.itemAt(1));
    QVERIFY(firstRemaining != nullptr);
    QVERIFY(secondRemaining != nullptr);
    QVERIFY(firstRemaining->intersectsCircle(QPoint(80, 20), 1));
    QVERIFY(secondRemaining->intersectsCircle(QPoint(80, 80), 1));
}

void TestAnnotationLayer::testHistoryCountLimit_CommitsOldestVisibleItem()
{
    AnnotationLayer layer;
    layer.addItem(createPencil(0));
    AnnotationItem* committedFirst = layer.itemAt(0);

    for (int i = 1; i <= 100; ++i) {
        layer.addItem(createPencil(i));
    }

    QCOMPARE(layer.itemCount(), static_cast<size_t>(101));
    QCOMPARE(layer.historyCommandCount(), AnnotationLayer::kMaxHistoryCommands);

    int undoCount = 0;
    while (layer.canUndo()) {
        layer.undo();
        ++undoCount;
    }

    QCOMPARE(undoCount, 100);
    QCOMPARE(layer.itemCount(), static_cast<size_t>(1));
    QCOMPARE(layer.itemAt(0), committedFirst);
}

void TestAnnotationLayer::testHistoryByteLimit_PreservesLatestOversizeCommand()
{
    AnnotationLayer layer;
    constexpr std::size_t oversize = AnnotationLayer::kMaxHistoryOwnedBytes + 1024;

    layer.addItem(std::make_unique<SizedAnnotation>(1024));
    AnnotationItem* committedBaseline = layer.itemAt(0);
    layer.addItem(std::make_unique<SizedAnnotation>(oversize));
    AnnotationItem* item = layer.itemAt(1);
    layer.undo();

    QCOMPARE(layer.itemCount(), static_cast<size_t>(1));
    QCOMPARE(layer.itemAt(0), committedBaseline);
    QCOMPARE(layer.historyCommandCount(), static_cast<size_t>(1));
    QVERIFY(layer.historyOwnedBytes() > AnnotationLayer::kMaxHistoryOwnedBytes);
    QVERIFY(!layer.canUndo());
    QVERIFY(layer.canRedo());

    layer.redo();
    QCOMPARE(layer.itemCount(), static_cast<size_t>(2));
    QCOMPARE(layer.itemAt(1), item);
    QVERIFY(layer.canUndo());
}

void TestAnnotationLayer::testHistoryByteLimit_PreservesNearestUndoAndRedo()
{
    AnnotationLayer layer;
    constexpr std::size_t largePayload = 40u * 1024u * 1024u;

    layer.addItem(std::make_unique<SizedAnnotation>(1024));
    AnnotationItem* first = layer.itemAt(0);
    layer.addItem(std::make_unique<SizedAnnotation>(largePayload));
    AnnotationItem* second = layer.itemAt(1);
    layer.addItem(std::make_unique<SizedAnnotation>(largePayload));

    layer.undo();
    layer.undo();

    QCOMPARE(layer.itemCount(), static_cast<size_t>(1));
    QCOMPARE(layer.itemAt(0), first);
    QCOMPARE(layer.historyCommandCount(), static_cast<size_t>(2));
    QVERIFY(layer.historyOwnedBytes() <= AnnotationLayer::kMaxHistoryOwnedBytes);
    QVERIFY(layer.canUndo());
    QVERIFY(layer.canRedo());

    // The farthest redo command was evicted; the commands immediately on
    // either side of the cursor remain available.
    layer.redo();
    QCOMPARE(layer.itemCount(), static_cast<size_t>(2));
    QCOMPARE(layer.itemAt(1), second);
    QVERIFY(!layer.canRedo());

    layer.undo();
    layer.undo();
    QCOMPARE(layer.itemCount(), static_cast<size_t>(0));
}

void TestAnnotationLayer::testHistoryBranch_DropsRedoOwnedBytes()
{
    AnnotationLayer layer;
    constexpr std::size_t largePayload = 40u * 1024u * 1024u;

    layer.addItem(std::make_unique<SizedAnnotation>(largePayload));
    layer.undo();
    const std::size_t withRedoPayload = layer.historyOwnedBytes();
    QVERIFY(withRedoPayload >= largePayload);

    layer.addItem(std::make_unique<SizedAnnotation>(1024));

    QVERIFY(!layer.canRedo());
    QVERIFY(layer.historyOwnedBytes() < withRedoPayload);
    QVERIFY(layer.historyOwnedBytes() <= AnnotationLayer::kMaxHistoryOwnedBytes);
}

void TestAnnotationLayer::testHistoryStateToken_TracksUndoRedoAndBranches()
{
    AnnotationLayer layer;
    const auto initial = layer.historyStateToken();
    layer.addItem(createPencil(20));
    const auto first = layer.historyStateToken();
    layer.addItem(createPencil(40));
    const auto abandoned = layer.historyStateToken();

    QCOMPARE(layer.historyStateRelation(abandoned),
             AnnotationLayer::HistoryStateRelation::Current);
    QCOMPARE(layer.historyStateRelation(first),
             AnnotationLayer::HistoryStateRelation::UndoReachable);
    QCOMPARE(layer.historyStateRelation(initial),
             AnnotationLayer::HistoryStateRelation::UndoReachable);

    layer.undo();
    QCOMPARE(layer.historyStateToken(), first);
    QCOMPARE(layer.historyStateRelation(abandoned),
             AnnotationLayer::HistoryStateRelation::RedoReachable);

    layer.addItem(createPencil(60));
    QVERIFY(layer.historyStateToken() != abandoned);
    QCOMPARE(layer.historyStateRelation(abandoned),
             AnnotationLayer::HistoryStateRelation::Unreachable);
}

void TestAnnotationLayer::testRemoveOutsideEraseTransaction_IsNoOp()
{
    AnnotationLayer layer;
    layer.addItem(createPencil(20));
    AnnotationItem* original = layer.itemAt(0);
    const auto historyState = layer.historyStateToken();

    auto removed = layer.removeItemsIntersecting(QPoint(80, 20), 24);

    QVERIFY(removed.empty());
    QCOMPARE(layer.itemCount(), static_cast<size_t>(1));
    QCOMPARE(layer.itemAt(0), original);
    QCOMPARE(layer.historyStateToken(), historyState);
    QVERIFY(layer.canUndo());
}

void TestAnnotationLayer::testDeleteDuringEraseTransaction_IsRejected()
{
    AnnotationLayer layer;
    layer.addItem(createPencil(20));
    layer.setSelectedIndex(0);
    layer.beginEraseTransaction();

    QVERIFY(!layer.removeSelectedItem());
    QCOMPARE(layer.itemCount(), static_cast<size_t>(1));
    QVERIFY(layer.endEraseTransaction());
}

void TestAnnotationLayer::testAddDuringEraseTransaction_DefersUntilCommit()
{
    AnnotationLayer layer;
    layer.addItem(createPencil(20));
    AnnotationItem* original = layer.itemAt(0);

    layer.beginEraseTransaction();
    auto removed = layer.removeItemsIntersecting(QPoint(80, 20), 24);
    QCOMPARE(removed.size(), static_cast<size_t>(1));

    auto deferred = createPencil(60);
    AnnotationItem* deferredItem = deferred.get();
    layer.addItem(std::move(deferred));
    QCOMPARE(layer.itemCount(), static_cast<size_t>(0));

    QVERIFY(layer.commitEraseTransaction(std::move(removed)));
    QCOMPARE(layer.itemCount(), static_cast<size_t>(1));
    QCOMPARE(layer.itemAt(0), deferredItem);

    // The deferred Add follows the completed Remove in history order.
    layer.undo();
    QCOMPARE(layer.itemCount(), static_cast<size_t>(0));
    layer.undo();
    QCOMPARE(layer.itemCount(), static_cast<size_t>(1));
    QCOMPARE(layer.itemAt(0), original);
}

void TestAnnotationLayer::testAddDuringEraseTransaction_DefersUntilCancel()
{
    AnnotationLayer layer;
    layer.addItem(createPencil(20));
    AnnotationItem* original = layer.itemAt(0);

    layer.beginEraseTransaction();
    auto removed = layer.removeItemsIntersecting(QPoint(80, 20), 24);
    QCOMPARE(removed.size(), static_cast<size_t>(1));

    auto deferred = createPencil(60);
    AnnotationItem* deferredItem = deferred.get();
    layer.addItem(std::move(deferred));
    QVERIFY(layer.cancelEraseTransaction(std::move(removed)));

    QCOMPARE(layer.itemCount(), static_cast<size_t>(2));
    QCOMPARE(layer.itemAt(0), original);
    QCOMPARE(layer.itemAt(1), deferredItem);

    // Cancelling creates no Remove command; only the real asynchronous Add is
    // undoable after the original scene item.
    layer.undo();
    QCOMPARE(layer.itemCount(), static_cast<size_t>(1));
    QCOMPARE(layer.itemAt(0), original);
}

void TestAnnotationLayer::testAddDuringEraseTransaction_DefersUntilEmptyCommit()
{
    AnnotationLayer layer;
    layer.beginEraseTransaction();

    auto deferred = createPencil(60);
    AnnotationItem* deferredItem = deferred.get();
    layer.addItem(std::move(deferred));
    QCOMPARE(layer.itemCount(), static_cast<size_t>(0));
    layer.translateAll(QPointF(5.0, 7.0));

    QVERIFY(layer.commitEraseTransaction({}));
    QCOMPARE(layer.itemCount(), static_cast<size_t>(1));
    QCOMPARE(layer.itemAt(0), deferredItem);
    auto* deferredStroke = dynamic_cast<PencilStroke*>(layer.itemAt(0));
    QVERIFY(deferredStroke != nullptr);
    QCOMPARE(deferredStroke->points().first(), QPointF(25.0, 67.0));
    QVERIFY(layer.canUndo());
}

void TestAnnotationLayer::testDeferredAdds_FlushAsSingleBatch()
{
    AnnotationLayer layer;
    QSignalSpy changedSpy(&layer, &AnnotationLayer::changed);
    layer.beginEraseTransaction();

    auto first = createPencil(20);
    auto second = createPencil(40);
    AnnotationItem* firstItem = first.get();
    AnnotationItem* secondItem = second.get();
    layer.addItem(std::move(first));
    layer.addItem(std::move(second));
    QCOMPARE(changedSpy.count(), 0);

    QVERIFY(layer.commitEraseTransaction({}));
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(layer.itemCount(), static_cast<size_t>(2));
    QCOMPARE(layer.itemAt(0), firstItem);
    QCOMPARE(layer.itemAt(1), secondItem);

    layer.undo();
    QCOMPARE(layer.itemAt(0), firstItem);
    layer.undo();
    QVERIFY(layer.isEmpty());
}

void TestAnnotationLayer::testDeferredAddAfterCancel_ClearsRedoBranch()
{
    AnnotationLayer layer;
    layer.addItem(createPencil(20));
    layer.undo();
    QVERIFY(layer.canRedo());

    layer.beginEraseTransaction();
    auto deferred = createPencil(60);
    AnnotationItem* deferredItem = deferred.get();
    layer.addItem(std::move(deferred));
    QVERIFY(layer.cancelEraseTransaction({}));

    QCOMPARE(layer.itemCount(), static_cast<size_t>(1));
    QCOMPARE(layer.itemAt(0), deferredItem);
    QVERIFY(!layer.canRedo());
    QVERIFY(layer.canUndo());
}

void TestAnnotationLayer::testClearDuringEraseTransaction_DiscardsDeferredItems()
{
    AnnotationLayer layer;
    layer.addItem(createPencil(20));

    layer.beginEraseTransaction();
    auto removed = layer.removeItemsIntersecting(QPoint(80, 20), 24);
    QCOMPARE(removed.size(), static_cast<size_t>(1));
    layer.addItem(createPencil(60));

    layer.clear();

    QVERIFY(!layer.commitEraseTransaction(std::move(removed)));
    QVERIFY(layer.isEmpty());
    QVERIFY(!layer.canUndo());
    QVERIFY(!layer.canRedo());
}

void TestAnnotationLayer::testOwnedVisitor_RefreshesHistoryBytes()
{
    AnnotationLayer layer;
    layer.addItem(std::make_unique<SizedAnnotation>(1024));
    layer.undo();
    const std::size_t before = layer.historyOwnedBytes();

    layer.forEachOwnedItem([](AnnotationItem* item) {
        if (auto* sized = dynamic_cast<SizedAnnotation*>(item)) {
            sized->setRetainedBytes(8 * 1024 * 1024);
        }
    });
    const std::size_t grown = layer.historyOwnedBytes();
    QVERIFY(grown > before);

    layer.forEachOwnedItem([](AnnotationItem* item) {
        if (auto* sized = dynamic_cast<SizedAnnotation*>(item)) {
            sized->setRetainedBytes(2048);
        }
    });
    QVERIFY(layer.historyOwnedBytes() < grown);
    QVERIFY(layer.canRedo());
}

QTEST_MAIN(TestAnnotationLayer)
#include "tst_AnnotationLayer.moc"
