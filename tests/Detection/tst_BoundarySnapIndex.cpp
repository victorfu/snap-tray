#include <QtTest>
#include <QPainter>
#include "detection/BoundarySnapIndex.h"

class tst_BoundarySnapIndex : public QObject
{
    Q_OBJECT
private slots:
    void hardEdges_data() {
        QTest::addColumn<bool>("vertical");
        QTest::addColumn<int>("format");
        for (bool v : {false, true})
            for (auto f : {QImage::Format_RGB32, QImage::Format_RGBA8888,
                           QImage::Format_ARGB32_Premultiplied, QImage::Format_RGB888})
                QTest::newRow(qPrintable(QString("%1-%2").arg(v).arg(f))) << v << int(f);
    }
    void hardEdges() {
        QFETCH(bool, vertical); QFETCH(int, format);
        QImage image(100, 80, QImage::Format(format)); image.fill(Qt::black);
        { QPainter p(&image); p.fillRect(vertical ? QRect(50, 0, 50, 80) : QRect(0, 40, 100, 40), Qt::white); }
        BoundarySnapIndex index; index.setImage(image);
        auto axis = vertical ? BoundarySnapIndex::Axis::Vertical : BoundarySnapIndex::Axis::Horizontal;
        auto hit = index.evaluate(axis, vertical ? 50 : 40, 0, vertical ? 80 : 100, {});
        QVERIFY(hit); QCOMPARE(hit->supportFraction, 1.0);
        QCOMPARE(hit->axis, axis);
        QVERIFY(qAbs(hit->meanStrength - std::sqrt(3.0 * 255 * 255)) < 0.001);
        QVERIFY(!index.evaluate(axis, vertical ? 49 : 39, 0, 10, {}));
        // The index owns shared immutable storage; later writes detach.
        image.fill(Qt::black); QVERIFY(index.evaluate(axis, vertical ? 50 : 40, 0, 10, {}));
    }
    void supportAndSpan() {
        QImage image(100, 100, QImage::Format_RGB32); image.fill(Qt::black);
        image.setPixel(50, 0, qRgb(255,255,255));
        BoundarySnapIndex index; index.setImage(image);
        using Axis = BoundarySnapIndex::Axis;
        QVERIFY(!index.evaluate(Axis::Vertical, 50, 0, 100, {}));
        { QPainter p(&image); p.fillRect(50, 0, 50, 40, Qt::white); }
        index.setImage(image);
        QVERIFY(!index.evaluate(Axis::Vertical, 50, 0, 100, {}));
        QVERIFY(index.evaluate(Axis::Vertical, 50, 0, 40, {}));
        QVERIFY(!index.evaluate(Axis::Vertical, 50, 40, 100, {}));
    }
    void contrastAndTexture() {
        using Axis = BoundarySnapIndex::Axis;
        for (int contrast : {10, 16, 17, 30}) {
            QImage image(80, 80, QImage::Format_RGB32); image.fill(Qt::black);
            { QPainter p(&image); p.fillRect(40, 0, 40, 80, QColor(contrast,contrast,contrast)); }
            BoundarySnapIndex index; index.setImage(image);
            QCOMPARE(bool(index.evaluate(Axis::Vertical, 40, 0, 80, {})), contrast >= 17);
        }
        QImage texture(80, 80, QImage::Format_RGB32); texture.fill(Qt::black);
        for (int y=0; y<80; ++y) for (int x=0; x<80; ++x)
            texture.setPixel(x,y,qRgb(x*3,x*3,x*3));
        BoundarySnapIndex index; index.setImage(texture);
        QVERIFY(!index.evaluate(Axis::Vertical, 40, 0, 80, {})); // smooth shadow
        for (int y=0; y<80; y+=3) texture.setPixel(40,y,qRgb(255,255,255));
        index.setImage(texture);
        QVERIFY(!index.evaluate(Axis::Vertical, 40, 0, 80, {})); // sparse text-like texture
    }
    void antialiasedEdgesAndGrid() {
        using Axis = BoundarySnapIndex::Axis;
        // A three-column antialiased transition: each known boundary must
        // qualify with full support, rather than finding an unspecified edge.
        QImage image(100,100,QImage::Format_RGB32); image.fill(Qt::black);
        { QPainter p(&image);
          p.fillRect(48,0,52,100,QColor(32,32,32));
          p.fillRect(49,0,51,100,QColor(128,128,128));
          p.fillRect(50,0,50,100,Qt::white); }
        BoundarySnapIndex index; index.setImage(image);
        for (int boundary : {48,49,50}) {
            const auto hit=index.evaluate(Axis::Vertical,boundary,0,100,{});
            QVERIFY(hit); QCOMPARE(hit->supportFraction,1.0);
        }
        image.fill(Qt::white);
        { QPainter p(&image); p.setPen(Qt::black);
          for (int c=20;c<100;c+=20) { p.drawLine(c,0,c,99);p.drawLine(0,c,99,c); } }
        index.setImage(image);
        for (int boundary : {20,21,40,41,60,61,80,81}) {
            QVERIFY(index.evaluate(Axis::Vertical,boundary,0,100,{}));
            QVERIFY(index.evaluate(Axis::Horizontal,boundary,0,100,{}));
        }
        QVERIFY(!index.evaluate(Axis::Vertical,30,0,100,{}));
    }
    void meanDistanceIsNotMeanSquaredDistance() {
        QImage image(2,100,QImage::Format_RGB32); image.fill(Qt::black);
        // 60% weak edges and 40% stronger ones: mean sqrt = 26, while
        // sqrt(mean squared) > 28. Support clears the independent threshold.
        for (int y=0;y<100;++y) image.setPixel(1,y,qRgb(y<60 ? 10 : 50,0,0));
        BoundarySnapIndex index;index.setImage(image);
        BoundarySnapIndex::Options options;options.supportThreshold=10;
        QVERIFY(!index.evaluate(BoundarySnapIndex::Axis::Vertical,1,0,100,options));
        options.minimumMean=26;
        const auto hit=index.evaluate(BoundarySnapIndex::Axis::Vertical,1,0,100,options);
        QVERIFY(hit);QCOMPARE(hit->meanStrength,26.0);QCOMPARE(hit->supportFraction,1.0);
    }
    void supportThresholdBoundary() {
        QImage image(2,100,QImage::Format_RGB32);
        BoundarySnapIndex index;
        for (int count : {54,55,56}) {
            image.fill(Qt::black);
            for(int y=0;y<count;++y) image.setPixel(1,y,qRgb(255,255,255));
            index.setImage(image);
            QCOMPARE(bool(index.evaluate(BoundarySnapIndex::Axis::Vertical,1,0,100,{})),count>=55);
        }
    }
    void invalidAndAlpha() {
        using Axis = BoundarySnapIndex::Axis;
        BoundarySnapIndex index;
        QVERIFY(!index.evaluate(Axis::Vertical, 1, 0, 2, {}));
        QImage image(20,20,QImage::Format_ARGB32); image.fill(Qt::transparent);
        for (int y=0;y<20;++y) image.setPixel(10,y,qRgba(255,255,255,0));
        index.setImage(image);
        QVERIFY(!index.evaluate(Axis::Vertical, 10, 0, 20, {}));
        for (int b : {-1,0,20,21}) QVERIFY(!index.evaluate(Axis::Vertical,b,0,20,{}));
        QVERIFY(!index.evaluate(Axis::Vertical,10,-1,20,{}));
        QVERIFY(!index.evaluate(Axis::Vertical,10,0,21,{}));
        QVERIFY(!index.evaluate(Axis::Vertical,10,5,5,{}));
        index.setImage(QImage(1,20,QImage::Format_RGB32)); QVERIFY(index.size().isEmpty());
        // Large dimensions are rejected before allocating a conversion/index.
        QImage large(6400,6251,QImage::Format_Mono);
        index.setImage(large); QVERIFY(index.size().isEmpty());
    }
};
QTEST_GUILESS_MAIN(tst_BoundarySnapIndex)
#include "tst_BoundarySnapIndex.moc"
