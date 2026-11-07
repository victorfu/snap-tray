#include <QtTest>
#include "detection/TextLineSnapper.h"
#include "detection/TextLineGeometryCache.h"
#include <limits>

class TestTextLineSnapper : public QObject {
    Q_OBJECT
private slots:
    void platformGranularityAndDpr_data() {
        QTest::addColumn<qreal>("dpr");
        for (qreal dpr : {1.0,1.25,1.5,2.0}) QTest::newRow(qPrintable(QString::number(dpr))) << dpr;
    }
    void platformGranularityAndDpr() {
        QFETCH(qreal, dpr);
        const QRectF crop(50/dpr, 100/dpr, 400/dpr, 200/dpr);
        // macOS Vision line and Windows word fixtures after native normalization.
        auto line = TextLineSnapshot::fromOCR({{"hello world",{0.1,0.2,0.45,0.1},0.9f}},crop);
        auto words = TextLineSnapshot::fromOCR({{"hello",{0.1,0.2,0.2,0.1},-1},
                                               {"world",{0.32,0.2,0.23,0.1},-1}},crop);
        QCOMPARE(words->lines().size(), 1);
        QCOMPARE(words->lines().first().rect, line->lines().first().rect);
        const auto match = words->query(QPointF(150/dpr,150/dpr));
        QVERIFY(match.line);
        QCOMPARE(match.line->rect.center().y(),150/dpr);
        QCOMPARE(match.line->width,qRound(20/dpr*1.15));
    }
    void ambiguityColumnsAndInvalid() {
        const QRectF crop(0,0,1000,1000);
        auto columns = TextLineSnapshot::fromOCR({{"left",{.1,.1,.1,.02},-1},
                                                  {"right",{.4,.1,.1,.02},-1}},crop);
        QCOMPARE(columns->lines().size(),2);
        QVERIFY(!columns->query({300,110}).line);
        QVERIFY(columns->query({150,110}).line);
        QVERIFY(columns->query({50,110},150).line);
        QVERIFY(columns->query({250,110},150).line);
        QCOMPARE(columns->query({50,110},550).reason,TextLineSnapshot::Reason::Ambiguous);
        QVERIFY(!columns->query({50,110},102).line);
        auto close = TextLineSnapshot::fromOCR({{"a",{.1,.1,.2,.02},-1},
                                                {"b",{.1,.125,.2,.02},-1}},crop);
        QCOMPARE(close->query({150,122.5}).reason,TextLineSnapshot::Reason::Ambiguous);
        QVERIFY(close->query({150,110}).line);
        const qreal nan = std::numeric_limits<qreal>::quiet_NaN();
        auto invalid = TextLineSnapshot::fromOCR({{"x",{nan,.1,.2,.02},-1},
            {"x",{-.1,.1,.2,.02},-1},{"x",{.1,.1,.2,.02},.1f},
            {"x",{.1,.1,.2,0},-1}},crop);
        QVERIFY(invalid->lines().isEmpty());
        QCOMPARE(columns->query({nan,1}).reason,TextLineSnapshot::Reason::InvalidInput);
        auto duplicate = TextLineSnapshot::fromOCR({{"a",{.1,.1,.2,.02},-1},
                                                   {"a",{.1,.1,.2,.02},-1}},crop);
        QCOMPARE(duplicate->query({150,110}).reason,TextLineSnapshot::Reason::Ambiguous);
    }
    void cacheLifecycle() {
        QVector<OCRCallback> callbacks;
        int requests = 0;
        TextLineGeometryCache cache([&](const QPixmap&, const QStringList&, const OCRCallback& cb) {
            ++requests; callbacks.append(cb);
        });
        QPixmap image(400,200); image.fill(Qt::white);
        TextLineGeometryCache::Key key{image.cacheKey(),{50,20,200,100},image.size(),2,{"en-US"}};
        OCRResult result; result.success = true; result.blocks = {{"hello",{0,.2,.5,.2},-1}};
        cache.request(key,image); cache.request(key,image);
        QCOMPARE(requests,1);
        key.source++;
        cache.request(key,image);
        QVERIFY(!cache.snapshot());
        callbacks[0](result);
        QVERIFY(!cache.snapshot());
        QCOMPARE(requests,2);
        callbacks[1](result);
        QVERIFY(cache.snapshot());
        QCOMPARE(cache.snapshot()->lines().first().rect,QRectF(25,20,50,10));
        key.languages = {"zh-Hant","en-US"}; cache.request(key,image);
        QCOMPARE(requests,3);
        callbacks[2](OCRResult{});
        cache.request(key,image); QCOMPARE(requests,3); // failed key is not retried
        cache.clear(); cache.request(key,image); QCOMPARE(requests,4);
        cache.clear(); callbacks[3](result); QVERIFY(!cache.snapshot());
        OCRCallback afterDelete;
        auto* temporary = new TextLineGeometryCache([&](const QPixmap&,const QStringList&,const OCRCallback& cb){afterDelete=cb;});
        temporary->request(key,image); delete temporary; afterDelete(result);
    }
    void indexedQuery() {
        QVector<OCRTextBlock> blocks;
        for (int i=0;i<1000;++i) blocks.append({"line",{.1,i/1000.0,.5,.0005},-1});
        auto snapshot = TextLineSnapshot::fromOCR(blocks,{0,0,1000,100000});
        QCOMPARE(snapshot->lines().size(),1000);
        QBENCHMARK { QVERIFY(snapshot->query({200,50025}).line); }
    }
};
QTEST_MAIN(TestTextLineSnapper)
#include "tst_TextLineSnapper.moc"
