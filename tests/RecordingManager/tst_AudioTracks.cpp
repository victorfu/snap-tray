#include <QtTest>

#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QtEndian>

#include <cmath>

#include "recording/AudioTracks.h"

using namespace SnapTray;

namespace {

QByteArray tone(int frames, double frequency, double amplitude, qint64 firstFrame = 0)
{
    QByteArray pcm(frames * AudioTracks::kBytesPerFrame, '\0');
    auto* out = reinterpret_cast<qint16*>(pcm.data());
    for (int f = 0; f < frames; ++f) {
        const double t = double(firstFrame + f) / AudioTracks::kSampleRate;
        const auto value = qint16(std::lround(amplitude * 32767.0 * std::sin(2.0 * 3.14159265358979 * frequency * t)));
        qToLittleEndian<qint16>(value, out + 2 * f);
        qToLittleEndian<qint16>(value, out + 2 * f + 1);
    }
    return pcm;
}

double rmsDb(const QByteArray& pcm)
{
    const auto* in = reinterpret_cast<const qint16*>(pcm.constData());
    double sum = 0.0;
    const qsizetype n = pcm.size() / 2;
    for (qsizetype i = 0; i < n; ++i) {
        const double v = qFromLittleEndian<qint16>(in + i) / 32768.0;
        sum += v * v;
    }
    return 10.0 * std::log10(std::max(1e-12, sum / std::max<qsizetype>(1, n)));
}

qint16 sampleAt(const QByteArray& pcm, int frame, int channel = 0)
{
    return qFromLittleEndian<qint16>(reinterpret_cast<const qint16*>(pcm.constData()) + 2 * frame + channel);
}

void writeTrack(const QString& path, const QByteArray& pcm)
{
    AudioTrackWriter writer(path);
    QVERIFY(writer.open());
    writer.write(pcm, 0);
    writer.close();
}

} // namespace

class TestAudioTracks : public QObject
{
    Q_OBJECT

private slots:
    void writerPlacesChunksOnTheTimeline();
    void readerValidatesAndPadsWithSilence();
    void neutralMixIsTheRecordedMix();
    void gainsAndMutes();
    void noiseReductionQuietsNoiseAndKeepsSpeech();
    void noiseReductionCost();
    void settingsJson();
    void pathsAndRemoval();
};

void TestAudioTracks::writerPlacesChunksOnTheTimeline()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("v.mp4.mic.pcm"));
    AudioTrackWriter writer(path);
    QVERIFY(writer.open());
    writer.write(tone(480, 440, 0.5, 0), 0);
    // A gap (the mixer skipped a long silence): filled with zeros, not garbage.
    writer.write(tone(480, 440, 0.5, 48000), 48000);
    writer.close();
    QVERIFY(!writer.failed());
    AudioTrackReader reader;
    QVERIFY(reader.open(path));
    QCOMPARE(reader.frames(), 48480);
    QCOMPARE(reader.read(0, 480), tone(480, 440, 0.5, 0));
    QCOMPARE(reader.read(48000, 480), tone(480, 440, 0.5, 48000));
    const QByteArray gap = reader.read(1000, 1000);
    QCOMPARE(gap, QByteArray(gap.size(), '\0'));
}

void TestAudioTracks::readerValidatesAndPadsWithSilence()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("t.pcm"));
    writeTrack(path, tone(100, 440, 0.5));
    AudioTrackReader reader;
    QVERIFY(reader.open(path));
    // Before the start and past the end: silence, never a short read.
    const QByteArray around = reader.read(-50, 200);
    QCOMPARE(around.size(), 200 * AudioTracks::kBytesPerFrame);
    QCOMPARE(sampleAt(around, 0), qint16(0));
    QCOMPARE(sampleAt(around, 50 + 10), sampleAt(tone(100, 440, 0.5), 10));
    QCOMPARE(sampleAt(around, 199), qint16(0));
    // Not a track file.
    QFile other(dir.filePath(QStringLiteral("x.pcm")));
    QVERIFY(other.open(QIODevice::WriteOnly));
    other.write("not a track at all");
    other.close();
    AudioTrackReader bad;
    QVERIFY(!bad.open(other.fileName()));
}

void TestAudioTracks::neutralMixIsTheRecordedMix()
{
    QTemporaryDir dir;
    const QString video = dir.filePath(QStringLiteral("v.mp4"));
    const QByteArray mic = tone(4800, 440, 0.7);
    const QByteArray sys = tone(4800, 660, 0.7);
    writeTrack(AudioTracks::pathFor(video, AudioTracks::Track::Microphone), mic);
    writeTrack(AudioTracks::pathFor(video, AudioTracks::Track::SystemAudio), sys);
    QVERIFY(AudioTracks::exist(video));
    AudioTrackMixer mixer(video, {});
    QVERIFY(mixer.isValid());
    QVERIFY(AudioMixSettings().isNeutral());
    const QByteArray mixed = mixer.read(0, 4800);
    for (int f = 0; f < 4800; f += 37) {
        const int expected = std::clamp(int(sampleAt(mic, f)) + int(sampleAt(sys, f)), -32768, 32767);
        QCOMPARE(int(sampleAt(mixed, f)), expected); // what the recorder's mixer wrote
    }
}

void TestAudioTracks::gainsAndMutes()
{
    QTemporaryDir dir;
    const QString video = dir.filePath(QStringLiteral("v.mp4"));
    writeTrack(AudioTracks::pathFor(video, AudioTracks::Track::Microphone), tone(4800, 440, 0.2));
    writeTrack(AudioTracks::pathFor(video, AudioTracks::Track::SystemAudio), tone(4800, 660, 0.2));
    AudioMixSettings systemOnly;
    systemOnly.microphoneMuted = true;
    AudioTrackMixer onlySystem(video, systemOnly);
    QCOMPARE(onlySystem.read(0, 4800), tone(4800, 660, 0.2));
    AudioMixSettings louder;
    louder.microphoneGainDb = 6.0;
    louder.systemAudioMuted = true;
    AudioTrackMixer mixer(video, louder);
    const double gainDb = rmsDb(mixer.read(0, 4800)) - rmsDb(tone(4800, 440, 0.2));
    QVERIFY2(qAbs(gainDb - 6.0) < 0.1, qPrintable(QString::number(gainDb)));
    AudioMixSettings silent;
    silent.microphoneMuted = silent.systemAudioMuted = true;
    QVERIFY(!silent.isNeutral());
    const QByteArray none = AudioTrackMixer(video, silent).read(0, 4800);
    QCOMPARE(none, QByteArray(none.size(), '\0'));
}

void TestAudioTracks::noiseReductionQuietsNoiseAndKeepsSpeech()
{
    // Hiss far below speech level, then a speech-level tone.
    QByteArray quiet = tone(24000, 3000, 0.003);
    QByteArray loud = tone(24000, 300, 0.3, 24000);
    MicrophoneNoiseReducer reducer;
    QByteArray noise = quiet;
    reducer.process(noise);
    const double noiseDrop = rmsDb(quiet.mid(19200 * 4)) - rmsDb(noise.mid(19200 * 4)); // once the gate has closed
    QVERIFY2(noiseDrop > 15.0, qPrintable(QString::number(noiseDrop)));
    QByteArray speech = loud;
    reducer.process(speech);
    const double speechLoss = rmsDb(loud.mid(4800 * 4)) - rmsDb(speech.mid(4800 * 4));
    QVERIFY2(speechLoss < 1.0, qPrintable(QString::number(speechLoss)));
    // Rumble below 80 Hz is turned down.
    QByteArray rumble = tone(48000, 20, 0.3);
    const QByteArray before = rumble;
    MicrophoneNoiseReducer fresh;
    fresh.process(rumble);
    QVERIFY(rmsDb(before.mid(24000 * 4)) - rmsDb(rumble.mid(24000 * 4)) > 6.0);
}

void TestAudioTracks::noiseReductionCost()
{
    QByteArray minute = tone(48000 * 10, 300, 0.3);
    MicrophoneNoiseReducer reducer;
    QElapsedTimer timer;
    timer.start();
    reducer.process(minute);
    const double ms = timer.nsecsElapsed() / 1e6;
    qInfo("noise reduction: %.1f ms for 10 s of stereo audio (%.2f%% of real time)", ms, ms / 100.0);
    QVERIFY(ms < 2000.0); // well under real time, even on slow CI
}

void TestAudioTracks::settingsJson()
{
    AudioMixSettings s;
    s.microphoneGainDb = 6;
    s.systemAudioGainDb = -12;
    s.systemAudioMuted = true;
    s.microphoneNoiseReduction = true;
    QCOMPARE(AudioMixSettings::fromJson(s.toJson()), s);
    const auto clamped = AudioMixSettings::fromJson({{QStringLiteral("microphoneGainDb"), 99.0}});
    QCOMPARE(clamped.microphoneGainDb, AudioMixSettings::kMaxGainDb);
}

void TestAudioTracks::pathsAndRemoval()
{
    QTemporaryDir dir;
    const QString video = dir.filePath(QStringLiteral("SnapTray_Recording_x.mp4"));
    QVERIFY(!AudioTracks::exist(video));
    writeTrack(AudioTracks::pathFor(video, AudioTracks::Track::Microphone), tone(10, 440, 0.1));
    QVERIFY(!AudioTracks::exist(video)); // one track is not "separate tracks"
    writeTrack(AudioTracks::pathFor(video, AudioTracks::Track::SystemAudio), tone(10, 440, 0.1));
    QVERIFY(AudioTracks::exist(video));
    AudioTracks::remove(video);
    QVERIFY(!QFile::exists(AudioTracks::pathFor(video, AudioTracks::Track::Microphone)));
    QVERIFY(!QFile::exists(AudioTracks::pathFor(video, AudioTracks::Track::SystemAudio)));
}

QTEST_MAIN(TestAudioTracks)
#include "tst_AudioTracks.moc"
