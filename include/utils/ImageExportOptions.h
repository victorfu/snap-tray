#ifndef IMAGEEXPORTOPTIONS_H
#define IMAGEEXPORTOPTIONS_H

#include <QByteArray>
#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>

/**
 * @brief How a still image is written out as a smaller derived file (#357).
 *
 * Sizes are pixels of the image itself, never logical (device-independent)
 * points: a 2x capture that is 1280 points wide is 2560 px here. Resizing
 * keeps the aspect ratio and never enlarges.
 */
struct ImageExportOptions
{
    enum class Resize {
        Original = 0,   // keep every pixel
        Percent,        // scale both sides by `percent`
        LongEdge,       // limit the longer side to `longEdge` pixels
    };

    enum class Format {
        Png = 0,    // lossless
        Jpeg,       // lossy, no transparency
    };

    static constexpr int kMinPercent = 5;
    static constexpr int kMaxPercent = 100;
    static constexpr int kDefaultPercent = 50;
    static constexpr int kMinLongEdge = 16;
    static constexpr int kMaxLongEdge = 16384;
    static constexpr int kDefaultLongEdge = 1920;
    static constexpr int kMinJpegQuality = 1;
    static constexpr int kMaxJpegQuality = 100;
    static constexpr int kDefaultJpegQuality = 90;

    Resize resize = Resize::Original;
    int percent = kDefaultPercent;
    int longEdge = kDefaultLongEdge;
    Format format = Format::Png;
    int jpegQuality = kDefaultJpegQuality;

    // The same options with every value inside its range.
    ImageExportOptions normalized() const;

    // Pixel size of the output for a source of `sourcePixels`. Never larger
    // than the source and never smaller than 1x1.
    QSize outputSize(const QSize& sourcePixels) const;

    bool isLossy() const { return format == Format::Jpeg; }
    // QImageWriter format name: "png" or "jpeg".
    QByteArray formatName() const;
    // Preferred file suffix without the dot: "png" or "jpg".
    QString suffix() const;
    // Every suffix a file of this format may carry.
    QStringList acceptedSuffixes() const;

    friend bool operator==(const ImageExportOptions& a, const ImageExportOptions& b)
    {
        return a.resize == b.resize && a.percent == b.percent && a.longEdge == b.longEdge
            && a.format == b.format && a.jpegQuality == b.jpegQuality;
    }
    friend bool operator!=(const ImageExportOptions& a, const ImageExportOptions& b)
    {
        return !(a == b);
    }
};

namespace ImageExport {

struct Encoded
{
    QByteArray data;              // the complete file contents
    QSize pixelSize;              // size of the encoded image
    ImageExportOptions options;   // what `data` was made with
    // The source has see-through pixels, which this format cannot keep; they
    // were filled with kOpaqueBackground.
    bool transparencyDropped = false;
    QString error;

    bool isValid() const { return error.isEmpty() && !data.isEmpty(); }
};

// What shows through transparent pixels in a format without alpha. One place
// to change when a user-chosen background (#350) arrives.
inline constexpr QRgb kOpaqueBackground = 0xFFFFFFFF;

// The pixels that will be encoded: `source` resized as the options say, and
// for a format without alpha composed onto kOpaqueBackground. Returns
// `source` itself when neither is needed.
QImage render(const QImage& source, const ImageExportOptions& options);

// Renders and encodes in memory. Preview and final file both come from here,
// so the size shown is the size written.
Encoded encode(const QImage& source, const ImageExportOptions& options);

// True when any pixel is not fully opaque.
bool hasTransparency(const QImage& image);

} // namespace ImageExport

#endif // IMAGEEXPORTOPTIONS_H
