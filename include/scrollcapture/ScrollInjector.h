#pragma once

#include <QList>
#include <QString>

#include <chrono>
#include <memory>
#include <optional>
#include <utility>

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
    // When the input was handed to the system. Locating a target point can
    // take a while, so settle timing must start here, not when scroll() was
    // called.
    std::optional<std::chrono::steady_clock::time_point> dispatchedAt;

    static InjectResult failed(QString reason) { return {false, std::move(reason), std::nullopt}; }
};

// Sends scroll input to a scroll area found by ScrollAreaLocator. Wheel
// methods move the cursor (except postToProcess) to a point where the area is
// the innermost scroll area, because the system routes wheel input there; on
// Windows they refuse when wheel routing follows focus and the target is not
// focused.
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
