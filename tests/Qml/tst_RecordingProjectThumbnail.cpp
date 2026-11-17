#include <QtTest>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>
#include "qml/RecordingProjectThumbnail.h"

using namespace SnapTray;

class tst_RecordingProjectThumbnail : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_dir;
    int m_next = 0;
    RecordingProject project()
    {
        const QString path = m_dir.filePath(QString::number(++m_next) + QStringLiteral(".mp4"));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) qFatal("fixture");
        file.write("fixture");
        file.close();
        const auto result = RecordingProjects::create(path, false, RecordingRecipe::toJson({}, {}), {}, 5000, {});
        if (!result) qFatal("project");
        return *result;
    }
    static QImage poster()
    {
        QImage image(32, 18, QImage::Format_RGB32);
        image.fill(Qt::black);
        return image;
    }
private slots:
    void init() { RecordingProjects::setRootForTests(m_dir.filePath(QStringLiteral("projects"))); }
    void cleanup() { RecordingProjects::setRootForTests({}); }
    void coalescesBeforeDecode()
    {
        QString decoded;
        RecordingProjectThumbnailQueue queue(nullptr, [&](const RecordingProject& p, const std::atomic_bool&) {
            decoded = p.thumbnailPath();
            return poster();
        });
        auto p = project();
        queue.request(p);
        PreviewEditState edits;
        edits.trimStart = 100;
        QVERIFY(RecordingProjects::update(p, RecordingRecipe::toJson(edits, {}), {}));
        queue.request(p);
        QTRY_VERIFY(!p.thumbnailUrl().isEmpty());
        QCOMPARE(decoded, p.thumbnailPath());
    }
    void dedicatedWorkerAndShutdown()
    {
        auto* globalPool = QThreadPool::globalInstance();
        const int oldLimit = globalPool->maxThreadCount();
        globalPool->setMaxThreadCount(1);
        const auto restore = qScopeGuard([&] { globalPool->setMaxThreadCount(oldLimit); });
        std::atomic_int calls{0};
        std::atomic_bool finished{false};
        RecordingProjectThumbnailQueue queue(nullptr, [&](const RecordingProject&, const std::atomic_bool& cancelled) {
            ++calls;
            while (!cancelled.load()) QThread::msleep(5);
            finished = true;
            return QImage();
        });
        queue.request(project());
        queue.request(project());
        QTRY_COMPARE(calls.load(), 1);
        // A separate user job still runs while the thumbnail worker is held.
        auto marker = QtConcurrent::run([] { return 42; });
        QTRY_VERIFY(marker.isFinished());
        QCOMPARE(marker.result(), 42);
        queue.shutdown();
        QTRY_VERIFY(finished.load());
        QTest::qWait(50);
        QCOMPARE(calls.load(), 1);
    }
    void cancelsObsoleteActiveRevision()
    {
        std::atomic_int calls{0};
        std::atomic_bool cancelledOld{false};
        RecordingProjectThumbnailQueue queue(nullptr, [&](const RecordingProject&, const std::atomic_bool& cancelled) {
            if (++calls == 1) {
                while (!cancelled.load()) QThread::msleep(5);
                cancelledOld = true;
                return QImage();
            }
            return poster();
        });
        auto p = project();
        const auto old = p;
        queue.request(p);
        QTRY_COMPARE(calls.load(), 1);
        PreviewEditState edits;
        edits.trimStart = 100;
        QVERIFY(RecordingProjects::update(p, RecordingRecipe::toJson(edits, {}), {}));
        queue.request(p);
        QTRY_VERIFY(!p.thumbnailUrl().isEmpty());
        QVERIFY(cancelledOld.load());
        QCOMPARE(calls.load(), 2);
        QVERIFY(old.thumbnailUrl().isEmpty());
    }
    void retriesTransientFailure()
    {
        std::atomic_int calls{0};
        RecordingProjectThumbnailQueue queue(nullptr, [&](const RecordingProject&, const std::atomic_bool&) {
            return ++calls == 1 ? QImage() : poster();
        });
        const auto p = project();
        queue.request(p);
        QTRY_VERIFY(!p.thumbnailUrl().isEmpty());
        QCOMPARE(calls.load(), 2);
    }
    void boundsRetriesAndSkipsUnknownVersion()
    {
        std::atomic_int calls{0};
        RecordingProjectThumbnailQueue queue(nullptr, [&](const RecordingProject&, const std::atomic_bool&) {
            ++calls;
            return QImage();
        });
        const auto p = project();
        auto unknown = project();
        unknown.recipe.insert(QStringLiteral("version"), RecordingRecipe::kVersion + 1);
        queue.request(unknown);
        queue.request(p);
        QTRY_COMPARE(calls.load(), 3);
        queue.request(p);
        QTest::qWait(1200);
        QCOMPARE(calls.load(), 3);
        QVERIFY(p.thumbnailUrl().isEmpty());
    }
};
QTEST_GUILESS_MAIN(tst_RecordingProjectThumbnail)
#include "tst_RecordingProjectThumbnail.moc"
