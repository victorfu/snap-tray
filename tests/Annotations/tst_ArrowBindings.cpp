#include <QtTest>
#include <QImage>
#include "annotations/AnnotationLayer.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/TextBoxAnnotation.h"
#include "settings/AnnotationSettingsManager.h"
#include "history/AnnotationSerializer.h"

namespace {
struct Scene {
    AnnotationLayer layer;
    ShapeAnnotation* a;
    ShapeAnnotation* b;
    ArrowAnnotation* arrow;
    Scene() {
        auto left = std::make_unique<ShapeAnnotation>(QRect(10,20,100,80), ShapeType::Rectangle, Qt::red, 2);
        auto right = std::make_unique<ShapeAnnotation>(QRect(250,20,100,80), ShapeType::Ellipse, Qt::blue, 2);
        auto line = std::make_unique<ArrowAnnotation>(QPoint(110,60), QPoint(250,60), Qt::green, 2);
        a = left.get(); b = right.get(); arrow = line.get();
        layer.addItem(std::move(left)); layer.addItem(std::move(right)); layer.addItem(std::move(line));
        arrow->setBinding(false, {a->persistentId(), QPointF(1,0.5)});
        arrow->setBinding(true, {b->persistentId(), QPointF(0,0.5)});
        arrow->setControlPoint(QPointF(180,30));
    }
};
struct Counter { int draws = 0; int bounds = 0; };
class Unrelated final : public AnnotationItem {
public:
    explicit Unrelated(Counter* count) : m_count(count) {}
    void draw(QPainter&) const override { ++m_count->draws; }
    QRect boundingRect() const override { ++m_count->bounds; return QRect(0,0,1,1); }
    std::unique_ptr<AnnotationItem> clone() const override { return std::make_unique<Unrelated>(m_count); }
    Counter* m_count;
};
}

class TestArrowBindings : public QObject {
    Q_OBJECT
private slots:
    void init() { AnnotationSettingsManager::instance().saveArrowBinding(true); }
    void cleanup() { AnnotationSettingsManager::instance().saveArrowBinding(false); }
    void twoTargetsAndCurve() {
        Scene s;
        s.a->moveBy(QPointF(20,30));
        QCOMPARE(s.arrow->start(), QPoint(130,90));
        QCOMPARE(s.arrow->end(), QPoint(250,60));
        QCOMPARE(s.arrow->controlPoint(), QPointF(190,45));
        s.b->setScale(2,2);
        QCOMPARE(s.arrow->end(), QPoint(200,60));
        QCOMPARE(s.arrow->controlPoint(), QPointF(165,45));
        s.a->setRotation(90);
        QCOMPARE(s.arrow->start(), QPoint(80,140));
        s.arrow->setBinding(true, {});
        s.b->moveBy(QPointF(25,25));
        QCOMPARE(s.arrow->end(), QPoint(200,60));
    }
    void coincidentEndpointsDoNotResetCurve() {
        Scene s;
        s.b->moveBy(QPointF(-140,0));
        QCOMPARE(s.arrow->start(),s.arrow->end());
        QCOMPARE(s.arrow->controlPoint(),QPointF(110,30));
        s.b->moveBy(QPointF(140,0));
        QCOMPARE(s.arrow->controlPoint(),QPointF(180,30));
        s.arrow->setBinding(true,{s.a->persistentId(),QPointF(0,0.5)});
        s.a->moveBy(QPointF(20,0));
        QCOMPARE(s.arrow->start(),QPoint(130,60));
        QCOMPARE(s.arrow->end(),QPoint(30,60));
        const auto id = s.a->persistentId();
        s.layer.setSelectedIndex(0);
        QVERIFY(s.layer.removeSelectedItem());
        QVERIFY(s.arrow->binding(false).targetId.isNull());
        QVERIFY(s.arrow->binding(true).targetId.isNull());
        s.layer.undo();
        QCOMPARE(s.arrow->binding(false).targetId,id);
        QCOMPARE(s.arrow->binding(true).targetId,id);
    }
    void textMirrorAndLayout() {
        AnnotationLayer layer;
        auto text = std::make_unique<TextBoxAnnotation>(QPointF(20,30), "Hello", QFont(), Qt::red);
        auto* target = text.get();
        layer.addItem(std::move(text));
        auto arrow = std::make_unique<ArrowAnnotation>(QPoint(0,0), QPoint(300,200), Qt::red, 2);
        auto* line = arrow.get();
        arrow->setBinding(false, {target->persistentId(), QPointF(1,0.5)});
        layer.addItem(std::move(arrow));
        target->setMirror(true,false);
        QCOMPARE(line->start(), target->mapLocalPointToTransformed(QPointF(target->box().width(),target->box().height()/2)).toPoint());
        target->setRotation(90);
        target->setScale(1.5);
        target->setWrapWidth(300);
        QCOMPARE(line->start(), target->mapLocalPointToTransformed(QPointF(target->box().width(),target->box().height()/2)).toPoint());
    }
    void dirtyBoundsEndWithGesture() {
        Scene s;
        QVERIFY(s.layer.beginGesture(s.layer.idAt(0)));
        s.a->moveBy(QPointF(20,30));
        QVERIFY(!s.layer.bindingDirtyBounds().isEmpty());
        QVERIFY(s.layer.commitGesture(AnnotationLayer::ModifyGeometry));
        QVERIFY(s.layer.bindingDirtyBounds().isEmpty());
        QVERIFY(s.layer.beginGesture(s.layer.idAt(0)));
        s.a->moveBy(QPointF(30,20));
        s.layer.cancelGesture();
        QVERIFY(s.layer.bindingDirtyBounds().isEmpty());
    }
    void gestureUndoCancelAndDetach() {
        Scene s;
        const auto count = s.layer.historyCommandCount();
        QVERIFY(s.layer.beginGesture(s.layer.idAt(0)));
        s.a->moveBy(QPointF(20,30));
        s.layer.cancelGesture();
        QCOMPARE(s.arrow->start(), QPoint(110,60));
        QCOMPARE(s.arrow->controlPoint(), QPointF(180,30));
        QCOMPARE(s.layer.historyCommandCount(), count);
        QVERIFY(s.layer.beginGesture(s.layer.idAt(0)));
        s.a->moveBy(QPointF(20,30));
        QVERIFY(s.layer.commitGesture(AnnotationLayer::ModifyGeometry));
        QCOMPARE(s.layer.historyCommandCount(), count+1);
        s.layer.undo(); QCOMPARE(s.arrow->start(), QPoint(110,60));
        s.layer.redo(); QCOMPARE(s.arrow->start(), QPoint(130,90));
        QVERIFY(s.layer.beginGesture(s.layer.idAt(2)));
        s.layer.editArrowEndpoint(*s.arrow, false, QPoint(500,500));
        QVERIFY(s.arrow->binding(false).targetId.isNull());
        QVERIFY(s.layer.commitGesture(AnnotationLayer::ModifyGeometry));
        s.layer.undo(); QCOMPARE(s.arrow->binding(false).targetId, s.a->persistentId());
        s.layer.redo(); QVERIFY(s.arrow->binding(false).targetId.isNull());
    }
    void deleteTargetUndoRedo() {
        Scene s;
        const QUuid id = s.a->persistentId();
        s.layer.setSelectedIndex(0);
        QVERIFY(s.layer.removeSelectedItem());
        QVERIFY(s.arrow->binding(false).targetId.isNull());
        QCOMPARE(s.arrow->start(), QPoint(110,60));
        s.layer.undo(); QCOMPARE(s.arrow->binding(false).targetId, id);
        s.layer.redo(); QVERIFY(s.arrow->binding(false).targetId.isNull());
        s.layer.undo();
        s.a->moveBy(QPointF(10,0));
        QCOMPARE(s.arrow->start(), QPoint(120,60));
    }
    void eraseCancelAndUndo() {
        Scene s;
        const QUuid id = s.a->persistentId();
        s.layer.beginEraseTransaction();
        auto removed = s.layer.removeItemsIntersecting(QPoint(15,25), 2);
        QCOMPARE(removed.size(), size_t(1));
        QVERIFY(s.arrow->binding(false).targetId.isNull());
        QVERIFY(s.layer.cancelEraseTransaction(std::move(removed)));
        QCOMPARE(s.arrow->binding(false).targetId,id);
        s.layer.beginEraseTransaction();
        removed = s.layer.removeItemsIntersecting(QPoint(15,25),2);
        QVERIFY(s.layer.commitEraseTransaction(std::move(removed)));
        s.layer.undo(); QCOMPARE(s.arrow->binding(false).targetId,id);
        s.layer.redo(); QVERIFY(s.arrow->binding(false).targetId.isNull());
    }
    void copyAndReorderKeepTargets() {
        Scene s;
        const auto id = s.a->persistentId();
        s.layer.addItem(s.a->clone());
        QVERIFY(s.layer.itemAt(3)->persistentId() != id);
        s.layer.addItem(s.arrow->clone());
        auto* copy = dynamic_cast<ArrowAnnotation*>(s.layer.itemAt(4));
        QVERIFY(copy->persistentId() != s.arrow->persistentId());
        QCOMPARE(copy->binding(false).targetId, id);
        s.layer.setSelectedIndex(0);
        QVERIFY(s.layer.moveSelectedItem(AnnotationLayer::ZOrderMove::ToFront));
        s.a->moveBy(QPointF(20,0));
        QCOMPARE(copy->start(), QPoint(130,60));
        QCOMPARE(s.arrow->start(), QPoint(130,60));
        s.layer.undo();
        QCOMPARE(copy->binding(false).targetId, id);
    }
    void pasteAfterDeletingOriginalDoesNotReuseIdentity() {
        Scene s;
        const auto original = s.a->persistentId();
        auto copy = s.a->clone();
        s.layer.setSelectedIndex(0);
        QVERIFY(s.layer.removeSelectedItem());
        s.layer.addItem(std::move(copy));
        QVERIFY(s.layer.itemAt(2)->persistentId() != original);
        s.layer.undo(); // pasted copy
        s.layer.undo(); // target deletion
        QCOMPARE(s.arrow->binding(false).targetId,original);
        s.a->moveBy(QPointF(20,0));
        QCOMPARE(s.arrow->start(),QPoint(130,60));
    }
    void disabledModeAndHitTolerance() {
        Scene s;
        for (qreal scale : {0.5, 1.0, 2.0}) {
            QVERIFY(!s.layer.bindingAt(QPointF(110+7/scale,60),scale).targetId.isNull());
            QVERIFY(s.layer.bindingAt(QPointF(110+9/scale,60),scale).targetId.isNull());
        }
        AnnotationSettingsManager::instance().saveArrowBinding(false);
        s.a->moveBy(QPointF(20,0)); // switching off doesn't break existing relationships
        QCOMPARE(s.arrow->start(),QPoint(130,60));
        s.layer.editArrowEndpoint(*s.arrow,false,QPoint(130,60));
        QVERIFY(s.arrow->binding(false).targetId.isNull());
    }
    void translateWholeSceneOnce() {
        Scene s;
        s.layer.translateAll(QPointF(20,30));
        QCOMPARE(s.arrow->start(), QPoint(130,90));
        QCOMPARE(s.arrow->end(), QPoint(270,90));
        QCOMPARE(s.arrow->controlPoint(), QPointF(200,60));
    }
    void unrelatedGeometryAndCachesStayUntouchedDuringDrag() {
        Scene s;
        Counter counter;
        for (int i=0;i<250;++i) s.layer.addItem(std::make_unique<Unrelated>(&counter));
        s.layer.setSelectedIndex(0);
        QVERIFY(s.layer.beginGesture(s.layer.idAt(0)));
        QImage image(500,300,QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
        QPainter painter(&image);
        s.layer.drawWithDirtyRegion(painter,image.size(),1,0);
        counter = {};
        for (int i=0;i<20;++i) {
            s.a->moveBy(QPointF(1,0));
            s.layer.drawWithDirtyRegion(painter,image.size(),1,0);
        }
        QCOMPARE(counter.draws,0);
        QCOMPARE(counter.bounds,0);
        QCOMPARE(s.arrow->start(),QPoint(130,60));
        s.layer.cancelGesture();
    }
    void renderedGeometryMatchesExport_data() {
        QTest::addColumn<qreal>("dpr");
        QTest::newRow("1") << qreal(1);
        QTest::newRow("1.5") << qreal(1.5);
        QTest::newRow("2") << qreal(2);
    }
    void renderedGeometryMatchesExport() {
        QFETCH(qreal,dpr);
        Scene s;
        s.a->moveBy(QPointF(20,30));
        auto render = [&](bool cached) {
            QImage image(QSize(500*dpr,300*dpr),QImage::Format_ARGB32_Premultiplied);
            image.setDevicePixelRatio(dpr); image.fill(Qt::transparent);
            QPainter p(&image); p.setRenderHint(QPainter::Antialiasing);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            if (cached) s.layer.drawCached(p,QSize(500,300),dpr); else s.layer.draw(p);
            return image;
        };
        QCOMPARE(render(true),render(false));
    }
    void legacyAndMissingTargetRemainFree() {
        Scene s;
        auto root = QJsonDocument::fromJson(SnapTray::serializeAnnotationLayer(s.layer)).object();
        auto items = root["items"].toArray();
        auto target = items[0].toObject(); target.remove("id"); items[0]=target;
        auto arrow = items[2].toObject();
        arrow["endBinding"] = QJsonObject{{"targetId",s.b->persistentId().toString()}, {"anchor",QJsonArray{2,0.5}}};
        items[2]=arrow; root["items"]=items;
        AnnotationLayer restored;
        QVERIFY(SnapTray::deserializeAnnotationLayer(QJsonDocument(root).toJson(),&restored,{}));
        auto* line = dynamic_cast<ArrowAnnotation*>(restored.itemAt(2));
        QVERIFY(line->binding(false).targetId.isNull());
        QVERIFY(line->binding(true).targetId.isNull());
        QCOMPARE(line->start(),s.arrow->start());
        QCOMPARE(line->end(),s.arrow->end());
    }
    void roundtripAndMalformedTargets() {
        Scene s;
        s.a->setRotation(45);
        const auto data = SnapTray::serializeAnnotationLayer(s.layer);
        AnnotationLayer restored;
        QVERIFY(SnapTray::deserializeAnnotationLayer(data,&restored,{}));
        auto* arrow = dynamic_cast<ArrowAnnotation*>(restored.itemAt(2));
        QCOMPARE(arrow->start(),s.arrow->start());
        QCOMPARE(arrow->binding(false),s.arrow->binding(false));
        auto* target = dynamic_cast<ShapeAnnotation*>(restored.itemAt(0));
        target->moveBy(QPointF(20,0));
        QCOMPARE(arrow->start(),s.arrow->start()+QPoint(20,0));
        auto doc = QJsonDocument::fromJson(data);
        auto root = doc.object(); auto items = root["items"].toArray();
        auto duplicate = items[1].toObject(); duplicate["id"] = items[0].toObject()["id"]; items[1] = duplicate;
        root["items"] = items;
        QVERIFY(SnapTray::deserializeAnnotationLayer(QJsonDocument(root).toJson(),&restored,{}));
        arrow = dynamic_cast<ArrowAnnotation*>(restored.itemAt(2));
        QVERIFY(arrow->binding(false).targetId.isNull());
        QVERIFY(arrow->binding(true).targetId.isNull());
        QVERIFY(restored.itemAt(0)->persistentId()!=restored.itemAt(1)->persistentId());
    }
};
QTEST_MAIN(TestArrowBindings)
#include "tst_ArrowBindings.moc"
