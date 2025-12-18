#ifndef WINDOWDETECTOR_H
#define WINDOWDETECTOR_H

#include <QObject>
#include <QRect>
#include <QPoint>
#include <QString>
#include <vector>
#include <optional>

class QScreen;

// Represents a detected window
struct DetectedElement {
    QRect bounds;           // Window bounds in screen coordinates (logical pixels)
    QString windowTitle;    // Window title for hint display
    QString ownerApp;       // Owning application name
    int windowLayer;        // Window layer/level for z-ordering
    uint32_t windowId;      // Window ID for identification
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

    // Find window at point (screen coordinates)
    std::optional<DetectedElement> detectWindowAt(const QPoint &screenPos) const;

private:
    void enumerateWindows();

    std::vector<DetectedElement> m_windowCache;
    QScreen *m_currentScreen;
    bool m_enabled;
};

#endif // WINDOWDETECTOR_H
