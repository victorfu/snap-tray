#include "encoding/EncoderFactory.h"
#include "IVideoEncoder.h"
#include "FFmpegEncoder.h"
#include <QDebug>

EncoderFactory::EncoderResult EncoderFactory::create(
    const EncoderConfig& config, QObject* parent)
{
    EncoderResult result;

    qDebug() << "EncoderFactory: Creating encoder -"
             << "format:" << (config.format == Format::GIF ? "GIF" : "MP4")
             << "priority:" << static_cast<int>(config.priority)
             << "size:" << config.frameSize
             << "fps:" << config.frameRate;

    // GIF format requires FFmpeg
    if (config.format == Format::GIF) {
        result.ffmpegEncoder = tryCreateFFmpegEncoder(config, parent);
        if (result.ffmpegEncoder) {
            result.success = true;
            result.isNative = false;
            qDebug() << "EncoderFactory: Created FFmpeg encoder for GIF";
        } else {
            result.errorMessage = "GIF recording requires FFmpeg. "
                                  "Please install FFmpeg or switch to MP4 format.";
            qWarning() << "EncoderFactory:" << result.errorMessage;
        }
        return result;
    }

    // MP4 format: select encoder based on priority
    switch (config.priority) {
    case Priority::NativeFirst:
        result.nativeEncoder = tryCreateNativeEncoder(config, parent);
        if (result.nativeEncoder) {
            result.success = true;
            result.isNative = true;
            qDebug() << "EncoderFactory: Using native encoder for MP4";
        } else {
            // Fallback to FFmpeg
            result.ffmpegEncoder = tryCreateFFmpegEncoder(config, parent);
            if (result.ffmpegEncoder) {
                result.success = true;
                result.isNative = false;
                qDebug() << "EncoderFactory: Using FFmpeg encoder for MP4 (fallback)";
            } else {
                result.errorMessage = "No video encoder available. "
                                      "Native encoder failed and FFmpeg is not installed.";
                qWarning() << "EncoderFactory:" << result.errorMessage;
            }
        }
        break;

    case Priority::FFmpegFirst:
        result.ffmpegEncoder = tryCreateFFmpegEncoder(config, parent);
        if (result.ffmpegEncoder) {
            result.success = true;
            result.isNative = false;
            qDebug() << "EncoderFactory: Using FFmpeg encoder for MP4";
        } else {
            // Fallback to native
            result.nativeEncoder = tryCreateNativeEncoder(config, parent);
            if (result.nativeEncoder) {
                result.success = true;
                result.isNative = true;
                qDebug() << "EncoderFactory: Using native encoder for MP4 (fallback)";
            } else {
                result.errorMessage = "No video encoder available. "
                                      "FFmpeg is not installed and native encoder failed.";
                qWarning() << "EncoderFactory:" << result.errorMessage;
            }
        }
        break;

    case Priority::NativeOnly:
        result.nativeEncoder = tryCreateNativeEncoder(config, parent);
        if (result.nativeEncoder) {
            result.success = true;
            result.isNative = true;
            qDebug() << "EncoderFactory: Using native encoder for MP4 (native only)";
        } else {
            result.errorMessage = "Native encoder is not available on this platform.";
            qWarning() << "EncoderFactory:" << result.errorMessage;
        }
        break;

    case Priority::FFmpegOnly:
        result.ffmpegEncoder = tryCreateFFmpegEncoder(config, parent);
        if (result.ffmpegEncoder) {
            result.success = true;
            result.isNative = false;
            qDebug() << "EncoderFactory: Using FFmpeg encoder for MP4 (FFmpeg only)";
        } else {
            result.errorMessage = "FFmpeg is not available. Please install FFmpeg.";
            qWarning() << "EncoderFactory:" << result.errorMessage;
        }
        break;
    }

    return result;
}

bool EncoderFactory::isNativeEncoderAvailable()
{
    IVideoEncoder* encoder = IVideoEncoder::createNativeEncoder(nullptr);
    if (encoder) {
        bool available = encoder->isAvailable();
        delete encoder;
        return available;
    }
    return false;
}

bool EncoderFactory::isFFmpegAvailable()
{
    return FFmpegEncoder::isFFmpegAvailable();
}

IVideoEncoder* EncoderFactory::tryCreateNativeEncoder(
    const EncoderConfig& config, QObject* parent)
{
    IVideoEncoder* encoder = IVideoEncoder::createNativeEncoder(parent);
    if (!encoder) {
        qDebug() << "EncoderFactory: Native encoder not available on this platform";
        return nullptr;
    }

    if (!encoder->isAvailable()) {
        qDebug() << "EncoderFactory: Native encoder reports not available";
        delete encoder;
        return nullptr;
    }

    // Set quality
    encoder->setQuality(config.quality);
    qDebug() << "EncoderFactory: Native encoder quality set to" << config.quality;

    // Configure audio format before start() - this is critical
    if (config.enableAudio && encoder->isAudioSupported()) {
        encoder->setAudioFormat(
            config.audioSampleRate,
            config.audioChannels,
            config.audioBitsPerSample);
        qDebug() << "EncoderFactory: Configured native encoder audio -"
                 << config.audioSampleRate << "Hz,"
                 << config.audioChannels << "ch,"
                 << config.audioBitsPerSample << "bits";
    }

    // Start the encoder
    if (!encoder->start(config.outputPath, config.frameSize, config.frameRate)) {
        qWarning() << "EncoderFactory: Native encoder failed to start:"
                   << encoder->lastError();
        delete encoder;
        return nullptr;
    }

    qDebug() << "EncoderFactory: Native encoder started successfully:"
             << encoder->encoderName();
    return encoder;
}

FFmpegEncoder* EncoderFactory::tryCreateFFmpegEncoder(
    const EncoderConfig& config, QObject* parent)
{
    if (!FFmpegEncoder::isFFmpegAvailable()) {
        qDebug() << "EncoderFactory: FFmpeg not available";
        return nullptr;
    }

    FFmpegEncoder* encoder = new FFmpegEncoder(parent);

    // Set output format
    if (config.format == Format::GIF) {
        encoder->setOutputFormat(FFmpegEncoder::OutputFormat::GIF);
    } else {
        encoder->setOutputFormat(FFmpegEncoder::OutputFormat::MP4);
        // Set encoding parameters for MP4
        encoder->setPreset(config.preset);
        encoder->setCrf(config.crf);
        qDebug() << "EncoderFactory: FFmpeg preset:" << config.preset
                 << "CRF:" << config.crf;
    }

    // Start the encoder
    if (!encoder->start(config.outputPath, config.frameSize, config.frameRate)) {
        qWarning() << "EncoderFactory: FFmpeg encoder failed to start:"
                   << encoder->lastError();
        delete encoder;
        return nullptr;
    }

    qDebug() << "EncoderFactory: FFmpeg encoder started successfully";
    return encoder;
}
