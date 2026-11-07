#include <QtTest>
#include <QPainter>
#include <QSignalSpy>
#include "region/BoundarySnapController.h"
#include "region/SelectionStateManager.h"
#include "utils/CoordinateHelper.h"

using Handle = SelectionStateManager::ResizeHandle;
namespace {
QImage rectangleImage(const QRect& physical, QSize size = {400,400}) {
    QImage image(size, QImage::Format_RGB32); image.fill(Qt::black);
    QPainter painter(&image); painter.fillRect(physical, Qt::white); return image;
}
Qt::Edges handleEdges(int h) {
    static const Qt::Edges edges[] = {{}, Qt::LeftEdge|Qt::TopEdge, Qt::TopEdge,
        Qt::RightEdge|Qt::TopEdge, Qt::LeftEdge, Qt::RightEdge,
        Qt::LeftEdge|Qt::BottomEdge, Qt::BottomEdge, Qt::RightEdge|Qt::BottomEdge};
    return edges[h];
}
}
class tst_BoundarySnap : public QObject
{
    Q_OBJECT
private slots:
    void handlesAndDpr_data() {
        QTest::addColumn<int>("handle"); QTest::addColumn<qreal>("dpr");
        for (int h=1;h<=8;++h) for (qreal d : {1.,1.25,1.5,2.})
            QTest::newRow(qPrintable(QString("%1-%2").arg(h).arg(d))) << h << d;
    }
    void handlesAndDpr() {
        QFETCH(int,handle); QFETCH(qreal,dpr);
        // Odd coordinates exercise floor/ceil asymmetry at fractional DPR.
        const QRect target(41,43,78,76);
        const QRect crop = CoordinateHelper::toPhysicalCoveringRect(target,dpr);
        BoundarySnapController controller; controller.setSource(rectangleImage(crop),dpr);
        auto edges=handleEdges(handle);
        QRect raw=target;
        if(edges.testFlag(Qt::LeftEdge)) raw.setLeft(raw.left()+3);
        if(edges.testFlag(Qt::RightEdge)) raw.setRight(raw.right()-3);
        if(edges.testFlag(Qt::TopEdge)) raw.setTop(raw.top()+3);
        if(edges.testFlag(Qt::BottomEdge)) raw.setBottom(raw.bottom()-3);
        SelectionStateManager manager; manager.setBounds(QRect(0,0,200,200));
        manager.setSelectionRect(raw);
        const QPoint press(173,159); // arbitrary off-center press offset
        manager.startResize(press,Handle(handle));
        QSignalSpy changes(&manager,&SelectionStateManager::selectionChanged);
        bool rawMatches = false;
        manager.updateResize(press,[&](const QRect& candidate,Qt::Edges moving) {
            rawMatches = candidate == raw;
            auto result=controller.snap(candidate,moving);
            return SelectionStateManager::ResizePreview{result.rect,result.edges};
        });
        QVERIFY(rawMatches);
        QCOMPARE(manager.selectionRect(),target);
        QCOMPARE(manager.snappedEdges(),edges);
        QCOMPARE(changes.count(),1);
        QCOMPARE(CoordinateHelper::toPhysicalCoveringRect(manager.selectionRect(),dpr),crop);
        manager.updateResize(press); // modifier release restores raw, without accumulation
        QCOMPARE(manager.selectionRect(),raw); QVERIFY(!manager.snappedEdges());
        manager.cancelResizeOrMove(); QCOMPARE(manager.selectionRect(),raw);
    }
    void unrepresentableEdges() {
        for (qreal dpr : {1.25,1.5,2.}) {
            // Pick a trailing boundary absent from ceil(integer * DPR).
            int edge=51;
            for (;;++edge) {
                bool representable=false;
                for(int x=20;x<60;++x) representable |= qCeil(x*dpr)==edge;
                if(!representable) break;
            }
            BoundarySnapController controller;
            controller.setSource(rectangleImage(QRect(0,0,edge,200)),dpr);
            const QRect raw(10,10,qRound(edge/dpr)-10,50);
            auto result=controller.snap(raw,Qt::RightEdge);
            QVERIFY(!result.edges); QCOMPARE(result.rect,raw);
        }
    }
    void hysteresisAndGeneration() {
        BoundarySnapController controller;
        controller.setSource(rectangleImage(QRect(100,0,100,400)),1);
        auto result=controller.snap(QRect(20,20,77,80),Qt::RightEdge);
        QCOMPARE(result.rect.x()+result.rect.width(),100);
        result=controller.snap(QRect(20,20,88,80),Qt::RightEdge);
        QVERIFY(result.edges); // retained at distance 8, outside entry radius
        result=controller.snap(QRect(20,20,90,80),Qt::RightEdge);
        QVERIFY(!result.edges); // outside exit radius
        const auto generation=controller.generation();
        controller.setSource(rectangleImage(QRect(100,0,100,30)),1);
        QVERIFY(controller.generation()>generation);
        result=controller.snap(QRect(20,0,77,20),Qt::RightEdge); QVERIFY(result.edges);
        result=controller.snap(QRect(20,40,77,80),Qt::RightEdge); QVERIFY(!result.edges);
        controller.setSource({},1);
        QVERIFY(!controller.snap(QRect(20,0,77,20),Qt::RightEdge).edges);
    }
    void invalidSourcesAndBudgetGuard() {
        BoundarySnapController controller;
        const QRect raw(10,10,80,80);
        for (qreal dpr : {0.0,-1.0,qQNaN(),qInf(),9.0}) {
            controller.setSource(rectangleImage(QRect(90,0,100,400)),dpr);
            auto result=controller.snap(raw,Qt::RightEdge);
            QCOMPARE(result.rect,raw);QVERIFY(!result.edges);
        }
        controller.setSource(rectangleImage(QRect(1,0,1,20000),QSize(2,20000)),1);
        QVERIFY(!controller.snap(QRect(0,0,1,20000),Qt::RightEdge).edges);
        controller.setSource(rectangleImage(QRect(90,0,100,400)),2);
        QVERIFY(!controller.snap(QRect(1000000000,0,100,100),Qt::RightEdge).edges);
    }
    void tieAndConstraints() {
        QImage image(300,300,QImage::Format_RGB32);image.fill(Qt::black);
        {QPainter p(&image);p.fillRect(100,0,4,300,Qt::white);}
        BoundarySnapController controller;controller.setSource(image,1);
        auto result=controller.snap(QRect(20,20,82,80),Qt::RightEdge);
        QCOMPARE(result.rect.x()+result.rect.width(),100); // stable lower-coordinate tie
        SelectionStateManager manager;manager.setBounds(QRect(0,0,300,300));
        manager.setSelectionRect(QRect(50,50,80,80));manager.startResize({129,129},Handle::BottomRight);
        manager.updateResize({128,128},[](const QRect& r,Qt::Edges e) {
            return SelectionStateManager::ResizePreview{QRect(r.topLeft(),QSize(5,5)),e};
        });
        QCOMPARE(manager.selectionRect(),QRect(50,50,79,79));QVERIFY(!manager.snappedEdges());
        manager.updateResize({128,128},[](const QRect& r,Qt::Edges e) {
            return SelectionStateManager::ResizePreview{r.translated(-1,0),e};
        });
        QCOMPARE(manager.selectionRect(),QRect(50,50,79,79));
        manager.updateResize({128,128},[](const QRect&,Qt::Edges e) {
            return SelectionStateManager::ResizePreview{QRect(50,50,300,300),e};
        });
        QCOMPARE(manager.selectionRect(),QRect(50,50,79,79));
        manager.setAspectRatio(1.0);bool called=false;
        manager.updateResize({128,128},[&](const QRect& r,Qt::Edges e) {
            called=true;return SelectionStateManager::ResizePreview{r,e};
        });QVERIFY(!called);
    }
};
QTEST_GUILESS_MAIN(tst_BoundarySnap)
#include "tst_BoundarySnap.moc"
