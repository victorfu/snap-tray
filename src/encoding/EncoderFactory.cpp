#include "encoding/EncoderFactory.h"
#include "encoding/NativeGifEncoder.h"
#include "IVideoEncoder.h"
#include <QDebug>

EncoderFactory::EncoderResult EncoderFactory::create(
    const EncoderConfig& config, QObject* parent)
{
    EncoderResult result;

    qDebug() << "EncoderFactory: Creating encoder -"
             << "format:" << (config.format == Format::GIF ? "GIF" : "MP4")
             << "size:" << config.frameSize
             << "fps:" << config.frameRate;

    // GIF format uses NativeGifEncoder
    if (config.format == Format::GIF) {
        result.gifEncoder = tryCreateGifEncoder(config, parent);
        if (result.gifEncoder) {
            result.success = true;
            result.isNative = false;  // GIF uses gifEncoder, not nativeEncoder
            qDebug() << "EncoderFactory: Created native GIF encoder";
        } else {
            result.errorMessage = "Failed to create GIF encoder.";
            qWarning() << "EncoderFactory:" << result.errorMessage;
        }
        return result;
    }

    // MP4 format: select encoder based on priority
    switch (config.priority) {
    case Priority::NativeFirst:
    case Priority::NativeOnly:
        result.nativeEncoder = tryCreateNativeEncoder(config, parent);
        if (result.nativeEncoder) {
            result.success = true;
            result.isNative = true;
            qDebug() << "EncoderFactory: Using native encoder for MP4";
        } else {
            result.errorMessage = "Native encoder is not available on this platform.";
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

NativeGifEncoder* EncoderFactory::tryCreateGifEncoder(
    const EncoderConfig& config, QObject* parent)
{
    NativeGifEncoder* encoder = new NativeGifEncoder(parent);

    // Set quality (bit depth)
    encoder->setMaxBitDepth(config.gifBitDepth);
    qDebug() << "EncoderFactory: GIF encoder bit depth set to" << config.gifBitDepth;

    // Start the encoder
    if (!encoder->start(config.outputPath, config.frameSize, config.frameRate)) {
        qWarning() << "EncoderFactory: GIF encoder failed to start:"
                   << encoder->lastError();
        delete encoder;
        return nullptr;
    }

    qDebug() << "EncoderFactory: GIF encoder started successfully";
    return encoder;
}
