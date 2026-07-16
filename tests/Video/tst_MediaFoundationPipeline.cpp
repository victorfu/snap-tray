#include <QtTest/QtTest>

#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <memory>

#include "IVideoEncoder.h"
#include "video/IVideoPlayer.h"
#include "video/VideoTrimmer.h"

class TestMediaFoundationPipeline : public QObject
{
    Q_OBJECT

private slots:
    void pausedSeekProducesFrame();
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
        frame.fill(QColor::fromHsv((i * 30) % 360, 255, 220));
        encoder->writeFrame(frame, i * 100);
        if (withAudio) {
            encoder->writeAudioSamples(audioChunk, i * 100);
        }
    }
    encoder->finish();

    if (finishedSpy.count() != 1 || !finishedSpy.first().at(0).toBool()) {
        *errorMessage = encoder->lastError();
        return false;
    }
    return QFileInfo(path).size() > 0;
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
    frameSpy.clear();
    player->seek(500);

    QTRY_VERIFY_WITH_TIMEOUT(frameSpy.count() > 0, 3000);
    QVERIFY(!frameSpy.last().at(0).value<QImage>().isNull());
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
