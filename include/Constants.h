#ifndef SNAPTRAY_CONSTANTS_H
#define SNAPTRAY_CONSTANTS_H

#include <QColor>

namespace SnapTray {

// ============================================================================
// TIMER & ANIMATION (milliseconds)
// ============================================================================
namespace Timer {
constexpr int k60FpsInterval = 16;            // ~60fps animation/update
constexpr int k30FpsInterval = 33;            // ~30fps
constexpr int kClickThroughHover = 50;        // Click-through hover check
constexpr int kScrollStabilization = 50;      // After scroll stabilization
constexpr int kOverlayRenderDelay = 100;      // Before capture overlay render
constexpr int kDurationUpdate = 100;          // Recording duration UI update
constexpr int kPositionUpdate = 100;          // Video position update
constexpr int kResizeDebounce = 150;          // Resize finish debounce
constexpr int kFlickerAnimation = 150;        // Scrolling capture flicker
constexpr int kAnnotationAnimation = 200;     // Default annotation animation
constexpr int kDoubleClickInterval = 300;     // Double-click detection
constexpr int kSocketTimeout = 500;           // Socket connection wait
constexpr int kCountdownScale = 800;          // Countdown scale animation
constexpr int kSpinnerRotation = 1000;        // Spinner full rotation
constexpr int kLaserFade = 1000;              // Laser pointer fade
constexpr int kRecoveryTimer = 1500;          // Recovery timer duration
constexpr int kUIIndicatorTimeout = 1500;     // Zoom/opacity label timeout
constexpr int kLaserStrokeFade = 2000;        // Laser stroke fade
constexpr int kOCRToastTimeout = 2500;        // OCR notification
constexpr int kHintHideDelay = 3000;          // Onboarding hint hide
constexpr int kErrorToastTimeout = 5000;      // Error notifications
constexpr int kMaxCaptureTimeout = 300000;    // 5 minutes max capture
}  // namespace Timer

// ============================================================================
// FRAME RATES (fps)
// ============================================================================
namespace FrameRate {
constexpr int kLivePreview = 15;    // Pin window live capture
constexpr int kDefault = 30;        // Default recording
constexpr int kSmooth = 60;         // High quality
constexpr int kMax = 120;           // Maximum allowed
}  // namespace FrameRate

// ============================================================================
// UI DIMENSIONS (pixels)
// ============================================================================
namespace UI {
namespace Toolbar {
constexpr int kHeight = 32;
constexpr int kButtonWidth = 28;
constexpr int kButtonHeight = 24;
constexpr int kButtonSpacing = 2;
constexpr int kSeparatorWidth = 4;
constexpr int kHorizontalPadding = 10;
constexpr int kMargin = 8;
}  // namespace Toolbar

namespace PinWindow {
constexpr int kMinSize = 50;
constexpr int kLiveIndicatorRadius = 6;
constexpr int kLiveIndicatorMargin = 8;
}  // namespace PinWindow

namespace Toast {
constexpr int kWidth = 320;
constexpr int kMarginVertical = 12;
constexpr int kMarginHorizontal = 16;
constexpr int kLabelPadding = 8;
}  // namespace Toast

namespace Layout {
constexpr int kSpacingLarge = 16;
constexpr int kSpacingMedium = 12;
constexpr int kSpacingSmall = 8;
constexpr int kSettingsLabelWidth = 120;
constexpr int kHotkeyIconSize = 24;
}  // namespace Layout

namespace Border {
constexpr int kWidth = 2;
constexpr int kCornerRadius = 10;
}  // namespace Border
}  // namespace UI

// ============================================================================
// ZOOM & OPACITY BOUNDS
// ============================================================================
namespace Bounds {
constexpr double kMinZoom = 0.1;
constexpr double kMaxZoom = 5.0;
constexpr double kMinOpacity = 0.1;
constexpr double kMaxOpacity = 1.0;
constexpr int kMinSpotlightRadius = 50;
constexpr int kMaxSpotlightRadius = 300;
constexpr int kDefaultSpotlightRadius = 100;
constexpr int kMinCursorHighlightRadius = 10;
constexpr int kMaxCursorHighlightRadius = 100;
}  // namespace Bounds

// ============================================================================
// COLORS
// ============================================================================
namespace Color {
// Status colors
inline const QColor kSuccess{34, 139, 34, 220};     // Green
inline const QColor kFailure{200, 60, 60, 220};     // Red
inline const QColor kWarning{255, 165, 0};          // Orange
inline const QColor kInfo{0, 122, 255};             // Blue
inline const QColor kDisabled{128, 128, 128};       // Gray

// Live indicator
inline const QColor kLiveActive{255, 0, 0, 150};    // Pulsing red
inline const QColor kLivePaused{255, 165, 0};       // Orange

// Apple system colors
inline const QColor kAppleGreen{52, 199, 89};
inline const QColor kAppleBlue{0, 122, 255};
inline const QColor kAppleRed{255, 59, 48};

// UI accents
inline const QColor kGizmoAccent{0, 174, 255};
inline const QColor kClickRipple{255, 200, 0};      // Gold
inline const QColor kUndoAvailable{255, 180, 100};  // Orange

// Pin window borders
inline const QColor kBorderIndigo{88, 86, 214, 200};
inline const QColor kBorderGreen{52, 199, 89, 200};
inline const QColor kBorderBlue{0, 122, 255, 200};

// Capture status
inline const QColor kCaptureIdle{0x88, 0x88, 0x88};
inline const QColor kCaptureActive{0x4C, 0xAF, 0x50};
inline const QColor kCaptureWarning{0xFF, 0xC1, 0x07};
inline const QColor kCaptureFailed{0xFF, 0x3B, 0x30};
inline const QColor kCaptureRecovered{0x00, 0x7A, 0xFF};
}  // namespace Color

// ============================================================================
// ENCODING & QUALITY
// ============================================================================
namespace Encoding {
constexpr int kAudioBitsPerSample = 16;
constexpr int kGifBitDepthDefault = 16;
constexpr int kGifBitDepthMin = 1;
constexpr int kGifBitDepthMax = 16;
constexpr int kH264QualityDefault = 55;
constexpr int kWebPQualityDefault = 80;
constexpr int kWebPQualityMin = 0;
constexpr int kWebPQualityMax = 100;
constexpr int kVP9CRFMax = 51;
}  // namespace Encoding

// ============================================================================
// SCROLLING CAPTURE
// ============================================================================
namespace ScrollCapture {
constexpr int kCaptureIntervalMs = 60;
constexpr int kMaxCaptureIntervalMs = 200;
constexpr int kAdjustmentCooldownMs = 500;
constexpr int kMaxPendingFrames = 100;
constexpr int kMaxTotalFrames = 1000;
constexpr int kMaxRecoveryPixels = 600;
constexpr int kDefaultScrollEstimate = 60;
constexpr int kMaxOverlapPixels = 500;
constexpr int kDuplicateCheckWindow = 200;
}  // namespace ScrollCapture

// ============================================================================
// IMAGE PROCESSING THRESHOLDS
// ============================================================================
namespace ImageProcessing {
constexpr double kMaxOverlapDiffThreshold = 0.10;
constexpr double kSeamVerifyThreshold = 0.06;
constexpr double kMinCorrelationThreshold = 0.25;
constexpr double kConfidenceGood = 0.50;
constexpr double kConfidenceExcellent = 0.70;
constexpr int kPixelDiffThreshold = 20;
constexpr int kCorrelationSampleSize = 64;
constexpr int kMosaicBlockSize = 12;
constexpr int kMaxORBFeatures = 2000;
}  // namespace ImageProcessing

// ============================================================================
// CURSOR PRIORITIES
// ============================================================================
namespace CursorPriority {
constexpr int kTool = 100;
constexpr int kHover = 150;
constexpr int kSelection = 200;
constexpr int kDrag = 300;
constexpr int kOverride = 500;
}  // namespace CursorPriority

// ============================================================================
// MISCELLANEOUS
// ============================================================================
namespace Misc {
constexpr int kMaxUndoHistory = 50;
constexpr int kMaxDXGIRetries = 3;
constexpr int kOpticalFlowMaxHistory = 10;
constexpr int kOpticalFlowTrackingPoints = 100;
constexpr int kMinCaptureRegionDim = 10;
constexpr int kTextEditorMinWidth = 150;
constexpr int kDefaultFontSize = 16;
constexpr int kRotationIncrement = 90;
constexpr size_t kMaxPendingMemoryBytes = 300 * 1024 * 1024;  // 300MB
}  // namespace Misc

}  // namespace SnapTray

#endif  // SNAPTRAY_CONSTANTS_H
