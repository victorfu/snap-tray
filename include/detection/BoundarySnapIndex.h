#pragma once

#include <QImage>
#include <optional>

// Immutable screenshot pixels, not an image-sized edge map. Coordinates are
// physical between-pixel boundaries; spans are half open. Premultiplied RGB
// treats transparency as composited over black (alpha itself is not an edge).
class BoundarySnapIndex
{
public:
    enum class Axis { Vertical, Horizontal };
    struct Options {
        double minimumMean = 28.0;
        double supportThreshold = 28.0;
        double minimumSupport = 0.55;
    };
    struct Hit {
        Axis axis;
        int pixelBoundary;
        double meanStrength;
        double supportFraction;
    };
    static constexpr qint64 MaximumPixels = 40000000;

    void setImage(const QImage& image);
    QSize size() const { return m_image.size(); }
    std::optional<Hit> evaluate(Axis axis, int boundary, int spanBegin, int spanEnd,
                                const Options& options) const;

private:
    QImage m_image;
};
