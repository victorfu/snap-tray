// Developer-only offline evaluator. Built with the test tools, never shipped.
#include "longshot/FrameReaderLongshotSource.h"
#include "longshot/LongshotSession.h"
#include "SyntheticScroll.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSaveFile>
#include <QTextStream>
#ifdef Q_OS_UNIX
#include <sys/resource.h>
#endif
using namespace SnapTray::Longshot;

static bool writeJson(const QString& path, const QJsonObject& object)
{
    QSaveFile file(path);
    const auto bytes = QJsonDocument(object).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

static QJsonObject evaluate(const QJsonObject& spec, const QDir& base, const QString& output)
{
    QJsonObject result{{"id", spec.value("id")}, {"passed", false}};
    auto fail = [&](const QString& error) { result["error"] = error; return result; };
    const QString video = base.absoluteFilePath(spec.value("video").toString());
    QFile source(video);
    if (!source.open(QIODevice::ReadOnly)) return fail("Source unavailable");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&source)) return fail("Source could not be hashed");
    const QString digest = QString::fromLatin1(hash.result().toHex());
    result["sha256"] = digest;
    if (!spec.value("sha256").toString().isEmpty() && spec.value("sha256").toString() != digest)
        return fail("Source checksum mismatch");
    source.close();
    if (!QDir().mkpath(output)) return fail("Cannot create output directory");
    LongshotSession session(FrameReaderLongshotSource::createNative);
    session.setRecording(video);
    const qint64 start = spec.value("startMs").toVariant().toLongLong();
    const qint64 end = spec.contains("endMs") ? spec.value("endMs").toVariant().toLongLong() : -1;
    if (start < 0 || (end != -1 && end <= start)) return fail("Invalid trim interval");
    session.setTrim(start, end);
    const auto crop = spec.value("crop").toArray();
    if (!crop.isEmpty()) {
        if (crop.size() != 4 || crop[0].toInt(-1) < 0 || crop[1].toInt(-1) < 0
            || crop[2].toInt() < kMinAnalysisSide || crop[3].toInt() < kMinAnalysisSide)
            return fail("Invalid crop: expected x,y,width,height");
        session.setCrop(QRect(crop[0].toInt(), crop[1].toInt(), crop[2].toInt(), crop[3].toInt()));
    }
    LongshotOptions options; options.splitOversize = true;
    session.setOptions(options);
    QElapsedTimer timer; timer.start();
    const auto report = session.run({});
    result["elapsedMs"] = double(timer.elapsed());
    result["engineError"] = int(report.error);
    result["framesAnalyzed"] = report.framesAnalyzed;
    result["decodeCacheBytes"] = double(session.decodeCache().bytes());
#ifdef Q_OS_UNIX
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
#ifdef Q_OS_MACOS
        result["processPeakRssBytes"] = double(usage.ru_maxrss);
#else
        result["processPeakRssBytes"] = double(usage.ru_maxrss) * 1024;
#endif
    }
#endif
    if (report.error != LongshotError::None || report.render.parts.isEmpty()) return fail("Analysis/render failed");
    result["breakRows"] = int(report.render.breakRows.size());
    result["breakTimes"] = int(report.analysis.solve.breakTimesMs.size());
    result["lowConfidenceRows"] = int(report.render.lowConfidenceRows.size());
    result["fullHeight"] = report.render.fullHeightPx;
    QJsonArray parts;
    int totalHeight = 0;
    for (int i = 0; i < report.render.parts.size(); ++i) {
        const auto& image = report.render.parts[i];
        const QString name = QString("part-%1.png").arg(i + 1, 3, 10, QLatin1Char('0'));
        QSaveFile file(QDir(output).filePath(name));
        if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit()) return fail("PNG write failed");
        const auto info = report.render.partInfo.value(i);
        parts.append(QJsonObject{{"file", name}, {"width", image.width()}, {"height", image.height()},
                                 {"section", info.section + 1}, {"partInSection", info.indexInSection + 1},
                                 {"partsInSection", info.partsInSection}, {"sourceStartMs", double(info.startMs)},
                                 {"sourceEndMs", double(info.endMs)}});
        totalHeight += image.height();
    }
    result["parts"] = parts;
    result["sectionCount"] = report.render.sectionCount;
    if (report.render.sectionCount > 1)
        return fail("Multiple independent sections exported; a single ground-truth image cannot certify their ordering");
    const QString truthPath = spec.value("groundTruth").toString();
    if (truthPath.isEmpty()) return fail("Manual review required: no ground truth; not an automated pass");
    const QImage truth(base.absoluteFilePath(truthPath));
    if (truth.isNull() || truth.width() != report.render.parts.first().width()) return fail("Invalid ground truth dimensions");
    // Bound the evaluation-only joined image; the production renderer remains split.
    constexpr qint64 kEvaluationImageBudget = 256LL * 1024 * 1024;
    if (qint64(truth.width()) * totalHeight * 4 > kEvaluationImageBudget) return fail("Evaluation image exceeds budget");
    QImage joined(truth.width(), totalHeight, QImage::Format_RGB32);
    if (joined.isNull()) return fail("Evaluation allocation failed");
    QPainter painter(&joined);
    int y = 0;
    for (const auto& part : report.render.parts) { painter.drawImage(0, y, part); y += part.height(); }
    painter.end();
    const auto metrics = SyntheticScroll::compareWithGroundTruth(joined, truth, 0, truth.height() - 1);
    result["duplicatedRows"] = metrics.duplicatedRows;
    result["missingRows"] = metrics.missingRows;
    result["misalignedRows"] = metrics.misalignedRows;
    result["unmatchedRows"] = metrics.unmatchedRows;
    result["passed"] = metrics.duplicatedRows == 0 && metrics.missingRows == 0
        && metrics.misalignedRows == 0 && metrics.unmatchedRows == 0 && totalHeight == truth.height()
        && report.render.breakRows.empty() && report.analysis.solve.breakTimesMs.empty() && !report.render.heightCapped;
    return result;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("Offline longshot evaluation; manual/unavailable cases never pass.");
    parser.addHelpOption();
    parser.addOptions({{{"m", "manifest"}, "JSON manifest with a cases array", "path"},
                       {{"v", "video"}, "Single input recording", "path"},
                       {{"o", "output"}, "Output directory", "path"},
                       {{"g", "ground-truth"}, "Ground truth PNG", "path"},
                       {"start", "Start time in milliseconds", "ms", "0"},
                       {"end", "Exclusive end time (-1 means end)", "ms", "-1"},
                       {"crop", "Video-pixel crop x,y,width,height", "rect"}});
    parser.process(app);
    if (!parser.isSet("output") || parser.isSet("manifest") == parser.isSet("video")) parser.showHelp(2);
    QDir base(QDir::currentPath()); QJsonArray cases;
    if (parser.isSet("manifest")) {
        QFile file(parser.value("manifest"));
        if (!file.open(QIODevice::ReadOnly)) return 2;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) return 2;
        cases = document.object().value("cases").toArray();
        base = QFileInfo(file).absoluteDir();
    } else {
        bool startOk, endOk;
        const auto start = parser.value("start").toLongLong(&startOk);
        const auto end = parser.value("end").toLongLong(&endOk);
        if (!startOk || !endOk) return 2;
        QJsonObject spec{{"id", "recording"}, {"video", parser.value("video")},
                         {"groundTruth", parser.value("ground-truth")}, {"startMs", double(start)}, {"endMs", double(end)}};
        if (parser.isSet("crop")) {
            QJsonArray crop;
            for (const auto& value : parser.value("crop").split(',')) {
                bool ok; int n = value.toInt(&ok); if (!ok) return 2; crop.append(n);
            }
            spec["crop"] = crop;
        }
        cases.append(spec);
    }
    if (cases.isEmpty()) return 2;
    QJsonArray results; bool passed = true;
    for (int i = 0; i < cases.size(); ++i) {
        auto result = evaluate(cases[i].toObject(), base, QDir(parser.value("output")).filePath(QString::number(i + 1)));
        passed &= result.value("passed").toBool(); results.append(result);
    }
    QJsonObject report{{"schemaVersion", 1}, {"passed", passed}, {"cases", results}};
    if (!QDir().mkpath(parser.value("output")) || !writeJson(QDir(parser.value("output")).filePath("report.json"), report)) return 2;
    QTextStream(stdout) << QJsonDocument(report).toJson();
    return passed ? 0 : 1;
}
