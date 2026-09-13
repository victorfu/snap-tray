#include <QtTest/QtTest>

#include <QFileInfo>
#include <QFile>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QPainter>
#include <QImageReader>
#include <webp/demux.h>

#include <memory>

#include "IVideoEncoder.h"
#include "video/IVideoPlayer.h"
#include "video/VideoTrimmer.h"

class TestMediaFoundationPipeline : public QObject
{
    Q_OBJECT

private slots:
    void reloadAndCloseStress();
    void pausedSeekProducesFrame();
    void playRestartsAfterPausedTerminalSeek_data();
    void playRestartsAfterPausedTerminalSeek();
    void terminalSeekSupersededFromFrameCallback();
    void seekSupersedesQueuedFramesAndResumes();
    void trimmedFramesMatchSource_data();
    void trimmedFramesMatchSource();
    void loopingRestartsAfterEndOfStream();
    void playRestartsAfterNonLoopingEnd();
    void audioPlaybackCapabilityIsTruthful();
    void trimCompletesFromPausedSeeks();
    void mp4TrimRejectsAudioInput();

private:
    bool createTestVideo(const QString &path, bool withAudio, QString *errorMessage);
};

bool TestMediaFoundationPipeline::createTestVideo(const QString &path,
                                                   bool withAudio,
                                                   QString *errorMessage)
{
    std::unique_ptr<IVideoEncoder> encoder(IVideoEncoder::createNativeEncoder());
    if (!encoder || !encoder->isAvailable()) {
        *errorMessage = QStringLiteral("Media Foundation encoder is unavailable");
        return false;
    }

    if (withAudio) {
        encoder->setAudioFormat(48000, 2, 16);
    }

    if (!encoder->start(path, QSize(64, 64), 10)) {
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
        QImage frame(64, 64, QImage::Format_RGB32);
        frame.fill(QColor(40, 40, 40));
        QPainter painter(&frame);
        painter.fillRect(QRect(i * 4, 20, 8, 8), Qt::white);
        painter.end();
        encoder->writeFrame(frame, i * 100);
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

void TestMediaFoundationPipeline::trimmedFramesMatchSource_data()
{
    QTest::addColumn<int>("format");
    QTest::addColumn<QString>("extension");
    QTest::newRow("MP4") << int(EncoderFactory::Format::MP4) << QString("mp4");
    QTest::newRow("GIF") << int(EncoderFactory::Format::GIF) << QString("gif");
    QTest::newRow("WebP") << int(EncoderFactory::Format::WebP) << QString("webp");
}

void TestMediaFoundationPipeline::trimmedFramesMatchSource()
{
    QFETCH(int, format);
    QFETCH(QString, extension);
    QTemporaryDir dir;
    QString error;
    const QString input = dir.filePath("input.mp4");
    const QString output = dir.filePath("output." + extension);
    if (!createTestVideo(input, false, &error)) QSKIP(qPrintable(error));
    VideoTrimmer trimmer;
    trimmer.setInputPath(input);
    trimmer.setOutputPath(output);
    trimmer.setOutputFormat(static_cast<EncoderFactory::Format>(format));
    trimmer.setTrimRange(200, 600);
    QSignalSpy finished(&trimmer, &VideoTrimmer::finished);
    QSignalSpy errors(&trimmer, &VideoTrimmer::error);
    trimmer.startTrim();
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty() || !errors.isEmpty(), 10000);
    QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first().first().toString()));
    QVERIFY(finished.first().first().toBool());

    std::unique_ptr<IVideoPlayer> player;
    std::unique_ptr<QSignalSpy> frames;
    QList<QImage> webpFrames;
    QImageReader imageReader(output);
    if (extension == "mp4") {
        player.reset(IVideoPlayer::create());
        frames = std::make_unique<QSignalSpy>(player.get(), &IVideoPlayer::frameReady);
        QVERIFY(player->load(output));
        player->pause();
    } else if (extension == "webp") {
        // Use the bundled decoder so this regression test does not depend on
        // the optional Qt WebP image plugin being installed on the machine.
        QFile file(output);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        const WebPData data{reinterpret_cast<const uint8_t*>(bytes.constData()), size_t(bytes.size())};
        WebPAnimDecoderOptions options;
        QVERIFY(WebPAnimDecoderOptionsInit(&options));
        options.color_mode = MODE_RGBA;
        std::unique_ptr<WebPAnimDecoder, decltype(&WebPAnimDecoderDelete)> decoder(
            WebPAnimDecoderNew(&data, &options), &WebPAnimDecoderDelete);
        QVERIFY(decoder);
        WebPAnimInfo info;
        QVERIFY(WebPAnimDecoderGetInfo(decoder.get(), &info));
        while (WebPAnimDecoderHasMoreFrames(decoder.get()) && webpFrames.size() < 4) {
            uint8_t* rgba = nullptr;
            int timestamp = 0;
            QVERIFY(WebPAnimDecoderGetNext(decoder.get(), &rgba, &timestamp));
            webpFrames.append(QImage(rgba, int(info.canvas_width), int(info.canvas_height),
                                    int(info.canvas_width) * 4, QImage::Format_RGBA8888).copy());
        }
        QCOMPARE(webpFrames.size(), 4);
    } else if (!imageReader.canRead()) {
        QSKIP(qPrintable("Image decoder unavailable: " + imageReader.errorString()));
    }
    for (int index = 0; index < 4; ++index) {
        QImage frame;
        if (player) {
            frames->clear();
            player->seek(index * 100);
            QTRY_VERIFY_WITH_TIMEOUT(!frames->isEmpty(), 3000);
            frame = frames->last().first().value<QImage>();
        } else if (extension == "webp") {
            frame = webpFrames.at(index);
        } else {
            frame = imageReader.read();
        }
        QVERIFY2(!frame.isNull(), qPrintable(imageReader.errorString()));
        const int expectedX = (index + 2) * 4;
        QVERIFY2(frame.pixelColor(expectedX + 3, 23).red() > 170,
                 qPrintable(QString("Wrong source content in output frame %1").arg(index)));
        QVERIFY(frame.pixelColor(2, 23).red() < 110);
    }
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

void TestMediaFoundationPipeline::trimCompletesFromPausedSeeks()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString inputPath = dir.filePath(QStringLiteral("trim-input.mp4"));
    const QString outputPath = dir.filePath(QStringLiteral("trim-output.gif"));
    QString createError;
    if (!createTestVideo(inputPath, false, &createError)) {
        QSKIP(qPrintable(createError));
    }

    VideoTrimmer trimmer;
    trimmer.setInputPath(inputPath);
    trimmer.setOutputPath(outputPath);
    trimmer.setOutputFormat(EncoderFactory::Format::GIF);
    trimmer.setTrimRange(200, 600);

    QSignalSpy finishedSpy(&trimmer, &VideoTrimmer::finished);
    QSignalSpy errorSpy(&trimmer, &VideoTrimmer::error);
    trimmer.startTrim();

    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0 || errorSpy.count() > 0, 10000);
    QCOMPARE(errorSpy.count(), 0);
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(finishedSpy.first().at(0).toBool());
    QVERIFY(QFileInfo(outputPath).size() > 0);
}

void TestMediaFoundationPipeline::mp4TrimRejectsAudioInput()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString inputPath = dir.filePath(QStringLiteral("audio-input.mp4"));
    const QString outputPath = dir.filePath(QStringLiteral("audio-trimmed.mp4"));
    QString createError;
    if (!createTestVideo(inputPath, true, &createError)) {
        QSKIP(qPrintable(createError));
    }

    {
        std::unique_ptr<IVideoPlayer> player(IVideoPlayer::create());
        QVERIFY(player != nullptr);
        QVERIFY(player->load(inputPath));
        QVERIFY(player->hasAudio());
    }

    VideoTrimmer trimmer;
    trimmer.setInputPath(inputPath);
    trimmer.setOutputPath(outputPath);
    trimmer.setOutputFormat(EncoderFactory::Format::MP4);
    trimmer.setTrimRange(100, 700);

    QSignalSpy errorSpy(&trimmer, &VideoTrimmer::error);
    QSignalSpy finishedSpy(&trimmer, &VideoTrimmer::finished);
    trimmer.startTrim();

    QTRY_COMPARE_WITH_TIMEOUT(errorSpy.count(), 1, 3000);
    QVERIFY(errorSpy.first().at(0).toString().contains(QStringLiteral("audio"),
                                                       Qt::CaseInsensitive));
    QCOMPARE(finishedSpy.count(), 0);
    QVERIFY(!trimmer.isRunning());
    QVERIFY(QFileInfo::exists(inputPath));
    QVERIFY(!QFileInfo::exists(outputPath));
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
