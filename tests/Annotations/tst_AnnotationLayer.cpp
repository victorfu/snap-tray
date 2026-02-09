#include <QtTest/QtTest>
#include <QImage>
#include <QPainter>
#include <QFont>

#include "annotations/AnnotationLayer.h"
#include "annotations/PolylineAnnotation.h"
#include "annotations/TextBoxAnnotation.h"

class TestAnnotationLayer : public QObject
{
    Q_OBJECT

private slots:
    void testDrawWithDirtyRegion_ExcludesDraggedItemFromFullCache();
    void testDrawCached_RebuildsAfterExcludeCache();
    void testHitTestText_IgnoresHiddenItems();
    void testSetSelectedIndex_InvalidOrHiddenClearsSelection();

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

QTEST_MAIN(TestAnnotationLayer)
#include "tst_AnnotationLayer.moc"
