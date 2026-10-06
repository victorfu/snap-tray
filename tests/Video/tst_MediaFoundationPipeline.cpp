#include <QtTest/QtTest>

#include <QFileInfo>
#include <QFile>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QPainter>

#include <memory>

#include "IVideoEncoder.h"
#include "video/IVideoPlayer.h"

class TestMediaFoundationPipeline : public QObject
{
    Q_OBJECT

private slots:
    void reloadAndCloseStress();
    void pausedSeekProducesFrame();
    void stepForwardFollowsDecodedFrames_data();
    void stepForwardFollowsDecodedFrames();
    void stepForwardAcrossTimestampGap();
    void queuedStepForward_data();
    void queuedStepForward();
    void queuedStepsStopAtEnd();
    void queuedStepsSuperseded_data();
    void queuedStepsSuperseded();
    void seekSupersedesStepForward();
    void playRestartsAfterPausedTerminalSeek_data();
    void playRestartsAfterPausedTerminalSeek();
    void terminalSeekSupersededFromFrameCallback();
    void seekSupersedesQueuedFramesAndResumes();
    void loopingRestartsAfterEndOfStream();
    void playRestartsAfterNonLoopingEnd();
    void audioPlaybackCapabilityIsTruthful();
    void decodedFramesUseDisplayedSize();

private:
    bool createTestVideo(const QString &path, bool withAudio, QString *errorMessage,
                         int frameRate = 10, const QSize &frameSize = QSize(64, 64));
};

bool TestMediaFoundationPipeline::createTestVideo(const QString &path,
                                                   bool withAudio,
                                                   QString *errorMessage,
                                                   int frameRate,
                                                   const QSize &frameSize)
{
    std::unique_ptr<IVideoEncoder> encoder(IVideoEncoder::createNativeEncoder());
    if (!encoder || !encoder->isAvailable()) {
        *errorMessage = QStringLiteral("Media Foundation encoder is unavailable");
        return false;
    }

    if (withAudio) {
        encoder->setAudioFormat(48000, 2, 16);
    }

    if (!encoder->start(path, frameSize, frameRate)) {
        *errorMessage = encoder->lastError();
        return false;
    }

    if (withAudio && !encoder->isAudioEnabled()) {
        encoder->abort();
        *errorMessage = QStringLiteral("Media Foundation AAC encoding is unavailable");
        return false;
    }

    QSignalSpy finishedSpy(encoder.get(), &IVideoEncoder::finished);
    const QByteArray audioChunk(4800 * 2 * 2, '\0'); // 100 ms, 48 kHz stereo PCM16
    for (int i = 0; i < 10; ++i) {
        QImage frame(frameSize, QImage::Format_RGB32);
        frame.fill(QColor(40, 40, 40));
        QPainter painter(&frame);
        painter.fillRect(QRect(i * 4, 20, 8, 8), Qt::white);
        painter.end();
        encoder->writeFrame(frame, qRound64(i * 1000.0 / frameRate));
        if (withAudio) {
            encoder->writeAudioSamples(audioChunk, i * 4800LL);
        }
    }
    encoder->finish();

    if (finishedSpy.count() != 1 || !finishedSpy.first().at(0).toBool()) {
        *errorMessage = encoder->lastError();
        return false;
    }
    return QFileInfo(path).size() > 0;
}

void TestMediaFoundationPipeline::audioPlaybackCapabilityIsTruthful()
{
    std::unique_ptr<IVideoPlayer> player(IVideoPlayer::create());
    QVERIFY(player != nullptr);
    QVERIFY(!player->supportsAudioPlayback());

    player->setMuted(true);
    player->setVolume(0.0f);
    QVERIFY(!player->isMuted());
    QCOMPARE(player->volume(), 1.0f);
}

// H.264 codes frames in 16-row macroblocks, so a 120-row video is decoded
// into a 128-row buffer with a display aperture. The player must report and
// deliver the displayed 160x120 (the size the crop editor and the transcoder
// work in), never the padded buffer.
void TestMediaFoundationPipeline::decodedFramesUseDisplayedSize()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString inputPath = dir.filePath(QStringLiteral("padded.mp4"));
    QString createError;
    if (!createTestVideo(inputPath, false, &createError, 10, QSize(160, 120))) {
        QSKIP(qPrintable(createError));
    }

    std::unique_ptr<IVideoPlayer> player(IVideoPlayer::create());
    QVERIFY(player != nullptr);
    QSignalSpy frames(player.get(), &IVideoPlayer::frameReady);
    QVERIFY(player->load(inputPath));
    QCOMPARE(player->videoSize(), QSize(160, 120));
    player->pause();
    player->seek(0);
    QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 1, 3000);
    const QImage frame = frames.first().first().value<QImage>();
    QCOMPARE(frame.size(), QSize(160, 120));
    // Marker and background are picture; the bottom row is not padding.
    QVERIFY(frame.pixelColor(3, 23).red() > 180);
    const QColor bottom = frame.pixelColor(80, 119);
    QVERIFY2(qAbs(bottom.red() - 40) < 30 && qAbs(bottom.green() - 40) < 30 && qAbs(bottom.blue() - 40) < 30,
             qPrintable(bottom.name()));
}

void TestMediaFoundationPipeline::pausedSeekProducesFrame()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString inputPath = dir.filePath(QStringLiteral("seek.mp4"));
    QString createError;
    if (!createTestVideo(inputPath, false, &createError)) {
        QSKIP(qPrintable(createError));
    }

    std::unique_ptr<IVideoPlayer> player(IVideoPlayer::create());
    QVERIFY(player != nullptr);
    QSignalSpy loadedSpy(player.get(), &IVideoPlayer::mediaLoaded);
    QSignalSpy frameSpy(player.get(), &IVideoPlayer::frameReady);

    QVERIFY(player->load(inputPath));
    QCOMPARE(loadedSpy.count(), 1);
    player->pause();

    const qint64 seekPositions[] = { 200, 550, 800, 999, 1000, 0 };
    for (qint64 positionMs : seekPositions) {
        frameSpy.clear();
        player->seek(positionMs);

        QTRY_VERIFY_WITH_TIMEOUT(frameSpy.count() > 0, 3000);
        QVERIFY(!frameSpy.last().at(0).value<QImage>().isNull());
        const qint64 expectedTime = qMin<qint64>(900, (positionMs / 100) * 100);
        QCOMPARE(player->position(), expectedTime);
        const QImage frame = frameSpy.last().at(0).value<QImage>();
        const int expectedX = int(expectedTime / 100) * 4;
        QVERIFY(frame.pixelColor(expectedX + 3, 23).red() > 180);
        if (expectedX >= 8) {
            QVERIFY(frame.pixelColor(2, 23).red() < 100);
        }
        QCOMPARE(player->state(), IVideoPlayer::State::Paused);
    }
}

void TestMediaFoundationPipeline::stepForwardFollowsDecodedFrames_data()
{
    QTest::addColumn<int>("frameRate");
    for (int fps : {10, 15, 24, 30, 60}) {
        QTest::newRow(qPrintable(QString::number(fps) + "fps")) << fps;
    }
}

void TestMediaFoundationPipeline::stepForwardFollowsDecodedFrames()
{
    QFETCH(int, frameRate);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString input = dir.filePath("stepping.mp4");
    QString createError;
    if (!createTestVideo(input, false, &createError, frameRate)) {
        QSKIP(qPrintable(createError));
    }

    std::unique_ptr<IVideoPlayer> player(IVideoPlayer::create());
    QVERIFY(player != nullptr);
    QSignalSpy frames(player.get(), &IVideoPlayer::frameReady);
    QSignalSpy finished(player.get(), &IVideoPlayer::playbackFinished);
    QSignalSpy errors(player.get(), &IVideoPlayer::error);
    QVERIFY(player->load(input));
    player->seek(0);
    QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 1, 3000);

    // Compare stepping against sequential decoding, not nominal FPS arithmetic:
    // native PTS can include fractional milliseconds and irregular intervals.
    QList<qint64> positions{player->position()};
    QList<QImage> images{frames.first().first().value<QImage>()};
    const auto collect = connect(player.get(), &IVideoPlayer::frameReady, player.get(),
                                 [&](const QImage& image) {
        positions.append(player->position());
        images.append(image);
    });
    player->play();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
    disconnect(collect);
    QCOMPARE(positions.size(), 10);

    player->pause();
    frames.clear();
    player->seek(0);
    QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 1, 3000);
    finished.clear();
    QSignalSpy positionChanges(player.get(), &IVideoPlayer::positionChanged);
    for (int i = 1; i < positions.size(); ++i) {
        frames.clear();
        positionChanges.clear();
        player->stepForward();
        QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 1, 3000);
        QCOMPARE(player->position(), positions.at(i));
        QCOMPARE(frames.first().first().value<QImage>(), images.at(i));
        QCOMPARE(player->state(), IVideoPlayer::State::Paused);
        QVERIFY(!positionChanges.isEmpty());
        QCOMPARE(positionChanges.last().first().toLongLong(), positions.at(i));
        QTest::qWait(30);
        QCOMPARE(frames.count(), 1);
    }

    // At EOF, stepping keeps the final image, even with looping enabled.
    player->setLooping(true);
    auto* reader = player->findChild<QThread*>();
    QVERIFY(reader != nullptr);
    QSignalSpy endOfStream(reader, SIGNAL(endOfStream(quint64)));
    QVERIFY(endOfStream.isValid());
    for (int i = 0; i < 2; ++i) {
        frames.clear();
        endOfStream.clear();
        player->stepForward();
        QTRY_COMPARE_WITH_TIMEOUT(endOfStream.count(), 1, 3000);
        QCoreApplication::sendPostedEvents(player.get(), QEvent::MetaCall);
        QCOMPARE(frames.count(), 1);
        QCOMPARE(player->position(), positions.last());
        QCOMPARE(frames.first().first().value<QImage>(), images.last());
        QCOMPARE(player->state(), IVideoPlayer::State::Paused);
        QVERIFY(finished.isEmpty());
    }
    QVERIFY(errors.isEmpty());

    // Playback still restarts after a terminal step.
    frames.clear();
    qint64 firstResumedPosition = -1;
    connect(player.get(), &IVideoPlayer::frameReady, player.get(), [&] {
        if (firstResumedPosition < 0) firstResumedPosition = player->position();
        player->pause();
    });
    player->play();
    QTRY_VERIFY_WITH_TIMEOUT(!frames.isEmpty(), 3000);
    QCOMPARE(firstResumedPosition, positions.first());
}

void TestMediaFoundationPipeline::queuedStepForward_data()
{
    QTest::addColumn<qint64>("startPosition");
    QTest::addColumn<bool>("delayDelivery");
    QTest::newRow("back-to-back") << qint64(0) << false;
    QTest::newRow("decoded-frame-pending") << qint64(0) << true;
    QTest::newRow("timestamp-gap") << qint64(1000) << false;
    QTest::newRow("timestamp-gap-pending") << qint64(1000) << true;
}

void TestMediaFoundationPipeline::queuedStepForward()
{
    QFETCH(qint64, startPosition);
    QFETCH(bool, delayDelivery);
    const QString input = QFINDTESTDATA("fixtures/frame-gap.mp4");
    QVERIFY(!input.isEmpty());
    std::unique_ptr<IVideoPlayer> player(IVideoPlayer::create());
    QVERIFY(player != nullptr);
    QSignalSpy frames(player.get(), &IVideoPlayer::frameReady);
    QSignalSpy errors(player.get(), &IVideoPlayer::error);
    QVERIFY(player->load(input));
    player->pause();
    player->seek(startPosition);
    QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 1, 3000);

    QList<qint64> positions;
    connect(player.get(), &IVideoPlayer::frameReady, player.get(), [&] {
        positions.append(player->position());
    });
    player->stepForward();
    if (delayDelivery) {
        // A decoded frame waiting in Qt's queue must still count as in flight.
        QThread::msleep(150);
    }
    player->stepForward();
    player->stepForward();
    QTRY_COMPARE_WITH_TIMEOUT(positions.size(), 3, 3000);
    const QList<qint64> expected = startPosition == 0
        ? QList<qint64>{100, 200, 300} : QList<qint64>{1200, 1300, 1400};
    QCOMPARE(positions, expected);
    QCOMPARE(player->state(), IVideoPlayer::State::Paused);
    QTest::qWait(50);
    QCOMPARE(positions, expected);

    player->stepForward();
    QTRY_COMPARE_WITH_TIMEOUT(positions.size(), 4, 3000);
    QCOMPARE(positions.last(), expected.last() + 100);
    QVERIFY(errors.isEmpty());
}

void TestMediaFoundationPipeline::queuedStepsStopAtEnd()
{
    const QString input = QFINDTESTDATA("fixtures/frame-gap.mp4");
    QVERIFY(!input.isEmpty());
    std::unique_ptr<IVideoPlayer> player(IVideoPlayer::create());
    QVERIFY(player != nullptr);
    QSignalSpy frames(player.get(), &IVideoPlayer::frameReady);
    QSignalSpy finished(player.get(), &IVideoPlayer::playbackFinished);
    QVERIFY(player->load(input));
    player->pause();
    player->setLooping(true);
    player->seek(1900);
    QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 1, 3000);
    auto* reader = player->findChild<QThread*>();
    QVERIFY(reader != nullptr);
    QSignalSpy endOfStream(reader, SIGNAL(endOfStream(quint64)));
    QVERIFY(endOfStream.isValid());

    frames.clear();
    for (int i = 0; i < 5; ++i) player->stepForward();
    QTRY_COMPARE_WITH_TIMEOUT(endOfStream.count(), 1, 3000);
    QCoreApplication::sendPostedEvents(player.get(), QEvent::MetaCall);
    QCOMPARE(player->position(), qint64(2000));
    QCOMPARE(player->state(), IVideoPlayer::State::Paused);
    // One next frame and one terminal fallback, regardless of the extra presses.
    QCOMPARE(frames.count(), 2);
    QTest::qWait(50);
    QCOMPARE(frames.count(), 2);
    QCOMPARE(endOfStream.count(), 1);
    QVERIFY(finished.isEmpty());

    frames.clear();
    player->seek(0);
    QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 1, 3000);
    frames.clear();
    player->stepForward();
    QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 1, 3000);
    QCOMPARE(player->position(), qint64(100));
}

void TestMediaFoundationPipeline::queuedStepsSuperseded_data()
{
    QTest::addColumn<QString>("action");
    QTest::addColumn<bool>("fromFrameCallback");
    for (const QString& action : {QStringLiteral("seek"), QStringLiteral("stop"),
                                  QStringLiteral("reload")}) {
        QTest::addRow("%s-pending", qPrintable(action)) << action << false;
        QTest::addRow("%s-from-frame", qPrintable(action)) << action << true;
    }
}

void TestMediaFoundationPipeline::queuedStepsSuperseded()
{
    QFETCH(QString, action);
    QFETCH(bool, fromFrameCallback);
    const QString input = QFINDTESTDATA("fixtures/frame-gap.mp4");
    QVERIFY(!input.isEmpty());
    std::unique_ptr<IVideoPlayer> player(IVideoPlayer::create());
    QVERIFY(player != nullptr);
    QSignalSpy frames(player.get(), &IVideoPlayer::frameReady);
    QVERIFY(player->load(input));
    player->pause();
    player->seek(0);
    QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 1, 3000);
    frames.clear();

    const auto supersede = [&] {
        if (action == QStringLiteral("seek")) player->seek(550);
        else if (action == QStringLiteral("stop")) player->stop();
        else QVERIFY(player->load(input));
    };
    bool superseded = false;
    if (fromFrameCallback) {
        connect(player.get(), &IVideoPlayer::frameReady, player.get(), [&] {
            if (!superseded) {
                superseded = true;
                supersede();
            }
        });
    }
    for (int i = 0; i < 3; ++i) player->stepForward();
    if (!fromFrameCallback) {
        QThread::msleep(150);
        supersede();
    }
    const int expectedFrames = fromFrameCallback ? 2 : 1;
    QTRY_COMPARE_WITH_TIMEOUT(frames.count(), expectedFrames, 3000);
    const qint64 targetPosition = action == QStringLiteral("seek") ? 500 : 0;
    QCOMPARE(player->position(), targetPosition);
    QTest::qWait(100);
    QCOMPARE(frames.count(), expectedFrames);

    frames.clear();
    player->stepForward();
    QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 1, 3000);
    QCOMPARE(player->position(), targetPosition + 100);
    QTest::qWait(50);
    QCOMPARE(frames.count(), 1);
}

void TestMediaFoundationPipeline::seekSupersedesStepForward()
{
    QTemporaryDir dir;
    const QString input = dir.filePath("superseded-step.mp4");
    QString createError;
    if (!createTestVideo(input, false, &createError)) QSKIP(qPrintable(createError));
    std::unique_ptr<IVideoPlayer> player(IVideoPlayer::create());
    QVERIFY(player != nullptr);
    QSignalSpy frames(player.get(), &IVideoPlayer::frameReady);
    QVERIFY(player->load(input));
    player->seek(200);
    QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 1, 3000);

    frames.clear();
    player->stepForward();
    // Let the step reach Qt's event queue without delivering its callback.
    QThread::msleep(150);
    player->seek(550);
    QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 1, 3000);
    QCOMPARE(player->position(), qint64(500));
    QTest::qWait(150);
    QCOMPARE(frames.count(), 1);

    frames.clear();
    player->stepForward();
    QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 1, 3000);
    QCOMPARE(player->position(), qint64(600));
    QCOMPARE(player->state(), IVideoPlayer::State::Paused);

    frames.clear();
    player->play();
    QThread::msleep(150);
    player->stepForward();
    QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 1, 3000);
    QCOMPARE(player->position(), qint64(700));
    QCOMPARE(player->state(), IVideoPlayer::State::Paused);
    QTest::qWait(150);
    QCOMPARE(frames.count(), 1);
}

void TestMediaFoundationPipeline::stepForwardAcrossTimestampGap()
{
    const QString input = QFINDTESTDATA("fixtures/frame-gap.mp4");
    QVERIFY(!input.isEmpty());
    std::unique_ptr<IVideoPlayer> player(IVideoPlayer::create());
    QVERIFY(player != nullptr);
    QSignalSpy frames(player.get(), &IVideoPlayer::frameReady);
    QVERIFY(player->load(input));
    // MF may infer an average FPS from the track duration. Its nominal
    // interval still cannot cross this 200 ms gap in one time-based step.
    QVERIFY(player->frameIntervalMs() > 0);
    QVERIFY(player->frameIntervalMs() < 200);
    player->pause();

    // This fixture has no frame at 1100 ms. Time seeks must still round down.
    player->seek(1100);
    QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 1, 3000);
    QCOMPARE(player->position(), qint64(1000));
    QVERIFY(frames.first().first().value<QImage>().pixelColor(32, 32).red() > 180);

    frames.clear();
    player->stepForward();
    QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 1, 3000);
    QCOMPARE(player->position(), qint64(1200));
    QVERIFY(frames.first().first().value<QImage>().pixelColor(32, 32).green() > 100);
    QCOMPARE(player->state(), IVideoPlayer::State::Paused);

    frames.clear();
    player->stepForward();
    QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 1, 3000);
    QCOMPARE(player->position(), qint64(1300));
}

void TestMediaFoundationPipeline::playRestartsAfterPausedTerminalSeek_data()
{
    QTest::addColumn<qint64>("seekPosition");
    QTest::addColumn<bool>("looping");
    QTest::newRow("final-interval") << qint64(950) << false;
    QTest::newRow("near-end") << qint64(999) << false;
    QTest::newRow("duration") << qint64(1000) << false;
    QTest::newRow("final-interval-looping") << qint64(950) << true;
    QTest::newRow("near-end-looping") << qint64(999) << true;
    QTest::newRow("duration-looping") << qint64(1000) << true;
}

void TestMediaFoundationPipeline::playRestartsAfterPausedTerminalSeek()
{
    QFETCH(qint64, seekPosition);
    QFETCH(bool, looping);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString input = dir.filePath("terminal-seek.mp4");
    QString error;
    if (!createTestVideo(input, false, &error)) QSKIP(qPrintable(error));

    std::unique_ptr<IVideoPlayer> player(IVideoPlayer::create());
    QVERIFY(player != nullptr);
    QVERIFY(player->load(input));
    QCOMPARE(player->duration(), qint64(1000));
    player->setLooping(looping);
    player->pause();

    auto* reader = player->findChild<QThread*>();
    QVERIFY(reader != nullptr);
    QSignalSpy endSpy(reader, SIGNAL(endOfStream(quint64)));
    QVERIFY(endSpy.isValid());
    QSignalSpy frames(player.get(), &IVideoPlayer::frameReady);
    QSignalSpy finished(player.get(), &IVideoPlayer::playbackFinished);
    QSignalSpy errors(player.get(), &IVideoPlayer::error);
    player->seek(seekPosition);

    // Wait for native EOF, then dispatch the player's queued frame/EOS calls.
    // Receiving the final frame alone does not guarantee EOF was handled yet.
    QTRY_COMPARE_WITH_TIMEOUT(endSpy.count(), 1, 3000);
    QCoreApplication::sendPostedEvents(player.get(), QEvent::MetaCall);
    QCOMPARE(frames.count(), 1);
    QVERIFY(!frames.first().first().value<QImage>().isNull());
    QCOMPARE(player->position(), qint64(900));
    QCOMPARE(player->state(), IVideoPlayer::State::Paused);
    QCOMPARE(finished.count(), 0);

    QList<qint64> resumedPositions;
    connect(player.get(), &IVideoPlayer::frameReady, player.get(), [&] {
        resumedPositions.append(player->position());
    });
    player->play();
    QCOMPARE(player->state(), IVideoPlayer::State::Playing);
    QTRY_VERIFY_WITH_TIMEOUT(resumedPositions.size() >= 2, 3000);
    QCOMPARE(resumedPositions.at(0), qint64(0));
    QCOMPARE(resumedPositions.at(1), qint64(100));

    if (looping) {
        QTRY_VERIFY_WITH_TIMEOUT(resumedPositions.count(100) >= 2, 4000);
        QCOMPARE(player->state(), IVideoPlayer::State::Playing);
        QCOMPARE(finished.count(), 0);
        player->pause();
    } else {
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 4000);
        QCOMPARE(player->state(), IVideoPlayer::State::Stopped);
        QCOMPARE(player->position(), player->duration());
    }
    QVERIFY(errors.isEmpty());
}

void TestMediaFoundationPipeline::terminalSeekSupersededFromFrameCallback()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString input = dir.filePath("superseded-terminal-seek.mp4");
    QString error;
    if (!createTestVideo(input, false, &error)) QSKIP(qPrintable(error));

    std::unique_ptr<IVideoPlayer> player(IVideoPlayer::create());
    QVERIFY(player != nullptr);
    QVERIFY(player->load(input));
    player->pause();
    QSignalSpy finished(player.get(), &IVideoPlayer::playbackFinished);
    QList<qint64> positions;
    connect(player.get(), &IVideoPlayer::frameReady, player.get(), [&] {
        positions.append(player->position());
        if (positions.size() == 1) {
            // Supersede the terminal seek before its queued EOS is dispatched.
            player->seek(550);
        }
    });
    player->seek(950);
    QTRY_COMPARE_WITH_TIMEOUT(positions.size(), 2, 3000);
    QCOMPARE(positions.at(0), qint64(900));
    QCOMPARE(positions.at(1), qint64(500));
    QCOMPARE(player->state(), IVideoPlayer::State::Paused);
    QCOMPARE(finished.count(), 0);

    player->play();
    QTRY_VERIFY_WITH_TIMEOUT(positions.size() >= 3, 3000);
    QCOMPARE(positions.at(2), qint64(600));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 4000);
    QCOMPARE(player->state(), IVideoPlayer::State::Stopped);
}

void TestMediaFoundationPipeline::seekSupersedesQueuedFramesAndResumes()
{
    QTemporaryDir dir;
    QString error;
    const QString input = dir.filePath("queued-seek.mp4");
    if (!createTestVideo(input, false, &error)) QSKIP(qPrintable(error));
    std::unique_ptr<IVideoPlayer> player(IVideoPlayer::create());
    QVERIFY(player->load(input));
    player->pause();
    // Let the old preview reach Qt's event queue without dispatching it.
    QThread::msleep(150);
    QSignalSpy frames(player.get(), &IVideoPlayer::frameReady);
    player->seek(550);
    QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 1, 3000);
    QCOMPARE(player->position(), qint64(500));
    QTest::qWait(150);
    QCOMPARE(frames.count(), 1);
    frames.clear();
    qint64 firstResumedPosition = -1;
    connect(player.get(), &IVideoPlayer::frameReady, player.get(), [&] {
        if (firstResumedPosition < 0) firstResumedPosition = player->position();
    });
    player->play();
    QTRY_VERIFY_WITH_TIMEOUT(!frames.isEmpty(), 3000);
    QCOMPARE(firstResumedPosition, qint64(600));
}

void TestMediaFoundationPipeline::loopingRestartsAfterEndOfStream()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString inputPath = dir.filePath(QStringLiteral("loop.mp4"));
    QString createError;
    if (!createTestVideo(inputPath, false, &createError)) {
        QSKIP(qPrintable(createError));
    }

    std::unique_ptr<IVideoPlayer> player(IVideoPlayer::create());
    QVERIFY(player != nullptr);
    QVERIFY(player->load(inputPath));

    const qint64 lateFrameThreshold = qMax<qint64>(200, player->duration() / 2);
    qint64 previousPosition = -1;
    bool sawLateFrame = false;
    bool sawWrappedFrame = false;
    bool sawFrameAfterWrap = false;
    connect(player.get(), &IVideoPlayer::frameReady, player.get(),
            [&](const QImage &frame) {
        QVERIFY(!frame.isNull());
        const qint64 position = player->position();
        if (position >= lateFrameThreshold) {
            sawLateFrame = true;
        }
        if (sawLateFrame && previousPosition >= lateFrameThreshold
            && position < previousPosition) {
            sawWrappedFrame = true;
        } else if (sawWrappedFrame && position > 0) {
            sawFrameAfterWrap = true;
        }
        previousPosition = position;
    });

    QSignalSpy finishedSpy(player.get(), &IVideoPlayer::playbackFinished);
    player->setLooping(true);
    player->play();

    QTRY_VERIFY_WITH_TIMEOUT(sawFrameAfterWrap, 6000);
    QCOMPARE(finishedSpy.count(), 0);
    QCOMPARE(player->state(), IVideoPlayer::State::Playing);
}

void TestMediaFoundationPipeline::playRestartsAfterNonLoopingEnd()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString inputPath = dir.filePath(QStringLiteral("replay.mp4"));
    QString createError;
    if (!createTestVideo(inputPath, false, &createError)) {
        QSKIP(qPrintable(createError));
    }

    std::unique_ptr<IVideoPlayer> player(IVideoPlayer::create());
    QVERIFY(player != nullptr);
    QSignalSpy frameSpy(player.get(), &IVideoPlayer::frameReady);
    QSignalSpy finishedSpy(player.get(), &IVideoPlayer::playbackFinished);
    QVERIFY(player->load(inputPath));
    frameSpy.clear();

    player->play();
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 4000);
    QCOMPARE(player->state(), IVideoPlayer::State::Stopped);

    frameSpy.clear();
    player->play();
    QCOMPARE(player->state(), IVideoPlayer::State::Playing);
    QCOMPARE(player->position(), qint64(0));
    QTRY_VERIFY_WITH_TIMEOUT(frameSpy.count() > 0, 3000);
    QVERIFY(!frameSpy.first().at(0).value<QImage>().isNull());
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 2, 4000);
    QCOMPARE(player->state(), IVideoPlayer::State::Stopped);
}

QTEST_MAIN(TestMediaFoundationPipeline)
#include "tst_MediaFoundationPipeline.moc"

void TestMediaFoundationPipeline::reloadAndCloseStress()
{
    QTemporaryDir dir;
    const QString input = dir.filePath("shutdown.mp4");
    QString error;
    if (!createTestVideo(input, false, &error)) QSKIP(qPrintable(error));
    const QString invalid = dir.filePath("truncated.mp4");
    QFile corrupt(invalid);
    QVERIFY(corrupt.open(QIODevice::WriteOnly));
    QVERIFY(corrupt.write("Truncated recording") > 0);
    corrupt.close();
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < 30; ++i) {
        std::unique_ptr<IVideoPlayer> player(IVideoPlayer::create());
        QVERIFY(player->load(input));
        if (i % 2) player->play();
        player->seek((i % 8) * 100);
        QVERIFY(!player->load(invalid));
        QVERIFY(player->load(input));
        player->seek(500);
        player.reset(); // May still be awaiting its first async decoder sample.
        QCoreApplication::processEvents();
    }
    QVERIFY(timer.elapsed() < 15000);
}
