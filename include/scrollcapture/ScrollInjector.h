#pragma once

#include <QList>
#include <QString>

#include <memory>
#include <optional>

namespace snaptray::scrollcapture {

class ScrollArea;

enum class ScrollMethod {
    // macOS: CGEvent scroll wheel in pixel units.
    WheelPixels,
    // macOS: the same, flagged continuous so it is handled like a trackpad.
    WheelPixelsContinuous,
    // Windows: SendInput MOUSEEVENTF_WHEEL; amount in wheel units (120 = one notch).
    WheelDelta,
    // Windows: ScrollPattern::Scroll, `amount` small increments.
    PatternSmallIncrement,
    // Windows: ScrollPattern::SetScrollPercent to `percent`.
    PatternSetPercent,
};

struct ScrollRequest {
    ScrollMethod method = ScrollMethod::WheelPixels;
    // Positive scrolls toward the bottom of the content.
    int amount = 0;
    double percent = 0.0;
    // macOS: deliver with CGEventPostToPid instead of moving the cursor.
    bool postToProcess = false;
};

struct InjectResult {
    bool ok = false;
    QString detail;
};

// Sends scroll input to a scroll area found by ScrollAreaLocator. Wheel
// methods move the cursor to the centre of the area (except postToProcess),
// because the system routes wheel input to the window under the cursor.
class ScrollInjector
{
public:
    static std::unique_ptr<ScrollInjector> create();

    virtual ~ScrollInjector() = default;
    virtual QList<ScrollMethod> supportedMethods() const = 0;
    virtual InjectResult scroll(const ScrollArea& area, const ScrollRequest& request) = 0;
};

const char* scrollMethodName(ScrollMethod method);
std::optional<ScrollMethod> scrollMethodFromName(const QString& name);

} // namespace snaptray::scrollcapture
