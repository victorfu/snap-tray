#include <QtTest>
#include <QPainter>
#include <QSignalSpy>
#include <limits>
#include "region/BoundarySnapController.h"
#include "region/SelectionStateManager.h"
#include "utils/CoordinateHelper.h"

using Handle = SelectionStateManager::ResizeHandle;
namespace {
// Geometry assertions must not depend on CI scheduling; timeout has an explicit case.
class GeometrySnapController : public BoundarySnapController
{
public:
    Result snap(const QRect& raw, Qt::Edges edges,
                qint64 budget = (std::numeric_limits<qint64>::max)())
    {
        return BoundarySnapController::snap(raw, edges, budget);
    }
};

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
    void roundedScreenEdge_data()
    {
        QTest::addColumn<QSize>("pixels");
        QTest::addColumn<qreal>("dpr");
        QTest::addColumn<bool>("bottomHandle");
        const QList<QPair<QSize, qreal>> screens = {
            {{400, 400}, 1.5}, {{1366, 768}, 1.25},
            {{2560, 1600}, 1.5}, {{2560, 1600}, 1.75},
            {{3840, 2160}, 2.25}
        };
        for (const auto& screen : screens) {
            for (bool bottom : {false, true}) {
                const auto name = QString("%1x%2-dpr%3-%4")
                    .arg(screen.first.width()).arg(screen.first.height())
                    .arg(screen.second).arg(bottom ? "bottom" : "right");
                QTest::newRow(qPrintable(name)) << screen.first << screen.second << bottom;
            }
        }
    }

    void roundedScreenEdge()
    {
        QFETCH(QSize, pixels);
        QFETCH(qreal, dpr);
        QFETCH(bool, bottomHandle);
        const QSize canvas = pixels / dpr;
        const int boundary = qCeil(100 * dpr);
        QImage image(pixels, QImage::Format_RGB32);
        image.fill(Qt::black);
        {
            QPainter painter(&image);
            painter.fillRect(bottomHandle
                ? QRect(0, boundary, pixels.width(), pixels.height() - boundary)
                : QRect(boundary, 0, pixels.width() - boundary, pixels.height()), Qt::white);
        }
        const QRect raw = bottomHandle
            ? QRect(0, 20, canvas.width(), 77)
            : QRect(20, 0, 77, canvas.height());
        const auto edge = bottomHandle ? Qt::BottomEdge : Qt::RightEdge;
        GeometrySnapController controller;
        controller.setSource(image, dpr);
        const auto result = controller.snap(raw, edge, (std::numeric_limits<qint64>::max)());
        QCOMPARE(result.edges, Qt::Edges(edge));
        const QRect crop = CoordinateHelper::toPhysicalCoveringRect(result.rect, dpr)
            .intersected(image.rect());
        QCOMPARE(bottomHandle ? crop.bottom() + 1 : crop.right() + 1, boundary);
        QCOMPARE(bottomHandle ? crop.width() : crop.height(),
                 bottomHandle ? pixels.width() : pixels.height());
    }

    void handlesAndDpr_data() {
        QTest::addColumn<int>("handle"); QTest::addColumn<qreal>("dpr");
        for (int h=1;h<=8;++h) for (qreal d : {1.,1.25,1.5,2.})
            QTest::newRow(qPrintable(QString("%1-dpr-%2")
                .arg(QString::fromLatin1(QMetaEnum::fromType<Handle>().valueToKey(h))).arg(d))) << h << d;
    }
    void handlesAndDpr() {
        QFETCH(int,handle); QFETCH(qreal,dpr);
        // Odd coordinates exercise floor/ceil asymmetry at fractional DPR.
        const QRect target(41,43,78,76);
        const QRect crop = CoordinateHelper::toPhysicalCoveringRect(target,dpr);
        GeometrySnapController controller; controller.setSource(rectangleImage(crop),dpr);
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
            GeometrySnapController controller;
            controller.setSource(rectangleImage(QRect(0,0,edge,200)),dpr);
            const QRect raw(10,10,qRound(edge/dpr)-10,50);
            auto result=controller.snap(raw,Qt::RightEdge);
            QVERIFY(!result.edges); QCOMPARE(result.rect,raw);
        }
    }
    void hysteresisAndSourceReplacement() {
        GeometrySnapController controller;
        controller.setSource(rectangleImage(QRect(100,0,100,400)),1);
        auto result=controller.snap(QRect(20,20,77,80),Qt::RightEdge);
        QCOMPARE(result.rect.x()+result.rect.width(),100);
        result=controller.snap(QRect(20,20,88,80),Qt::RightEdge);
        QVERIFY(result.edges); // retained at distance 8, outside entry radius
        result=controller.snap(QRect(20,20,90,80),Qt::RightEdge);
        QVERIFY(!result.edges); // outside exit radius
        controller.setSource(rectangleImage(QRect(100,0,100,30)),1);
        result=controller.snap(QRect(20,0,77,20),Qt::RightEdge); QVERIFY(result.edges);
        result=controller.snap(QRect(20,40,77,80),Qt::RightEdge); QVERIFY(!result.edges);
        controller.setSource({},1);
        QVERIFY(!controller.snap(QRect(20,0,77,20),Qt::RightEdge).edges);
    }
    void lightAndDarkCards_data() {
        QTest::addColumn<bool>("dark");QTest::addColumn<qreal>("dpr");
        for(bool dark : {false,true}) for(qreal dpr : {1.,1.25,1.5,2.})
            QTest::newRow(qPrintable(QString("%1-%2").arg(dark).arg(dpr))) << dark << dpr;
    }
    void lightAndDarkCards() {
        QFETCH(bool,dark);QFETCH(qreal,dpr);
        const QRect target(41,43,78,76);
        const QRect physical=CoordinateHelper::toPhysicalCoveringRect(target,dpr);
        QImage image(400,400,QImage::Format_RGB32);image.fill(dark ? QColor(20,20,20) : QColor(220,220,220));
        {QPainter p(&image);p.fillRect(physical,dark ? QColor(60,60,60) : Qt::white);}
        GeometrySnapController controller;controller.setSource(image,dpr);
        QRect raw=target;raw.setRight(raw.right()-3);raw.setBottom(raw.bottom()-3);
        const auto result=controller.snap(raw,Qt::RightEdge|Qt::BottomEdge);
        QCOMPARE(result.rect,target);QCOMPARE(result.edges,Qt::Edges(Qt::RightEdge|Qt::BottomEdge));
    }
    void rankingAndJitter() {
        QImage image(300,200,QImage::Format_RGB32); image.fill(Qt::black);
        {QPainter p(&image);p.fillRect(100,0,4,200,QColor(80,80,80));
         p.fillRect(104,0,196,200,Qt::white);}
        GeometrySnapController controller;controller.setSource(image,1);
        // Equidistant candidates choose stronger 104, nearer 100 wins otherwise.
        QCOMPARE(controller.snap(QRect(20,20,82,80),Qt::RightEdge).rect.right()+1,104);
        controller.reset();
        QCOMPARE(controller.snap(QRect(20,20,79,80),Qt::RightEdge).rect.right()+1,100);
        int switches=0;int previous=100;
        for (int boundary : {101,102,103,102,101,104,103,102,101}) {
            const auto result=controller.snap(QRect(20,20,boundary-20,80),Qt::RightEdge);
            QVERIFY(result.edges.testFlag(Qt::RightEdge));
            const int snapped=result.rect.right()+1;
            switches+=snapped!=previous;previous=snapped;
        }
        QCOMPARE(switches,0); // synthetic jitter metric, not a real pointer study
        // Leave lock 100: 104 remains within entry distance and takes over.
        QCOMPARE(controller.snap(QRect(20,20,90,80),Qt::RightEdge).rect.right()+1,104);
        // Same distance and exact mean (red-only distances): broader support wins.
        QImage supportImage(300,100,QImage::Format_RGB32);supportImage.fill(Qt::black);
        for(int y=0;y<100;++y) for(int x=100;x<300;++x) {
            const int red=(y<60 ? 100 : 0)+(x>=104 ? 60 : 0);
            supportImage.setPixel(x,y,qRgb(red,0,0));
        }
        controller.setSource(supportImage,1);
        QCOMPARE(controller.snap(QRect(20,0,82,100),Qt::RightEdge).rect.right()+1,104);
    }
    void cornerQueriesUseRawSpans() {
        QImage image(200,200,QImage::Format_RGB32);image.fill(Qt::black);
        {QPainter p(&image);
         p.fillRect(100,20,100,80,Qt::white);
         // At y=100, contrast covers 43/77 raw pixels (passes 55%), but
         // only 43/80 if X is snapped first (fails). Both axes must hit.
         p.fillRect(20,100,43,100,Qt::white);}
        GeometrySnapController controller;controller.setSource(image,1);
        const auto result=controller.snap(QRect(20,20,77,77),Qt::RightEdge|Qt::BottomEdge);
        QCOMPARE(result.edges,Qt::Edges(Qt::RightEdge|Qt::BottomEdge));
        QCOMPARE(result.rect,QRect(20,20,80,80));
    }
    void sourceReplacementChangesScaleAndLock() {
        GeometrySnapController controller;
        controller.setSource(rectangleImage(QRect(100,0,100,400)),1);
        QCOMPARE(controller.snap(QRect(20,20,77,80),Qt::RightEdge).rect.right()+1,100);
        controller.setSource(rectangleImage(QRect(200,0,8,400)),2);
        // The old logical lock at 100 still qualifies in the new image. It
        // must be discarded so nearer 104 wins instead of hysteresis keeping it.
        const auto result=controller.snap(QRect(20,20,83,80),Qt::RightEdge);
        QCOMPARE(result.rect.right()+1,104);
        QCOMPARE(CoordinateHelper::toPhysicalCoveringRect(result.rect,2).right()+1,208);
    }
    void timeoutFallsBackAndDropsLocks() {
        GeometrySnapController controller;
        controller.setSource(rectangleImage(QRect(100,0,100,400)),1);
        const QRect raw(20,20,77,80);
        QVERIFY(controller.snap(raw,Qt::RightEdge).edges);
        const auto expired=controller.snap(raw,Qt::RightEdge,{0});
        QCOMPARE(expired.rect,raw);QVERIFY(!expired.edges);
        // Distance 8 would keep the old lock, but cannot enter a fresh one.
        const QRect outsideEntry(20,20,88,80);
        const auto next=controller.snap(outsideEntry,Qt::RightEdge);
        QCOMPARE(next.rect,outsideEntry);QVERIFY(!next.edges);
    }
    void invalidSourcesAndBudgetGuard() {
        GeometrySnapController controller;
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
        GeometrySnapController controller;controller.setSource(image,1);
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
