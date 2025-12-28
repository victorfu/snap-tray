#ifndef ENCODERFACTORY_H
#define ENCODERFACTORY_H

#include <QObject>
#include <QSize>
#include <QString>

class IVideoEncoder;
class NativeGifEncoder;
class WebPAnimationEncoder;

/**
 * @brief Factory class for creating video encoders
 *
 * Encapsulates the encoder selection logic, handling:
 * - Native MP4 encoder (platform-specific)
 * - Native GIF encoder (msf_gif)
 */
class EncoderFactory {
public:
    /**
     * @brief Output format enumeration
     */
    enum class Format {
        MP4,    // H.264 encoded MP4 (native encoder)
        GIF,    // Palette-optimized GIF (msf_gif)
        WebP    // WebP animation (libwebp)
    };

    /**
     * @brief Encoder selection priority
     */
    enum class Priority {
        NativeFirst,   // Try native encoder first (default)
        NativeOnly     // Only use native encoder
    };

    /**
     * @brief Configuration for encoder creation
     */
    struct EncoderConfig {
        Format format = Format::MP4;
        Priority priority = Priority::NativeFirst;
        QSize frameSize;
        int frameRate = 30;
        QString outputPath;

        // Native encoder settings
        int quality = 55;  // 0-100 (higher = better quality, larger file)

        // GIF settings (msf_gif)
        int gifBitDepth = 16;  // 1-16, higher = better colors but larger file

        // WebP settings (libwebp)
        int webpQuality = 80;  // 0-100, higher = better quality, larger file
        bool webpLooping = true;  // Whether animation should loop

        // Audio settings (for native MP4 encoder only)
        bool enableAudio = false;
        int audioSampleRate = 48000;
        int audioChannels = 2;
        int audioBitsPerSample = 16;
    };

    /**
     * @brief Result of encoder creation
     */
    struct EncoderResult {
        IVideoEncoder* nativeEncoder = nullptr;
        NativeGifEncoder* gifEncoder = nullptr;
        WebPAnimationEncoder* webpEncoder = nullptr;
        bool success = false;
        bool isNative = false;
        QString errorMessage;

        /**
         * @brief Check if any encoder was created
         */
        bool hasEncoder() const {
            return nativeEncoder != nullptr || gifEncoder != nullptr || webpEncoder != nullptr;
        }
    };

    // Delete constructor - this is a static-only class
    EncoderFactory() = delete;

    /**
     * @brief Create an encoder based on configuration
     * @param config Encoder configuration
     * @param parent QObject parent for the created encoder
     * @return EncoderResult containing the created encoder or error message
     */
    static EncoderResult create(const EncoderConfig& config, QObject* parent);

    /**
     * @brief Check if native encoder is available on this platform
     */
    static bool isNativeEncoderAvailable();

private:
    /**
     * @brief Try to create and start a native encoder
     * @return Encoder pointer or nullptr if failed
     */
    static IVideoEncoder* tryCreateNativeEncoder(
        const EncoderConfig& config, QObject* parent);

    /**
     * @brief Try to create and start a GIF encoder
     * @return Encoder pointer or nullptr if failed
     */
    static NativeGifEncoder* tryCreateGifEncoder(
        const EncoderConfig& config, QObject* parent);

    /**
     * @brief Try to create and start a WebP encoder
     * @return Encoder pointer or nullptr if failed
     */
    static WebPAnimationEncoder* tryCreateWebPEncoder(
        const EncoderConfig& config, QObject* parent);
};

#endif // ENCODERFACTORY_H
