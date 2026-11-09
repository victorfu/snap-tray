#pragma once

#include <QJsonObject>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

namespace snaptray::scrollcapture {

// Scroll position as reported by the platform accessibility API. Every field
// is optional because applications expose different subsets; `sources` names
// the attributes each value came from.
struct ScrollPosition {
    // 0 at the top, 1 at the bottom.
    std::optional<double> fraction;
    // Visible height divided by content height (0..1].
    std::optional<double> viewportRatio;
    // Content height and offset from the top, in logical pixels.
    std::optional<double> contentHeight;
    std::optional<double> offset;
    std::optional<bool> atTop;
    std::optional<bool> atBottom;
    QStringList sources;
};

struct ScrollAreaSnapshot {
    // Qt global logical coordinates.
    QRect bounds;
    // AX role (macOS) or UIA control type (Windows).
    QString role;
    // 0 is the innermost scroll area under the point.
    int level = 0;
    // Scroll areas found between the point and its window.
    int availableLevels = 0;
    qint64 processId = 0;
    QString appName;
    QString appVersion;
    ScrollPosition position;
    // Raw platform values for the measurement log.
    QJsonObject diagnostics;
};

// A scroll area found by ScrollAreaLocator. It keeps the native element so
// the bounds and position can be read again after scrolling.
class ScrollArea
{
public:
    virtual ~ScrollArea() = default;
    virtual ScrollAreaSnapshot snapshot() const = 0;
};

enum class LocateStatus {
    Found,
    // The point is over a window, but nothing scrollable was reported.
    NoScrollArea,
    // No other application's window under the point.
    NoWindow,
    // The process may not use the accessibility API (macOS).
    PermissionDenied,
    // The target runs at a higher privilege level (Windows UIPI).
    Blocked,
    // No implementation on this platform.
    Unsupported,
    Failed,
};

struct LocateOptions {
    // Which scroll area to return when several are nested: 0 is innermost.
    int level = 0;
    // Chromium can return a partial tree on the first query; retrying gives
    // it time to build the rest.
    int maxAttempts = 1;
    int retryDelayMs = 150;
};

// Ways to ask a Chromium/Electron application to build the accessibility
// tree of its web content, which it otherwise does only for assistive clients.
enum class AccessibilityBoost {
    ManualAccessibility,     // AXManualAccessibility (Electron's switch)
    EnhancedUserInterface,   // AXEnhancedUserInterface (what VoiceOver sets)
    Both,
};

struct LocateResult {
    LocateStatus status = LocateStatus::Failed;
    QString detail;
    int attempts = 0;
    // Owner of the window under the point when known, even without a scroll area.
    qint64 processId = 0;
    std::unique_ptr<ScrollArea> area;
};

// Finds the scroll area under a screen point by asking the platform
// accessibility API (macOS AX, Windows UI Automation). Never infers anything
// from pixels. Windows of this process are skipped.
class ScrollAreaLocator
{
public:
    static std::unique_ptr<ScrollAreaLocator> create();

    virtual ~ScrollAreaLocator() = default;
    virtual bool isAvailable() const = 0;
    // `globalPos` is in Qt global logical coordinates.
    virtual LocateResult locateAt(const QPoint& globalPos, const LocateOptions& options) = 0;
    // macOS: asks an application to build its full accessibility tree. The
    // previous values are remembered per process and put back by
    // restoreAccessibility() or the destructor; an EnhancedUserInterface that
    // was already on (VoiceOver) is left alone. The result lists each
    // attribute with its AXError for the log. Chromium can report an error
    // and still build the tree, so judge success by locating again.
    // Other platforms: {"supported": false}.
    virtual QJsonObject requestFullAccessibilityTree(qint64 processId, AccessibilityBoost boost);
    virtual void restoreAccessibility();
    // System settings that change scrolling behaviour, for the measurement log.
    virtual QJsonObject environment() const = 0;
};

const char* locateStatusName(LocateStatus status);
const char* accessibilityBoostName(AccessibilityBoost boost);
std::optional<AccessibilityBoost> accessibilityBoostFromName(const QString& name);
QJsonObject toJson(const ScrollPosition& position);
QJsonObject toJson(const ScrollAreaSnapshot& snapshot);

} // namespace snaptray::scrollcapture
