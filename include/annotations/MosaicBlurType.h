#pragma once

/**
 * Shared blur mode used by manual mosaic strokes, rectangular auto-blur
 * annotations, and persisted blur-related settings.
 */
enum class MosaicBlurType {
    Pixelate = 0,
    Gaussian = 1,
};

