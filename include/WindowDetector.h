#ifndef WINDOWDETECTOR_H
#define WINDOWDETECTOR_H

#include <QObject>
#include <QRect>
#include <QPoint>
#include <QString>
#include <QFlags>
#include <QMutex>
#include <QFuture>
#include <QPointer>
#include <vector>
#include <optional>

class QScreen;

// Element type classification for detected UI elements
enum class ElementType {
    Window,         // Normal application window
    ContextMenu,    // Right-click context menu
    PopupMenu,      // Application menu dropdown
    Dialog,         // Dialog/modal window
    StatusBarItem,  // Menu bar popup (macOS) / System tray popup (Windows)
    Unknown
};

// Detection mode flags for controlling what types of elements to detect
enum class DetectionFlag {
    None           = 0,
    Windows        = 1 << 0,   // Normal application windows (default)
    ContextMenus   = 1 << 1,   // Right-click context menus
    PopupMenus     = 1 << 2,   // Application menu dropdowns
    Dialogs        = 1 << 3,   // Dialog/modal windows
    StatusBarItems = 1 << 4,   // Menu bar popups (macOS) / System tray popups (Windows)
    AllSystemUI    = ContextMenus | PopupMenus | Dialogs | StatusBarItems,
    All            = Windows | AllSystemUI
};
Q_DECLARE_FLAGS(DetectionFlags, DetectionFlag)
Q_DECLARE_OPERATORS_FOR_FLAGS(DetectionFlags)

// Represents a detected window or UI element
struct DetectedElement {
    QRect bounds;           // Element bounds in screen coordinates (logical pixels)
    QString windowTitle;    // Window/element title for hint display
    QString ownerApp;       // Owning application name
    int windowLayer;        // Window layer/level for z-ordering
    uint32_t windowId;      // Window ID for identification
    ElementType elementType = ElementType::Window;  // Type of detected element
};

class WindowDetector : public QObject
{
    Q_OBJECT

public:
    explicit WindowDetector(QObject *parent = nullptr);
    ~WindowDetector();

    // Permission management
    static bool hasAccessibilityPermission();
    static void requestAccessibilityPermission();

    // Detection control
    void setScreen(QScreen *screen);
    void setEnabled(bool enabled);
    bool isEnabled() const;

    // Refresh window list (call before capture session starts)
    void refreshWindowList();

    // Async version - enumerate windows in background thread
    void refreshWindowListAsync();

    // Check if async refresh is complete
    bool isRefreshComplete() const;

    // Find window at point (screen coordinates)
    std::optional<DetectedElement> detectWindowAt(const QPoint &screenPos) const;

    // Detection mode control
    void setDetectionFlags(DetectionFlags flags);
    DetectionFlags detectionFlags() const;

signals:
    void windowListReady();

private:
    void enumerateWindows();
    void enumerateWindowsInternal(std::vector<DetectedElement>& cache, qreal dpr);

    std::vector<DetectedElement> m_windowCache;
    mutable QMutex m_cacheMutex;
    QPointer<QScreen> m_currentScreen;
    bool m_enabled;
    DetectionFlags m_detectionFlags = DetectionFlag::All;
    QFuture<void> m_refreshFuture;
    std::atomic<bool> m_refreshComplete{true};
};

#endif // WINDOWDETECTOR_H
