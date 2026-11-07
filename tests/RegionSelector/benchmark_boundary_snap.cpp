// Synthetic acceptance evidence; see docs/developer/boundary-snap.md.
#include "region/BoundarySnapController.h"
#include "utils/CoordinateHelper.h"
#include <QGuiApplication>
#include <QPixmap>
#include <QElapsedTimer>
#include <QPainter>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QFile>
#include <QProcess>
#include <QSysInfo>
#include <algorithm>
#include <vector>
#include <limits>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace {
qint64 peakResidentBytes()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    return K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))
        ? qint64(counters.PeakWorkingSetSize) : -1;
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return -1;
#ifdef Q_OS_MACOS
    return usage.ru_maxrss;
#else
    return qint64(usage.ru_maxrss) * 1024;
#endif
#endif
}
QString cpuModel()
{
#ifdef Q_OS_MACOS
    QProcess process;
    process.start(QStringLiteral("/usr/sbin/sysctl"), {"-n", "machdep.cpu.brand_string"});
    if (process.waitForFinished(1000) && process.exitCode() == 0)
        return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
#elif defined(Q_OS_WIN)
    return qEnvironmentVariable("PROCESSOR_IDENTIFIER");
#else
    QFile file(QStringLiteral("/proc/cpuinfo"));
    if (file.open(QIODevice::ReadOnly)) {
        for (const QByteArray& line : file.readAll().split('\n'))
            if (line.startsWith("model name")) return QString::fromUtf8(line.mid(line.indexOf(':')+1)).trimmed();
    }
#endif
    return {}; // Unavailable metadata must not be guessed.
}
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    const QStringList args=app.arguments();
    if (args.size()!=1 && (args.size()!=3 || args[1]!="--report")) {
        qCritical()<<"Usage: RegionSelector_BoundarySnapBenchmark [--report file.json]";
        return 2;
    }
    QJsonArray scenarios;
    bool correct=true;
    for (QSize size : {QSize(3840,2160),QSize(6016,3384)}) {
        for (qreal dpr : {1.,1.25,1.5,2.}) {
            QImage image(size,QImage::Format_RGB32);
            if (image.isNull()) { qCritical()<<"Could not allocate source image";return 2; }
            image.fill(Qt::black);
            const int right=qFloor((size.width()-100)/dpr);
            const int bottom=qFloor((size.height()-100)/dpr);
            const int pixelRight=qCeil(right*dpr), pixelBottom=qCeil(bottom*dpr);
            { QPainter p(&image);p.fillRect(0,0,pixelRight,pixelBottom,Qt::white); }
            const QPixmap pixmap=QPixmap::fromImage(image);
            QElapsedTimer conversion;conversion.start();
            const QImage source=pixmap.toImage();
            if (source.isNull()) { qCritical()<<"Could not convert source pixmap";return 2; }
            BoundarySnapController controller;controller.setSource(source,dpr);
            const double conversionMs=conversion.nsecsElapsed()/1e6;
            const QRect raw(10,10,right-13,bottom-13);
            // Timing fallback is valid, but cannot make an inert implementation pass.
            const auto preflight = controller.snap(raw, Qt::RightEdge | Qt::BottomEdge,
                                                   (std::numeric_limits<qint64>::max)());
            const QRect preflightCrop = CoordinateHelper::toPhysicalCoveringRect(preflight.rect, dpr);
            const bool preflightCorrect = preflight.edges == (Qt::RightEdge | Qt::BottomEdge)
                && preflight.rect.topLeft() == raw.topLeft()
                && preflightCrop.right() + 1 == pixelRight
                && preflightCrop.bottom() + 1 == pixelBottom;
            correct &= preflightCorrect;
            std::vector<double> times;
            int hits=0, fallbacks=0, incorrect=0;
            for(int i=0;i<220;++i) {
                controller.reset(); QElapsedTimer timer;timer.start();
                const auto result=controller.snap(raw,Qt::RightEdge|Qt::BottomEdge);
                const double ms=timer.nsecsElapsed()/1e6;
                const QRect crop=CoordinateHelper::toPhysicalCoveringRect(result.rect,dpr);
                const bool hit=result.edges==(Qt::RightEdge|Qt::BottomEdge);
                const bool valid=hit
                    ? result.rect.topLeft()==raw.topLeft()
                        && crop.right()+1==pixelRight && crop.bottom()+1==pixelBottom
                    : !result.edges && result.rect==raw;
                correct &= valid;
                if(i>=20) { times.push_back(ms);hits+=hit && valid;fallbacks+=!hit && valid;incorrect+=!valid; }
            }
            std::sort(times.begin(),times.end());
            scenarios.append(QJsonObject{
                {"width",size.width()},{"height",size.height()},{"dpr",dpr},
                {"selection_width",raw.width()},{"selection_height",raw.height()},
                {"samples",200},{"p50_ms",times[99]},{"p95_ms",times[189]},
                {"preflight_correct",preflightCorrect},
                {"p95_target_met",times[189]<=4.0},{"hits",hits},{"fallbacks",fallbacks},
                {"incorrect_results",incorrect},{"source_bytes",double(source.sizeInBytes())},
                {"source_ms",conversionMs},{"source_shared",image.constBits()==source.constBits()},
                {"controller_bytes",int(sizeof(controller))},{"pixel_index_bytes",0}
            });
        }
    }
#ifdef NDEBUG
    const QString buildType=QStringLiteral("Release");
#else
    const QString buildType=QStringLiteral("Debug");
#endif
    const QJsonObject report{
        {"schema_version",1},{"synthetic",true},{"os",QSysInfo::prettyProductName()},
        {"cpu_arch",QSysInfo::currentCpuArchitecture()},{"cpu_model",cpuModel()},
        {"qt",qVersion()},{"platform",QGuiApplication::platformName()},
        {"build_type",buildType},{"peak_process_rss_bytes",double(peakResidentBytes())},
        {"correct",correct},{"scenarios",scenarios}
    };
    const QByteArray json=QJsonDocument(report).toJson();
    if(args.size()==3) {
        QSaveFile file(args[2]);
        if(!file.open(QIODevice::WriteOnly) || file.write(json)!=json.size() || !file.commit()) {
            qCritical()<<"Could not write benchmark report"<<args[2];return 2;
        }
    }
    qInfo().noquote()<<json;
    // Host-dependent timing is reported, never misrepresented as correctness.
    return correct ? 0 : 1;
}
