// Standalone release-mode probe; see docs/developer/boundary-snap.md.
#include "region/BoundarySnapController.h"
#include <QGuiApplication>
#include <QPixmap>
#include <QElapsedTimer>
#include <QPainter>
#include <QDebug>
#include <algorithm>
#include <vector>

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    for (QSize size : {QSize(3840,2160),QSize(6016,3384)}) {
        for (qreal dpr : {1.,1.25,1.5,2.}) {
            QImage image(size,QImage::Format_RGB32);image.fill(Qt::black);
            // Full-height/high-width candidate bands, both axes, no cached locks.
            const int right=qFloor((size.width()-100)/dpr);
            const int bottom=qFloor((size.height()-100)/dpr);
            const int pixelRight=qCeil(right*dpr), pixelBottom=qCeil(bottom*dpr);
            { QPainter p(&image);p.fillRect(0,0,pixelRight,pixelBottom,Qt::white); }
            const QPixmap pixmap=QPixmap::fromImage(image);
            QElapsedTimer conversion;conversion.start();
            const QImage source=pixmap.toImage();
            BoundarySnapController controller;controller.setSource(source,dpr);
            const double conversionMs=conversion.nsecsElapsed()/1e6;
            const QRect raw(10,10,right-13,bottom-13);
            std::vector<double> times;int hits=0;
            for(int i=0;i<220;++i) {
                controller.reset(); QElapsedTimer timer;timer.start();
                const auto result=controller.snap(raw,Qt::RightEdge|Qt::BottomEdge);
                double ms=timer.nsecsElapsed()/1e6;
                if(i>=20) { times.push_back(ms);hits+=result.edges==(Qt::RightEdge|Qt::BottomEdge); }
            }
            std::sort(times.begin(),times.end());
            qInfo().nospace()<<size.width()<<"x"<<size.height()<<" dpr="<<dpr
                <<" logical="<<raw.width()<<"x"<<raw.height()<<" p50_ms="<<times[100]
                <<" p95_ms="<<times[189]<<" hits="<<hits<<"/200 image_bytes="<<source.sizeInBytes()
                <<" source_ms="<<conversionMs<<" source_shared="<<(image.constBits()==source.constBits())
                <<" controller_bytes="<<sizeof(controller);
        }
    }
}
