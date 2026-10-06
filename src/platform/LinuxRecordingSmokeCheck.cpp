#include "platform/LinuxRecordingSmokeCheck.h"
#include "IVideoEncoder.h"
#include "video/IVideoFrameReader.h"
#include "video/IVideoTranscoder.h"
#include "video/IVideoPlayer.h"
#include <QTemporaryDir>
#include <QDebug>
#include <QTextStream>
#include <memory>

namespace SnapTray {
int runLinuxRecordingSmokeCheck()
{
    QTemporaryDir directory;
    std::unique_ptr<IVideoEncoder> encoder(IVideoEncoder::createNativeEncoder());
    auto reader = IVideoFrameReader::create();
    auto transcoder = IVideoTranscoder::create();
    std::unique_ptr<IVideoPlayer> player(IVideoPlayer::create());
    if (!directory.isValid() || !encoder || !reader || !transcoder || !player) return 1;
    const QString path = directory.filePath("recording.mp4");
    encoder->setAudioFormat(48000, 2, 16);
    if (!encoder->start(path, QSize(160,120), 30) || !encoder->isAudioEnabled()) return 1;
    QImage frame(160,120,QImage::Format_RGB32); frame.fill(Qt::green);
    for (int i = 0; i < 15; ++i) {
        encoder->writeFrame(frame, qRound(i*1000.0/30));
        encoder->writeAudioSamples(QByteArray(1600*4, '\0'), i*1600);
    }
    encoder->finish();
    const auto probe = transcoder->probe(path);
    if (!encoder->lastError().isEmpty() || !probe.valid || !probe.hasAudio || !reader->load(path)) return 1;
    const auto decoded = reader->frameAt(200);
    if (decoded.isNull() || decoded.pixelColor(40,40).green() < 200) return 1;
    VideoTranscodeRequest request{path, directory.filePath("crop.mp4"), 100, 400, QRect(0,0,80,60)};
    const auto result = transcoder->transcode(request, {});
    if (!result.success || !result.audioCopied || transcoder->probe(request.outputPath).videoSize != QSize(80,60)) {
        qCritical() << "Recording smoke check failed:" << result.errorMessage; return 1;
    }
    QTextStream(stdout) << "Linux recording smoke check passed: H.264/AAC encode, decode and crop export.\n";
    return 0;
}
}
