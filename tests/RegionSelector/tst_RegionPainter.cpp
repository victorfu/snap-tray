#include <QtTest>

#include <QImage>
#include <QPainter>
#include <QWidget>

#include "annotations/AnnotationLayer.h"
#include "region/RegionPainter.h"
#include "region/SelectionStateManager.h"
#include "tools/ToolId.h"

class tst_RegionPainter : public QObject
{
    Q_OBJECT

private slots:
    void testStaticSelectionReusesBaseLayerCache();
    void testSelectingStateDoesNotUseBaseLayerCache();
};

void tst_RegionPainter::testStaticSelectionReusesBaseLayerCache()
{
    QWidget host;
    host.resize(800, 600);

    SelectionStateManager selectionManager;
    selectionManager.setBounds(QRect(QPoint(0, 0), host.size()));
    selectionManager.setSelectionRect(QRect(120, 140, 220, 160));

    AnnotationLayer annotationLayer;
    RegionPainter regionPainter;
    regionPainter.setParentWidget(&host);
    regionPainter.setSelectionManager(&selectionManager);
    regionPainter.setAnnotationLayer(&annotationLayer);
    regionPainter.setCurrentTool(static_cast<int>(ToolId::Pencil));
    regionPainter.setDevicePixelRatio(1.0);

    QPixmap background(host.size());
    background.fill(Qt::white);

    QImage firstFrame(host.size(), QImage::Format_ARGB32_Premultiplied);
    firstFrame.fill(Qt::transparent);
    {
        QPainter painter(&firstFrame);
        regionPainter.paint(painter, background, host.rect());
    }
    QVERIFY(!regionPainter.lastPaintStats().usedBaseLayerCache);

    QImage secondFrame(host.size(), QImage::Format_ARGB32_Premultiplied);
    secondFrame.fill(Qt::transparent);
    {
        QPainter painter(&secondFrame);
        regionPainter.paint(painter, background, host.rect());
    }
    QVERIFY(regionPainter.lastPaintStats().usedBaseLayerCache);
}

void tst_RegionPainter::testSelectingStateDoesNotUseBaseLayerCache()
{
    QWidget host;
    host.resize(800, 600);

    SelectionStateManager selectionManager;
    selectionManager.setBounds(QRect(QPoint(0, 0), host.size()));
    selectionManager.startSelection(QPoint(100, 100));
    selectionManager.updateSelection(QPoint(260, 220));

    AnnotationLayer annotationLayer;
    RegionPainter regionPainter;
    regionPainter.setParentWidget(&host);
    regionPainter.setSelectionManager(&selectionManager);
    regionPainter.setAnnotationLayer(&annotationLayer);
    regionPainter.setCurrentTool(static_cast<int>(ToolId::Pencil));
    regionPainter.setDevicePixelRatio(1.0);

    QPixmap background(host.size());
    background.fill(Qt::white);

    QImage frame(host.size(), QImage::Format_ARGB32_Premultiplied);
    frame.fill(Qt::transparent);
    {
        QPainter painter(&frame);
        regionPainter.paint(painter, background, host.rect());
    }

    QVERIFY(!regionPainter.lastPaintStats().usedBaseLayerCache);
}

QTEST_MAIN(tst_RegionPainter)
#include "tst_RegionPainter.moc"
