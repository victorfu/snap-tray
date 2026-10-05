#include "capture/PulseAudioCaptureEngine.h"
#include "encoding/FFmpegEncoder.h"
#include "video/IVideoPlayer.h"
#include "video/IVideoTranscoder.h"
#include "video/FFmpegMedia.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QtEndian>
#include <atomic>
#include <cmath>

class TestLinuxMedia : public QObject {
    Q_OBJECT
private slots:
    void captureAudio_data() {
        QTest::addColumn<int>("source");
        QTest::newRow("microphone") << int(IAudioCaptureEngine::AudioSource::Microphone);
        QTest::newRow("system") << int(IAudioCaptureEngine::AudioSource::SystemAudio);
        QTest::newRow("mixed") << int(IAudioCaptureEngine::AudioSource::Both);
    }
    void captureAudio() {
        if (qEnvironmentVariable("SNAPTRAY_TEST_ISOLATED_AUDIO") != "1") QSKIP("Requires private generated audio server");
        QFETCH(int, source);
        PulseAudioCaptureEngine engine;
        QVERIFY(engine.isAvailable());
        QVERIFY(!engine.availableInputDevices().isEmpty());
        QCOMPARE(engine.defaultInputDevice(), QString("snaptray_microphone"));
        QVERIFY(engine.setAudioSource(static_cast<IAudioCaptureEngine::AudioSource>(source)));
        std::atomic<qint64> samples{0}, loud{0}, lastEnd{-1};
        std::atomic<bool> monotonic{true};
        connect(&engine, &IAudioCaptureEngine::audioDataReady, &engine, [&](const QByteArray& pcm, qint64 start) {
            if (lastEnd > start) monotonic = false;
            lastEnd = start+pcm.size()/4;
            samples += pcm.size()/4;
            for (qsizetype i = 0; i < pcm.size(); i += 4)
                if (qAbs(int(qFromLittleEndian<qint16>(pcm.constData()+i))) > 100) ++loud;
        }, Qt::DirectConnection);
        QVERIFY(engine.start());
        QTRY_VERIFY_WITH_TIMEOUT(loud.load() > 4800, 6000);
        engine.pause(); QTest::qWait(100);
        const qint64 paused = samples;
        QTest::qWait(200); QCOMPARE(samples.load(), paused);
        engine.resume();
        QTRY_VERIFY_WITH_TIMEOUT(samples.load() > paused+4800, 3000);
        engine.stop();
        QVERIFY(monotonic);
        QVERIFY(!engine.isRunning());
        const qint64 stopped = samples;
        QTest::qWait(30); QCOMPARE(samples.load(), stopped);
    }
    void audioVideoRoundTripAndPlayback() {
        if (qEnvironmentVariable("SNAPTRAY_TEST_ISOLATED_AUDIO") != "1") QSKIP("Requires private audio server");
        QTemporaryDir dir;
        const QString path = dir.filePath("av.mp4");
        FFmpegEncoder encoder;
        encoder.setAudioFormat(48000, 2, 16);
        QVERIFY(encoder.start(path, QSize(160,120), 30));
        QVERIFY(encoder.isAudioEnabled());
        for (int i = 0; i < 60; ++i) {
            QImage image(160,120,QImage::Format_RGB32); image.fill(i < 30 ? Qt::red : Qt::blue);
            encoder.writeFrame(image, qRound(i*1000.0/30));
            QByteArray pcm(1600*4, '\0');
            for (int j = 0; j < 1600; ++j) {
                const qint16 value = qRound(12000*std::sin((i*1600+j)*2*3.141592653589793*440/48000));
                qToLittleEndian(value, pcm.data()+j*4); qToLittleEndian(value, pcm.data()+j*4+2);
            }
            encoder.writeAudioSamples(pcm, i*1600);
        }
        encoder.finish(); QVERIFY2(encoder.lastError().isEmpty(), qPrintable(encoder.lastError()));
        SnapTray::FFmpeg::AudioReader audioReader;
        QVERIFY(audioReader.load(path));
        QByteArray decoded = audioReader.read(0, 48000);
        decoded += audioReader.read(48000, 48000);
        QCOMPARE(decoded.size(), 96000*4);
        int largestJump = 0;
        for (int i = 2049; i < 94000; ++i) {
            const int previous = qFromLittleEndian<qint16>(decoded.constData()+(i-1)*4);
            const int current = qFromLittleEndian<qint16>(decoded.constData()+i*4);
            largestJump = qMax(largestJump, qAbs(current-previous));
        }
        QVERIFY2(largestJump < 3000, qPrintable(QString("PCM discontinuity: %1").arg(largestJump)));
        auto transcoder = IVideoTranscoder::create(); QVERIFY(transcoder);
        const auto probe = transcoder->probe(path); QVERIFY(probe.valid); QVERIFY(probe.hasAudio);
        QVERIFY(qAbs(probe.durationMs-2000) < 60);
        auto player = std::unique_ptr<IVideoPlayer>(IVideoPlayer::create()); QVERIFY(player);
        QSignalSpy frames(player.get(), &IVideoPlayer::frameReady);
        QSignalSpy errors(player.get(), &IVideoPlayer::error);
        QSignalSpy ended(player.get(), &IVideoPlayer::playbackFinished);
        QVERIFY(player->load(path)); QVERIFY(player->supportsAudioPlayback());
        QVERIFY(!frames.isEmpty());
        QVERIFY(qvariant_cast<QImage>(frames.last().first()).pixelColor(0,0).red() > 200);
        player->seek(1200);
        QTRY_VERIFY(qvariant_cast<QImage>(frames.last().first()).pixelColor(0,0).blue() > 200);
        player->seek(100);
        QTRY_VERIFY(qvariant_cast<QImage>(frames.last().first()).pixelColor(0,0).red() > 200);
        player->play(); QTest::qWait(150); player->pause();
        const auto position = player->position(); QTest::qWait(100); QCOMPARE(player->position(), position);
        player->setPlaybackRate(2.0f); player->setVolume(0.5f); player->setMuted(true);
        player->seek(1800); player->play();
        QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 3000);
        QCOMPARE(errors.count(), 0);
        VideoTranscodeRequest request{path, dir.filePath("crop.mp4"), 500, 1500, QRect(20,20,100,80)};
        const auto exported = transcoder->transcode(request, {});
        QVERIFY2(exported.success, qPrintable(exported.errorMessage)); QVERIFY(exported.audioCopied);
        QCOMPARE(transcoder->probe(request.outputPath).videoSize, QSize(100,80));
        // Cancellation must preserve an existing destination, including at 100%.
        QFile existing(request.outputPath); QVERIFY(existing.open(QIODevice::ReadOnly)); const QByteArray before = existing.readAll(); existing.close();
        const auto cancelled = transcoder->transcode(request, [](int percent) { return percent != 100; });
        QVERIFY(!cancelled.success);
        QVERIFY(existing.open(QIODevice::ReadOnly)); QCOMPARE(existing.readAll(), before);
    }
};
QTEST_MAIN(TestLinuxMedia)
#include "tst_LinuxMedia.moc"
