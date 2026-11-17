#pragma once

#include <QJsonObject>
#include <QSize>
#include <algorithm>
#include <cmath>

namespace SnapTray {
// GIF and WebP have independent saved instances. Defaults preserve the old
// size/rate/quality; resizing happens after all source-resolution effects.
struct AnimatedExportOptions {
    enum class Resolution { Original, Edge1280, Edge960, Edge640, Edge480, CustomWidth };
    enum class Quality { High, Medium, Low };
    Resolution resolution = Resolution::Original;
    int customWidth = 640;
    int frameRate = 0;
    Quality quality = Quality::High;
    bool looping = true;
    bool minimizeSize = false;
    static constexpr int kFrameRates[] = {0, 30, 20, 15, 10};
    static constexpr int kEdges[] = {0, 1280, 960, 640, 480};
    static constexpr int kGifDepths[] = {16, 12, 8};
    static constexpr int kWebPQualities[] = {80, 60, 40};
    static constexpr qint64 kSmallShareLimit = 10ll * 1024 * 1024;
    static constexpr qint64 kLargeShareLimit = 25ll * 1024 * 1024;

    QSize outputSize(QSize source) const {
        if (source.isEmpty() || resolution == Resolution::Original) return source;
        const double ratio = resolution == Resolution::CustomWidth
            ? double(customWidth) / source.width()
            : double(kEdges[int(resolution)]) / std::max(source.width(), source.height());
        if (ratio >= 1.0) return source;
        // Both formats accept this even size, avoiding encoder-side stretching.
        return {std::max(2, int(std::floor(source.width() * ratio)) & ~1),
                std::max(2, int(std::floor(source.height() * ratio)) & ~1)};
    }
    int outputFrameRate(double source) const {
        const int fps = std::isfinite(source) && source > 0 ? std::max(1, int(source)) : 30;
        return frameRate > 0 ? std::min(frameRate, fps) : fps;
    }
    int gifDepth() const { return kGifDepths[int(quality)]; }
    int webpQuality() const { return kWebPQualities[int(quality)]; }
    QJsonObject toJson() const {
        return {{QStringLiteral("resolution"), int(resolution)}, {QStringLiteral("width"), customWidth},
                {QStringLiteral("fps"), frameRate}, {QStringLiteral("quality"), int(quality)},
                {QStringLiteral("loop"), looping}, {QStringLiteral("minimizeSize"), minimizeSize}};
    }
    static AnimatedExportOptions fromJson(const QJsonObject& json) {
        AnimatedExportOptions value;
        value.resolution = Resolution(std::clamp(json.value(QStringLiteral("resolution")).toInt(), 0, 5));
        value.customWidth = std::clamp(json.value(QStringLiteral("width")).toInt(640), 64, 7680);
        const int fps = json.value(QStringLiteral("fps")).toInt();
        for (int rate : kFrameRates) if (fps == rate) value.frameRate = rate;
        value.quality = Quality(std::clamp(json.value(QStringLiteral("quality")).toInt(), 0, 2));
        value.looping = json.value(QStringLiteral("loop")).toBool(true);
        value.minimizeSize = json.value(QStringLiteral("minimizeSize")).toBool(false);
        return value;
    }
    bool operator==(const AnimatedExportOptions& other) const { return toJson() == other.toJson(); }
};
} // namespace SnapTray
