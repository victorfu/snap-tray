#include <QtTest>
#include <QElapsedTimer>
#include "annotation/AnnotationSnapEngine.h"
#include "annotation/AnnotationSnapController.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/TextBoxAnnotation.h"
#include "annotations/EmojiStickerAnnotation.h"
#include "region/ShapeAnnotationEditor.h"
#include "region/TextAnnotationEditor.h"
#include "tools/handlers/TextToolHandler.h"
#include "tools/ToolContext.h"
#include "settings/AnnotationSettingsManager.h"
#include "settings/Settings.h"
#include <algorithm>
using namespace AnnotationSnap;
class tst_AnnotationSnapEngine : public QObject {
    Q_OBJECT
private slots:
    void init() { AnnotationSettingsManager::instance().saveAlignmentAssist(true); }
    void alignment_data() {
        QTest::addColumn<int>("axis"); QTest::addColumn<int>("anchor");
        for (int a=0; a<2; ++a) for (int k=0;k<3;++k)
            QTest::newRow(qPrintable(QString("%1/%2").arg(a).arg(k))) << a << k;
    }
    void alignment() {
        QFETCH(int, axis); QFETCH(int, anchor);
        Engine e;
        const QRectF target(100,100,80,80);
        const qreal at = 100 + 40*anchor;
        QRectF raw(at-10*anchor+3,220,20,20);
        if (axis) raw = QRectF(220,at-10*anchor+3,20,20);
        e.setTargets({{7,target}});
        const auto r=e.query(raw,1);
        QVERIFY(r.constraints[axis]); QCOMPARE(r.constraints[axis]->anchor,anchor);
        QCOMPARE(r.constraints[axis]->correction,-3.0);
        QVERIFY(!e.query(raw,2).constraints[axis]);
    }
    void spacing_data() {
        QTest::addColumn<int>("axis"); QTest::addColumn<int>("placement");
        for(int a=0;a<2;++a) for(int p=-1;p<=1;++p)
            QTest::newRow(qPrintable(QString("%1/%2").arg(a).arg(p))) << a << p;
    }
    void spacing() {
        QFETCH(int, axis); QFETCH(int, placement);
        Engine e;
        QRectF a(100,100,20,20), b(160,100,30,20);
        const qreal x=placement<0 ? 50 : placement>0 ? 230 : 135;
        QRectF raw(x+2,100,10,20);
        if(axis) { a=QRectF(100,100,20,20); b=QRectF(100,160,20,30); raw=QRectF(100,x+2,20,10); }
        e.setTargets({{1,a},{2,b}});
        const auto r=e.query(raw,1);
        QVERIFY(r.constraints[axis]); QCOMPARE(r.constraints[axis]->kind,Kind::Spacing);
        QCOMPARE(r.constraints[axis]->correction,-2.0);
        QCOMPARE(r.constraints[axis]->guides.size(),std::size_t(2));
        QCOMPARE(r.constraints[axis]->guides[0].length(),r.constraints[axis]->guides[1].length());
    }
    void fractionalSpacingAndExclusions() {
        Engine e; e.setTargets({{1,{100,100,20,20}},{2,{161,100,30,20}}});
        auto r=e.query({137,100,10,20},1);
        QVERIFY(r.constraints[0]); QCOMPARE(r.correction.x(),-1.5);
        QVERIFY(!e.query({137,200,10,20},1).constraints[0]);
        QVERIFY(!e.query({137,100,10,20},1,{},QRectF(137,100,10,20)).constraints[0]);
        e.setTargets({{1,{100,100,70,20}},{2,{161,100,30,20}}});
        r=e.query({197,100,10,20},1);
        QVERIFY(!r.constraints[0]);
    }
    void stableAndHysteresis() {
        Engine e; std::vector<Target> t={{9,{100,50,20,20}},{3,{106,50,20,20}}};
        e.setTargets(t); auto r=e.query({103,100,20,20},1);
        QVERIFY(r.constraints[0]); QCOMPARE(r.constraints[0]->first,Id(3));
        std::reverse(t.begin(),t.end());e.setTargets(t);
        QCOMPARE(e.query({103,100,20,20},1).constraints[0]->first,Id(3));
        r=e.query({113,100,20,20},1,r.constraints);
        QVERIFY(r.constraints[0]);QCOMPARE(r.constraints[0]->first,Id(3));
        QVERIFY(!e.query({135,100,20,20},1,r.constraints).constraints[0]);
        e.setTargets({});QVERIFY(!e.query({113,100,20,20},1,r.constraints).constraints[0]);
    }
    void layoutBounds() {
        ShapeAnnotation shape({100,200,30,40},ShapeType::Rectangle,Qt::red,20);
        QCOMPARE(*AnnotationSnapController::layoutBounds(&shape),QRectF(100,200,30,40));
        shape.setScale(2,2);
        QCOMPARE(AnnotationSnapController::layoutBounds(&shape)->size(),QSizeF(60,80));
        shape.setRotation(1);QVERIFY(!AnnotationSnapController::layoutBounds(&shape));
        shape.setRotation(0);shape.setVisible(false);QVERIFY(!AnnotationSnapController::layoutBounds(&shape));
        TextBoxAnnotation text({10,20},"abc",QFont(),Qt::red);
        QCOMPARE(*AnnotationSnapController::layoutBounds(&text),QRectF(text.position(), text.box().size()));
        EmojiStickerAnnotation emoji({50,50},QString::fromUtf8("🙂"));
        QVERIFY(AnnotationSnapController::layoutBounds(&emoji));
        emoji.translate({0.25,0.5});QCOMPARE(emoji.precisePosition(),QPointF(50.25,50.5));
        auto cloned=emoji.clone();QCOMPARE(static_cast<EmojiStickerAnnotation*>(cloned.get())->precisePosition(),emoji.precisePosition());
    }
    void dragHistoryAndNoDrift() {
        AnnotationLayer layer;
        layer.addItem(std::make_unique<ShapeAnnotation>(QRect(100,100,20,20),ShapeType::Rectangle,Qt::red,2));
        layer.addItem(std::make_unique<ShapeAnnotation>(QRect(40,200,20,20),ShapeType::Rectangle,Qt::red,2));
        layer.setSelectedIndex(1);layer.snapController().setEnabled(true);
        ShapeAnnotationEditor editor;editor.setAnnotationLayer(&layer);
        const auto history=layer.historyCommandCount();const auto id=layer.selectedId();
        editor.startDragging({45,205});
        for(int i=0;i<20;++i) {
            editor.updateDragging({102,205});
            QCOMPARE(static_cast<ShapeAnnotation*>(layer.selectedItem())->rectF().x(),100.0);
            layer.snapController().setSuspended(true);
            QCOMPARE(static_cast<ShapeAnnotation*>(layer.selectedItem())->rectF().x(),97.0);
            editor.updateDragging({70,205});
            QCOMPARE(static_cast<ShapeAnnotation*>(layer.selectedItem())->rectF().x(),65.0);
        }
        editor.updateDragging({102,205});editor.finishDragging();
        QCOMPARE(layer.historyCommandCount(),history+1);QCOMPARE(layer.selectedId(),id);
        layer.undo();QCOMPARE(static_cast<ShapeAnnotation*>(layer.itemAt(1))->rectF().x(),40.0);
        layer.redo();QCOMPARE(static_cast<ShapeAnnotation*>(layer.itemAt(1))->rectF().x(),100.0);
        editor.startDragging({105,205});editor.updateDragging({130,205});editor.cancelInteraction();
        QCOMPARE(static_cast<ShapeAnnotation*>(layer.itemAt(1))->rectF().x(),100.0);
        editor.startDragging({105,205});editor.updateDragging({150,205});editor.updateDragging({105,205});editor.finishDragging();
        QCOMPARE(layer.historyCommandCount(),history+1);
        QVERIFY(layer.snapController().guideBounds().isEmpty());
    }
    void textReleaseUsesFinalPointAndModifiers() {
        AnnotationLayer layer;
        layer.addItem(std::make_unique<TextBoxAnnotation>(QPointF(100,100),"abc",QFont(),Qt::red));
        layer.addItem(std::make_unique<TextBoxAnnotation>(QPointF(40,200),"abc",QFont(),Qt::red));
        layer.setSelectedIndex(1);layer.snapController().setEnabled(true);
        TextAnnotationEditor editor;editor.setAnnotationLayer(&layer);
        ToolContext context;context.annotationLayer=&layer;context.textAnnotationEditor=&editor;
        TextToolHandler handler;
        editor.startDragging({45,205});editor.updateDragging({102,205});
        auto item=[&] { return static_cast<TextBoxAnnotation*>(layer.itemAt(1)); };
        QCOMPARE(item()->position(),QPointF(100,200));
        layer.snapController().setInputModifiers(Qt::AltModifier);
        QVERIFY(handler.handleInteractionRelease(&context,{103,205}));
        QCOMPARE(item()->position(),QPointF(98,200));
        layer.undo();QCOMPARE(item()->position(),QPointF(40,200));
        layer.redo();QCOMPARE(item()->position(),QPointF(98,200));
        editor.startTransformation({103,205},GizmoHandle::Rotation);
        QVERIFY(!layer.snapController().active());editor.cancelInteraction();
    }
    void disabledAndInvalidated() {
        AnnotationLayer layer;
        layer.addItem(std::make_unique<ShapeAnnotation>(QRect(100,100,20,20),ShapeType::Rectangle,Qt::red,2));
        layer.addItem(std::make_unique<ShapeAnnotation>(QRect(40,200,20,20),ShapeType::Rectangle,Qt::red,2));
        layer.setSelectedIndex(1);layer.snapController().setEnabled(true);
        auto& c=layer.snapController();c.begin(&layer,{40,200});
        QVERIFY(c.move({97,200},false));QVERIFY(!c.guideBounds().isEmpty());
        layer.itemAt(0)->setVisible(false);c.move({97,200},false);
        QCOMPARE(static_cast<ShapeAnnotation*>(layer.selectedItem())->rectF().x(),97.0);
        QVERIFY(c.guideBounds().isEmpty());
        layer.itemAt(0)->setVisible(true);
        AnnotationSettingsManager::instance().saveAlignmentAssist(false);c.move({98,200},false);
        QCOMPARE(static_cast<ShapeAnnotation*>(layer.selectedItem())->rectF().x(),98.0);
        layer.cancelGesture();QVERIFY(!c.active());
    }
    void overlayIsNotExportedAndSourceReplacementCancels() {
        AnnotationLayer layer;
        layer.addItem(std::make_unique<ShapeAnnotation>(QRect(100,100,20,20),ShapeType::Rectangle,Qt::red,2));
        layer.addItem(std::make_unique<ShapeAnnotation>(QRect(40,200,20,20),ShapeType::Rectangle,Qt::red,2));
        layer.setSelectedIndex(1); auto& c=layer.snapController();
        c.begin(&layer,{40,200}); QVERIFY(!c.active()); // ScreenCanvas default
        c.setEnabled(true);c.begin(&layer,{40,200});c.move({97,200},false);
        QImage scene(400,400,QImage::Format_ARGB32_Premultiplied);scene.fill(Qt::white);
        { QPainter p(&scene);layer.draw(p); }
        auto withGuides=scene.copy();{ QPainter p(&withGuides);c.draw(p); }
        QVERIFY(withGuides!=scene);
        c.end();auto withoutGuides=scene.copy();withoutGuides.fill(Qt::white);
        { QPainter p(&withoutGuides);layer.draw(p); } QCOMPARE(withoutGuides,scene);
        c.begin(&layer,{100,200});layer.clear();
        layer.addItem(std::make_unique<ShapeAnnotation>(QRect(10,10,20,20),ShapeType::Rectangle,Qt::red,2));
        layer.setSelectedIndex(0);QVERIFY(c.move({300,200},false));
        QCOMPARE(static_cast<ShapeAnnotation*>(layer.selectedItem())->rectF(),QRectF(10,10,20,20));
        QVERIFY(c.guideBounds().isEmpty());
    }
    void incompatibleAxesAndInterveningTargets() {
        Engine e;
        // X equal gaps are valid at y=109, but Y alignment to 112 destroys
        // the half-height row overlap. The exact X spacing must win.
        e.setTargets({{1,{100,100,20,20}},{2,{160,100,30,20}},{3,{260,112,20,20}}});
        auto result=e.query({135,109,10,20},1);
        QVERIFY(result.constraints[0]); QCOMPARE(result.constraints[0]->kind,Kind::Spacing);
        QVERIFY(!result.constraints[1]);
        // C sits in the continuation's gap, without overlapping M. A/B may
        // not pretend to be adjacent to M.
        e.setTargets({{1,{100,100,20,20}},{2,{160,100,30,20}},{3,{200,100,10,20}}});
        result=e.query({230,100,10,20},1);
        QVERIFY(!result.constraints[0]);
        // Invalid and far-away targets never contribute anchors.
        e.setTargets({{1,{100,2000,20,20}},{2,{100,0,-1,20}}});
        QVERIFY(!e.query({103,100,20,20},1).constraints[0]);
    }
    void fractionalRoundTripHasNoHistory() {
        AnnotationLayer layer;
        auto shape=std::make_unique<ShapeAnnotation>(QRect(1,1,20,20),ShapeType::Rectangle,Qt::red,2);
        shape->setRectF({40.1,200.3,20.7,20.2}); layer.addItem(std::move(shape));
        layer.setSelectedIndex(0); layer.snapController().setEnabled(true);
        auto& c=layer.snapController();const auto count=layer.historyCommandCount();
        c.begin(&layer,{42,202});c.move({91.7,202.6},false);c.move({42,202},false);c.end();
        layer.commitGesture(AnnotationLayer::ModifyGeometry);
        QCOMPARE(layer.historyCommandCount(),count);
        QCOMPARE(static_cast<ShapeAnnotation*>(layer.selectedItem())->rectF(),QRectF(40.1,200.3,20.7,20.2));
    }
    void performance() {
        for(int n : {100,500}) {
            std::vector<Target> targets;
            for(int i=0;i<n;++i) targets.push_back({Id(i+1),QRectF((i%25)*140,(i/25)*100,30,30)});
            QElapsedTimer timer;timer.start();Engine e;e.setTargets(targets);auto build=timer.nsecsElapsed();
            std::vector<qint64> samples;
            for(int i=0;i<1000;++i) { timer.restart();e.query({qreal(400+i%100),355,40,40},1);samples.push_back(timer.nsecsElapsed()); }
            std::sort(samples.begin(),samples.end());
            qint64 total=0;for(auto sample:samples)total+=sample;
            qInfo() << "targets" << n << "build ns" << build << "query p95 ns" << samples[950]
                    << "1000 queries ns" << total;
            QVERIFY(samples[950]<2000000);
            AnnotationLayer layer;
            for (const auto& target : targets)
                layer.addItem(std::make_unique<ShapeAnnotation>(target.bounds.toRect(),ShapeType::Rectangle,Qt::red,2));
            layer.addItem(std::make_unique<ShapeAnnotation>(QRect(400,355,40,40),ShapeType::Rectangle,Qt::red,2));
            layer.setSelectedIndex(n);layer.snapController().setEnabled(true);
            timer.restart();layer.snapController().begin(&layer,{400,355});const auto beginTime=timer.nsecsElapsed();
            timer.restart();
            for(int i=0;i<1000;++i)layer.snapController().move({qreal(400+i%100),355},false);
            qInfo() << "targets" << n << "controller begin ns" << beginTime << "1000 moves ns" << timer.nsecsElapsed();
            layer.cancelGesture();
        }
    }
};
QTEST_MAIN(tst_AnnotationSnapEngine)
#include "tst_AnnotationSnapEngine.moc"
