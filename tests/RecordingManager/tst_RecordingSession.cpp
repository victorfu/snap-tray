#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include "qml/RecordingRecoveryViewModel.h"

#include <QLockFile>
#include <memory>
#include "recording/RecordingSession.h"
#include "recording/WindowTimelineSidecar.h"
#include "encoding/NativeGifEncoder.h"
#include "encoding/WebPAnimEncoder.h"
#include <QSignalSpy>

using SnapTray::RecordingSessionInfo;
namespace Session = SnapTray::RecordingSession;

namespace {

QString touch(const QString& path, const QByteArray& bytes = QByteArray("data"))
{
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) file.write(bytes);
    return path;
}

RecordingSessionInfo sessionFor(const QString& video)
{
    RecordingSessionInfo info;
    info.sessionId = QStringLiteral("id-1");
    info.videoPath = video;
    info.format = QStringLiteral("mp4");
    info.frameSize = QSize(1920, 1080);
    info.frameRate = 30;
    info.audioRequired = true;
    info.fragmented = true;
    info.startedAt = QDateTime::fromString(QStringLiteral("2026-11-01T10:00:00Z"), Qt::ISODate);
    info.updatedAt = info.startedAt.addSecs(90);
    info.recordedMs = 90000;
    return info;
}

} // namespace

class TestRecordingSession : public QObject
{
    Q_OBJECT

private slots:
    void manifestRoundTrip();
    void unreadableManifestsAreReported();
    void manifestCannotPointElsewhere();
    void scanTrustsTheFileNotTheManifest();
    void scanSkipsActiveAndDropsOrphans();
    void discardRemovesEverything();
    void recoveryTextsAreHonest();
    void discardRespectsTheSessionLock();
    void finishedAnimationsAreRecoverable();
};

void TestRecordingSession::manifestRoundTrip()
{
    QTemporaryDir dir;
    const QString video = dir.filePath(QStringLiteral("SnapTray_Recording_1.mp4"));
    QVERIFY(Session::write(sessionFor(video)));
    QVERIFY(Session::hasSession(video));
    QString error;
    const auto read = Session::read(Session::manifestPathFor(video), &error);
    QVERIFY2(read.has_value(), qPrintable(error));
    QCOMPARE(read->sessionId, QStringLiteral("id-1"));
    QCOMPARE(read->videoPath, video);
    QCOMPARE(read->state, RecordingSessionInfo::State::Recording);
    QCOMPARE(read->frameSize, QSize(1920, 1080));
    QVERIFY(read->audioRequired);
    QVERIFY(read->fragmented);
    QVERIFY(read->intermediate);
    QCOMPARE(read->recordedMs, qint64(90000));
    QCOMPARE(read->startedAt, sessionFor(video).startedAt);

    // Rewrites replace the file whole (write-and-rename), never leaving a mix.
    auto finished = sessionFor(video);
    finished.state = RecordingSessionInfo::State::Finished;
    QVERIFY(Session::write(finished));
    QCOMPARE(Session::read(Session::manifestPathFor(video))->state, RecordingSessionInfo::State::Finished);
    // Recorded at the selected quality round-trips; a manifest from before
    // the field existed counts as intermediate (keeps the quality check).
    finished.intermediate = false;
    QVERIFY(Session::write(finished));
    QVERIFY(!Session::read(Session::manifestPathFor(video))->intermediate);
    touch(Session::manifestPathFor(video),
          R"({"version": 1, "video": "SnapTray_Recording_1.mp4", "state": "finished"})");
    QVERIFY(Session::read(Session::manifestPathFor(video))->intermediate);
    QVERIFY(QDir(dir.path()).entryList({QStringLiteral("*.tmp"), QStringLiteral("*~")}, QDir::Files).isEmpty());
}

void TestRecordingSession::unreadableManifestsAreReported()
{
    QTemporaryDir dir;
    const QString video = dir.filePath(QStringLiteral("SnapTray_Recording_2.mp4"));
    QString error;
    QVERIFY(!Session::read(Session::manifestPathFor(video), &error));
    QVERIFY(!error.isEmpty());

    touch(Session::manifestPathFor(video), "{ not json");
    QVERIFY(!Session::read(Session::manifestPathFor(video), &error));
    QVERIFY(error.contains(QStringLiteral("corrupt")));

    // A manifest from a newer SnapTray is not guessed at.
    touch(Session::manifestPathFor(video),
          R"({"version": 99, "video": "SnapTray_Recording_2.mp4", "state": "recording"})");
    QVERIFY(!Session::read(Session::manifestPathFor(video), &error));
    QVERIFY(error.contains(QStringLiteral("version")));
}

void TestRecordingSession::manifestCannotPointElsewhere()
{
    QTemporaryDir dir;
    const QString video = dir.filePath(QStringLiteral("SnapTray_Recording_3.mp4"));
    QString error;
    touch(Session::manifestPathFor(video),
          R"({"version": 1, "video": "../../etc/passwd", "state": "recording"})");
    QVERIFY(!Session::read(Session::manifestPathFor(video), &error));
    touch(Session::manifestPathFor(video),
          R"({"version": 1, "video": "SnapTray_Recording_other.mp4", "state": "recording"})");
    QVERIFY(!Session::read(Session::manifestPathFor(video), &error));
    QVERIFY(error.contains(QStringLiteral("does not match")));
}

void TestRecordingSession::scanTrustsTheFileNotTheManifest()
{
    QTemporaryDir dir;
    const QString playable = touch(dir.filePath(QStringLiteral("SnapTray_Recording_a.mp4")));
    const QString truncated = touch(dir.filePath(QStringLiteral("SnapTray_Recording_b.mp4")));
    const QString silent = touch(dir.filePath(QStringLiteral("SnapTray_Recording_c.mp4")));
    auto a = sessionFor(playable);
    a.state = RecordingSessionInfo::State::Finished;
    QVERIFY(Session::write(a));
    auto b = sessionFor(truncated);
    b.fragmented = false;  // a crash without fragments leaves nothing playable
    QVERIFY(Session::write(b));
    QVERIFY(Session::write(sessionFor(silent)));

    const auto probe = [&](const QString& path) {
        Session::ProbeResult result;
        if (path == playable) result = {true, 120000, true};
        if (path == silent) result = {true, 45000, false};
        return result;  // truncated: not decodable
    };
    const auto found = Session::scan(dir.path(), probe, {});
    QCOMPARE(found.size(), 3);
    for (const auto& item : found) {
        if (item.info.videoPath == playable) {
            QVERIFY(item.decodable);
            QVERIFY(item.complete);
            QCOMPARE(item.durationMs, qint64(120000));
            QVERIFY(!item.audioMissing);
        } else if (item.info.videoPath == truncated) {
            QVERIFY(!item.decodable);
            QCOMPARE(item.durationMs, qint64(0));
            QVERIFY(item.problem.contains(QStringLiteral("not crash-safe")));
        } else {
            QVERIFY(item.decodable);
            QVERIFY(!item.complete);
            // Audio was required but is not there: it must say so.
            QVERIFY(item.audioMissing);
        }
    }
}

void TestRecordingSession::scanSkipsActiveAndDropsOrphans()
{
    QTemporaryDir dir;
    const QString active = touch(dir.filePath(QStringLiteral("SnapTray_Recording_live.mp4")));
    QVERIFY(Session::write(sessionFor(active)));
    const QString gone = dir.filePath(QStringLiteral("SnapTray_Recording_gone.mp4"));
    QVERIFY(Session::write(sessionFor(gone)));  // its video was saved or deleted
    const QString corrupt = touch(dir.filePath(QStringLiteral("SnapTray_Recording_bad.mp4")));
    touch(Session::manifestPathFor(corrupt), "garbage");

    const auto found = Session::scan(dir.path(), [](const QString&) { return Session::ProbeResult{true, 1000, true}; },
                                     [&](const QString& path) { return path == active; });
    QCOMPARE(found.size(), 1);
    // An unreadable manifest is shown (with its problem), not silently deleted.
    QCOMPARE(found.first().info.videoPath, corrupt);
    QVERIFY(!found.first().problem.isEmpty());
    QVERIFY(QFileInfo::exists(Session::manifestPathFor(corrupt)));
    // The active session is untouched; the orphan manifest is cleaned up.
    QVERIFY(Session::hasSession(active));
    QVERIFY(!Session::hasSession(gone));
}

void TestRecordingSession::discardRemovesEverything()
{
    QTemporaryDir dir;
    const QString video = touch(dir.filePath(QStringLiteral("SnapTray_Recording_d.mp4")));
    QVERIFY(Session::write(sessionFor(video)));
    touch(SnapTray::WindowTimelineSidecar::pathFor(video));
    Session::discard(video);
    QVERIFY(!QFileInfo::exists(video));
    QVERIFY(!QFileInfo::exists(SnapTray::WindowTimelineSidecar::pathFor(video)));
    QVERIFY(!Session::hasSession(video));
}

void TestRecordingSession::recoveryTextsAreHonest()
{
    using VM = SnapTray::RecordingRecoveryViewModel;
    Session::Recoverable item;
    item.decodable = false;
    QVERIFY(VM::statusText(item).contains(QStringLiteral("Cannot be recovered")));
    item.decodable = true;
    item.durationMs = 151000;
    QVERIFY(VM::statusText(item).contains(QStringLiteral("2:31")));
    QVERIFY(VM::statusText(item).contains(QStringLiteral("Interrupted")));
    item.complete = true;
    QVERIFY(VM::statusText(item).contains(QStringLiteral("Complete")));
    item.audioMissing = true;
    QVERIFY(VM::statusText(item).contains(QStringLiteral("audio is missing")));

    // The view model offers Recover only for playable recordings and
    // discards for real.
    QTemporaryDir dir;
    const QString video = touch(dir.filePath(QStringLiteral("SnapTray_Recording_e.mp4")));
    QVERIFY(Session::write(sessionFor(video)));
    Session::Recoverable broken;
    broken.info = sessionFor(video);
    VM model;
    model.setRecoveries({broken});
    QCOMPARE(model.recordings().size(), 1);
    QVERIFY(!model.recordings().first().toMap().value(QStringLiteral("canRecover")).toBool());
    QSignalSpy recover(&model, &VM::recoverRequested);
    model.recover(0);
    QCOMPARE(recover.count(), 0);
    QSignalSpy closed(&model, &VM::closeRequested);
    model.discard(0);
    QVERIFY(!QFileInfo::exists(video));
    QCOMPARE(model.count(), 0);
    QCOMPARE(closed.count(), 1);
}

void TestRecordingSession::discardRespectsTheSessionLock()
{
    // PR #172 review: another SnapTray instance may hold the recording open
    // in its preview; a stale recovery row must not delete it from under it.
    using VM = SnapTray::RecordingRecoveryViewModel;
    QTemporaryDir dir;
    const QString video = touch(dir.filePath(QStringLiteral("SnapTray_Recording_l.mp4")));
    QVERIFY(Session::write(sessionFor(video)));
    Session::Recoverable item;
    item.info = sessionFor(video);
    item.decodable = true;
    VM model;
    model.setRecoveries({item});

    auto held = std::make_unique<QLockFile>(Session::lockPathFor(video));
    held->setStaleLockTime(0);
    QVERIFY(held->tryLock(0));
    QSignalSpy discarded(&model, &VM::discarded);
    model.discard(0);
    QVERIFY(QFileInfo::exists(video));
    QVERIFY(Session::hasSession(video));
    QCOMPARE(model.count(), 1);
    QCOMPARE(discarded.count(), 0);
    QVERIFY(model.recordings().first().toMap().value(QStringLiteral("busy")).toBool());

    held.reset();
    model.discard(0);
    QVERIFY(!QFileInfo::exists(video));
    QVERIFY(!QFileInfo::exists(Session::lockPathFor(video)));
    QCOMPARE(model.count(), 0);
    QCOMPARE(discarded.count(), 1);
}

void TestRecordingSession::finishedAnimationsAreRecoverable()
{
    // PR #172 review: with preview off, a GIF/WebP recording is finished
    // before its save dialog; a crash there leaves a complete animation that
    // the video probe (AVFoundation, Media Foundation) cannot open, so it was
    // offered only for Discard. Animations are checked as images.
    QTemporaryDir dir;
    QImage a(64, 48, QImage::Format_ARGB32);
    QImage b(64, 48, QImage::Format_ARGB32);
    a.fill(Qt::red);
    b.fill(Qt::blue);
    const QString gif = dir.filePath(QStringLiteral("SnapTray_Recording_g.gif"));
    {
        NativeGifEncoder encoder;
        QVERIFY(encoder.start(gif, a.size(), 10));
        for (int i = 0; i < 6; ++i) encoder.writeFrame(i % 2 ? a : b, i * 100);
        QSignalSpy finished(&encoder, &NativeGifEncoder::finished);
        encoder.finish();
        QTRY_COMPARE(finished.count(), 1);
    }
    const QString webp = dir.filePath(QStringLiteral("SnapTray_Recording_w.webp"));
    {
        WebPAnimationEncoder encoder;
        QVERIFY(encoder.start(webp, a.size(), 10));
        for (int i = 0; i < 6; ++i) encoder.writeFrame(i % 2 ? a : b, i * 100);
        QSignalSpy finished(&encoder, &WebPAnimationEncoder::finished);
        encoder.finish();
        QTRY_COMPARE(finished.count(), 1);
    }
    // The video probe cannot open either, as on macOS and Windows.
    const Session::Probe videoProbe = [](const QString&) { return Session::ProbeResult{}; };
    const auto probe = [&videoProbe](const QString& path) { return Session::probeForRecovery(path, videoProbe); };
    for (const QString& path : {gif, webp}) {
        QVERIFY2(QFileInfo(path).size() > 0, qPrintable(path));
        auto info = sessionFor(path);
        info.format = QFileInfo(path).suffix();
        info.state = RecordingSessionInfo::State::Finished;
        info.recordedMs = 600;
        QVERIFY(Session::write(info));
    }
    // A cut-off copy (a crash while it was written) is not offered.
    const QString cut = dir.filePath(QStringLiteral("SnapTray_Recording_c.gif"));
    {
        QFile source(gif);
        QVERIFY(source.open(QIODevice::ReadOnly));
        const QByteArray bytes = source.readAll();
        touch(cut, bytes.left(bytes.size() - 20));
        auto info = sessionFor(cut);
        info.format = QStringLiteral("gif");
        info.state = RecordingSessionInfo::State::Finished;
        info.recordedMs = 600;
        QVERIFY(Session::write(info));
    }
    const auto found = Session::scan(dir.path(), probe, {});
    QCOMPARE(found.size(), 3);
    for (const auto& item : found) {
        const bool truncated = item.info.videoPath == cut;
        QCOMPARE(item.decodable, !truncated);
        if (!truncated) {
            QVERIFY(item.complete);
            QCOMPARE(item.durationMs, qint64(600));
        }
    }
}

QTEST_MAIN(TestRecordingSession)
#include "tst_RecordingSession.moc"
