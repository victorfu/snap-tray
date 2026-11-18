#pragma once

#include <QImage>

/**
 * Rebuilds a single column of upright CJK glyphs as one horizontal row so a
 * native OCR engine can read it. Glyphs are cut out and re-laid, not rotated.
 *
 * The output is a new image: its coordinates do not map back to the source,
 * so OCR geometry from it must never be used for highlighting, masking or
 * table detection. Only the recognized text is meaningful.
 */
namespace VerticalCJKNormalizer {

enum class Rejection {
    None,
    InvalidImage,
    NoForeground,      ///< No ink distinguishable from the background
    NotTallColumn,     ///< Ink area is not a tall, narrow column
    MultipleColumns,   ///< Empty vertical gaps split the ink into columns
    GlyphCountOutOfRange,
    NotGlyphLike,      ///< Row bands do not look like square CJK glyph cells
};

struct Result {
    QImage image;      ///< Horizontal re-layout; null when rejected
    int glyphCount = 0;
    Rejection rejection = Rejection::InvalidImage;

    bool isValid() const { return rejection == Rejection::None && !image.isNull(); }
};

// Bounds work on huge selections; the source is downscaled to this first.
constexpr int kMaxSourceDimension = 4096;
constexpr int kMinGlyphCount = 2;
constexpr int kMaxGlyphCount = 64;

Result normalize(const QImage& source);

} // namespace VerticalCJKNormalizer
