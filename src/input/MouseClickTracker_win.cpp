#include "input/MouseClickTracker.h"
#include "utils/CoordinateHelper.h"

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

#include <QDebug>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>

#ifdef Q_OS_WIN

namespace {

// Find screen containing a physical coordinate
// QGuiApplication::screenAt() expects logical coordinates, so we need custom logic
QScreen* screenAtPhysical(const QPoint &physicalPos)
{
    const auto screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        QRect logicalGeom = screen->geometry();
        qreal dpr = screen->devicePixelRatio();
        QRect physicalGeom = CoordinateHelper::toPhysical(logicalGeom, dpr);

        if (physicalGeom.contains(physicalPos)) {
            return screen;
        }
    }
    return QGuiApplication::primaryScreen();
}

} // anonymous namespace

/**
 * @brief Windows implementation using low-level mouse hook.
 *
 * This implementation uses SetWindowsHookEx with WH_MOUSE_LL to
 * capture mouse events at the system level.
 */
class WinMouseClickTracker : public MouseClickTracker
{
public:
    explicit WinMouseClickTracker(QObject *parent = nullptr)
        : MouseClickTracker(parent)
        , m_running(false)
    {
        // Store instance for static callback
        s_instance = this;
    }

    ~WinMouseClickTracker() override
    {
        stop();
        if (s_instance == this) {
            s_instance = nullptr;
        }
    }

    bool start() override
    {
        if (m_running) {
            return true;
        }

        // Install low-level mouse hook
        m_hook = SetWindowsHookEx(
            WH_MOUSE_LL,
            LowLevelMouseProc,
            GetModuleHandle(NULL),
            0
        );

        if (!m_hook) {
            qWarning() << "MouseClickTracker: Failed to install mouse hook, error:" << GetLastError();
            return false;
        }

        m_running = true;
        qDebug() << "MouseClickTracker: Started tracking mouse clicks";
        return true;
    }

    void stop() override
    {
        if (!m_running) {
            return;
        }

        m_running = false;

        if (m_hook) {
            UnhookWindowsHookEx(m_hook);
            m_hook = nullptr;
        }

        qDebug() << "MouseClickTracker: Stopped tracking";
    }

    bool isRunning() const override
    {
        return m_running;
    }

private:
    static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
    {
        if (nCode >= 0 && s_instance && s_instance->m_running) {
            MSLLHOOKSTRUCT *mouseStruct = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);

            // MSLLHOOKSTRUCT.pt is in physical screen coordinates
            QPoint physicalPos(mouseStruct->pt.x, mouseStruct->pt.y);

            // Convert to Qt logical coordinates for consistency with the rest of the app
            QScreen *screen = screenAtPhysical(physicalPos);
            qreal dpr = screen ? screen->devicePixelRatio() : 1.0;
            QPoint pos = CoordinateHelper::toLogical(physicalPos, dpr);

            switch (wParam) {
            case WM_LBUTTONDOWN:
            case WM_RBUTTONDOWN:
            case WM_MBUTTONDOWN:
                QMetaObject::invokeMethod(s_instance, [pos]() {
                    if (s_instance) {
                        emit s_instance->mouseClicked(pos);
                    }
                }, Qt::QueuedConnection);
                break;

            case WM_LBUTTONUP:
            case WM_RBUTTONUP:
            case WM_MBUTTONUP:
                QMetaObject::invokeMethod(s_instance, [pos]() {
                    if (s_instance) {
                        emit s_instance->mouseReleased(pos);
                    }
                }, Qt::QueuedConnection);
                break;
            }
        }

        return CallNextHookEx(s_instance ? s_instance->m_hook : nullptr, nCode, wParam, lParam);
    }

    HHOOK m_hook = nullptr;
    bool m_running;

    static WinMouseClickTracker *s_instance;
};

WinMouseClickTracker* WinMouseClickTracker::s_instance = nullptr;

#endif // Q_OS_WIN

// Base class constructor
MouseClickTracker::MouseClickTracker(QObject *parent)
    : QObject(parent)
{
}

// Factory method
MouseClickTracker* MouseClickTracker::create(QObject *parent)
{
#ifdef Q_OS_WIN
    return new WinMouseClickTracker(parent);
#else
    Q_UNUSED(parent);
    return nullptr;
#endif
}
