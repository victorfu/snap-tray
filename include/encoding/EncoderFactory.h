#ifndef ENCODERFACTORY_H
#define ENCODERFACTORY_H

#include <QObject>
#include <QSize>
#include <QString>

class IVideoEncoder;
class FFmpegEncoder;

/**
 * @brief Factory class for creating video encoders
 *
 * Encapsulates the encoder selection logic, handling:
 * - Native vs FFmpeg encoder selection
 * - MP4 vs GIF format requirements
 * - Priority-based fallback strategies
 */
class EncoderFactory {
public:
    /**
     * @brief Output format enumeration
     */
    enum class Format {
        MP4,    // H.264 encoded MP4
        GIF     // Palette-optimized GIF (FFmpeg only)
    };

    /**
     * @brief Encoder selection priority
     */
    enum class Priority {
        NativeFirst,   // Try native encoder first, fallback to FFmpeg
        FFmpegFirst,   // Try FFmpeg first, fallback to native
        NativeOnly,    // Only use native encoder
        FFmpegOnly     // Only use FFmpeg
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

        // FFmpeg specific settings
        QString preset = "ultrafast";
        int crf = 23;  // 0-51 (lower = better quality)

        // Audio settings (for native encoder)
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
        FFmpegEncoder* ffmpegEncoder = nullptr;
        bool success = false;
        bool isNative = false;
        QString errorMessage;

        /**
         * @brief Check if any encoder was created
         */
        bool hasEncoder() const {
            return nativeEncoder != nullptr || ffmpegEncoder != nullptr;
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

    /**
     * @brief Check if FFmpeg is available
     */
    static bool isFFmpegAvailable();

private:
    /**
     * @brief Try to create and start a native encoder
     * @return Encoder pointer or nullptr if failed
     */
    static IVideoEncoder* tryCreateNativeEncoder(
        const EncoderConfig& config, QObject* parent);

    /**
     * @brief Try to create and start an FFmpeg encoder
     * @return Encoder pointer or nullptr if failed
     */
    static FFmpegEncoder* tryCreateFFmpegEncoder(
        const EncoderConfig& config, QObject* parent);
};

#endif // ENCODERFACTORY_H
