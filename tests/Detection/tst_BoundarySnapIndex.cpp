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
