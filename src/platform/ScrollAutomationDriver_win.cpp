#include "platform/ScrollAutomationDriverFactory.h"
#include "platform/IScrollAutomationDriver.h"

#include <QDebug>
#include <QScreen>
#include <QString>
#include <QtGlobal>

#ifdef Q_OS_WIN
#include <windows.h>
#include <uiautomation.h>

namespace {

struct MonitorLookupContext {
    QString targetDeviceName;
    MONITORINFOEXW monitorInfo{};
    bool found = false;
};

BOOL CALLBACK findMonitorByDeviceName(HMONITOR hMonitor, HDC, LPRECT, LPARAM userData)
{
    auto *context = reinterpret_cast<MonitorLookupContext *>(userData);
    if (!context) {
        return TRUE;
    }

    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(hMonitor, reinterpret_cast<LPMONITORINFO>(&info))) {
        return TRUE;
    }

    if (QString::fromWCharArray(info.szDevice) == context->targetDeviceName) {
        context->monitorInfo = info;
        context->found = true;
        return FALSE;
    }

    return TRUE;
}

bool monitorInfoForScreen(QScreen *screen, MONITORINFOEXW *outInfo)
{
    if (!screen || !outInfo) {
        return false;
    }

    MonitorLookupContext context;
    context.targetDeviceName = screen->name();
    EnumDisplayMonitors(nullptr, nullptr, findMonitorByDeviceName, reinterpret_cast<LPARAM>(&context));
    if (!context.found) {
        return false;
    }

    *outInfo = context.monitorInfo;
    return true;
}

POINT logicalToPhysicalPoint(const QPoint &globalPoint, QScreen *screen)
{
    POINT pt{};
    pt.x = qRound(globalPoint.x());
    pt.y = qRound(globalPoint.y());
    if (!screen) {
        return pt;
    }

    const qreal dpr = qMax(1.0, screen->devicePixelRatio());
    MONITORINFOEXW monitorInfo{};
    if (!monitorInfoForScreen(screen, &monitorInfo)) {
        // Fallback for unexpected monitor lookup failures.
        pt.x = qRound(globalPoint.x() * dpr);
        pt.y = qRound(globalPoint.y() * dpr);
        return pt;
    }

    const QPoint logicalScreenOrigin = screen->geometry().topLeft();
    const int logicalOffsetX = globalPoint.x() - logicalScreenOrigin.x();
    const int logicalOffsetY = globalPoint.y() - logicalScreenOrigin.y();
    pt.x = monitorInfo.rcMonitor.left + qRound(logicalOffsetX * dpr);
    pt.y = monitorInfo.rcMonitor.top + qRound(logicalOffsetY * dpr);
    return pt;
}

bool sendMouseInput(DWORD flags, DWORD mouseData = 0)
{
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flags;
    input.mi.mouseData = mouseData;
    return SendInput(1, &input, sizeof(INPUT)) == 1;
}

bool injectFocusClickAt(const POINT &pt)
{
    POINT previous{};
    const BOOL hadPrevious = GetCursorPos(&previous);

    if (!SetCursorPos(pt.x, pt.y)) {
        return false;
    }

    const bool down = sendMouseInput(MOUSEEVENTF_LEFTDOWN);
    const bool up = sendMouseInput(MOUSEEVENTF_LEFTUP);

    if (hadPrevious) {
        SetCursorPos(previous.x, previous.y);
    }

    return down && up;
}

bool injectWheelStep(int wheelDelta)
{
    return sendMouseInput(MOUSEEVENTF_WHEEL, static_cast<DWORD>(wheelDelta));
}

bool injectWheelStepToWindow(HWND hwnd, const POINT &screenPoint, short wheelDelta)
{
    if (!hwnd || !IsWindow(hwnd)) {
        return false;
    }

    const WPARAM wParam = MAKEWPARAM(0, static_cast<WORD>(wheelDelta));
    const LPARAM lParam = MAKELPARAM(static_cast<short>(screenPoint.x), static_cast<short>(screenPoint.y));
    return PostMessageW(hwnd, WM_MOUSEWHEEL, wParam, lParam) != 0;
}

enum class DriverMode {
    None,
    UIAPattern,
    SyntheticWheel
};

class ScrollAutomationDriverWin final : public IScrollAutomationDriver
{
public:
    explicit ScrollAutomationDriverWin(QObject *parent = nullptr)
        : IScrollAutomationDriver(parent)
    {
        const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        m_comInitialized = SUCCEEDED(initHr);
        if (initHr == RPC_E_CHANGED_MODE) {
            m_comInitialized = false;
        }

        const HRESULT hr = CoCreateInstance(
            CLSID_CUIAutomation,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&m_uia));
        if (FAILED(hr)) {
            m_uia = nullptr;
        }
    }

    ~ScrollAutomationDriverWin() override
    {
        reset();
        if (m_uia) {
            m_uia->Release();
            m_uia = nullptr;
        }
        if (m_comInitialized) {
            CoUninitialize();
        }
    }

    ScrollProbeResult probeAt(const QPoint &globalPoint, QScreen *screen) override
    {
        reset();

        ScrollProbeResult result;
        result.mode = ScrollAutomationMode::ManualOnly;
        result.anchorPoint = globalPoint;
        m_anchorPoint = logicalToPhysicalPoint(globalPoint, screen);
        m_hasAnchorPoint = true;
        m_targetWindow = WindowFromPoint(m_anchorPoint);

        if (!m_uia) {
            m_mode = DriverMode::SyntheticWheel;
            result.mode = ScrollAutomationMode::AutoSynthetic;
            result.reason = QStringLiteral("UI Automation unavailable; using synthetic wheel fallback");
            result.focusRecommended = true;
            return result;
        }

        const POINT pt = logicalToPhysicalPoint(globalPoint, screen);
        IUIAutomationElement *element = nullptr;
        HRESULT hr = m_uia->ElementFromPoint(pt, &element);
        if (FAILED(hr) || !element) {
            m_mode = DriverMode::SyntheticWheel;
            result.mode = ScrollAutomationMode::AutoSynthetic;
            result.reason = QStringLiteral("No UIA element at cursor; using synthetic wheel fallback");
            result.focusRecommended = true;
            return result;
        }

        IUIAutomationTreeWalker *walker = nullptr;
        m_uia->get_ControlViewWalker(&walker);

        IUIAutomationElement *current = element;
        constexpr int kMaxDepth = 64;
        for (int depth = 0; depth < kMaxDepth && current; ++depth) {
            IUIAutomationScrollPattern *scrollPattern = nullptr;
            hr = current->GetCurrentPatternAs(UIA_ScrollPatternId, IID_PPV_ARGS(&scrollPattern));
            if (SUCCEEDED(hr) && scrollPattern) {
                double percent = UIA_ScrollPatternNoScroll;
                hr = scrollPattern->get_CurrentVerticalScrollPercent(&percent);
                scrollPattern->Release();
                if (SUCCEEDED(hr) && percent != UIA_ScrollPatternNoScroll) {
                    m_scrollElement = current;
                    m_scrollElement->AddRef();
                    m_lastVerticalPercent = percent;
                    m_mode = DriverMode::UIAPattern;
                    result.mode = ScrollAutomationMode::Auto;
                    result.reason = QStringLiteral("UI Automation scroll pattern active");
                    break;
                }
            }

            if (!walker) {
                break;
            }

            IUIAutomationElement *parent = nullptr;
            hr = walker->GetParentElement(current, &parent);
            if (depth > 0) {
                current->Release();
            }
            current = SUCCEEDED(hr) ? parent : nullptr;
        }

        const bool currentIsElement = (current == element);
        if (current) {
            current->Release();
        }
        if (element && !currentIsElement) {
            element->Release();
        }
        if (walker) {
            walker->Release();
        }

        if (result.mode != ScrollAutomationMode::Auto) {
            m_mode = DriverMode::SyntheticWheel;
            result.mode = ScrollAutomationMode::AutoSynthetic;
            result.reason = QStringLiteral("No vertical ScrollPattern found; using synthetic wheel fallback");
            result.focusRecommended = true;
        }
        return result;
    }

    ScrollStepResult step() override
    {
        ScrollStepResult result;
        if (m_mode == DriverMode::SyntheticWheel) {
            if (!m_hasAnchorPoint) {
                result.error = QStringLiteral("Synthetic anchor unavailable");
                return result;
            }

            const bool shouldRefocus = !m_focusInjected || m_syntheticStepsSinceFocus >= 12;
            if (shouldRefocus) {
                m_focusInjected = focusTarget();
                m_syntheticStepsSinceFocus = 0;
            }

            bool injected = false;
            bool targetLocked = false;

            if (m_targetWindow) {
                injected = injectWheelStepToWindow(m_targetWindow, m_anchorPoint, static_cast<short>(-WHEEL_DELTA * 2));
                targetLocked = injected;
            }
            if (!injected) {
                injected = injectWheelStep(-WHEEL_DELTA * 2);
            }

            if (!injected) {
                result.error = QStringLiteral("Synthetic wheel injection failed");
                return result;
            }

            result.status = ScrollStepStatus::Stepped;
            result.estimatedDeltaY = 180;
            result.inputInjected = true;
            result.targetLocked = targetLocked;
            ++m_syntheticStepsSinceFocus;
            return result;
        }

        if (!m_scrollElement) {
            result.error = QStringLiteral("Scroll probe not initialized");
            return result;
        }

        IUIAutomationScrollPattern *scrollPattern = nullptr;
        HRESULT hr = m_scrollElement->GetCurrentPatternAs(UIA_ScrollPatternId, IID_PPV_ARGS(&scrollPattern));
        if (FAILED(hr) || !scrollPattern) {
            result.error = QStringLiteral("ScrollPattern unavailable");
            return result;
        }

        double before = UIA_ScrollPatternNoScroll;
        hr = scrollPattern->get_CurrentVerticalScrollPercent(&before);
        if (FAILED(hr) || before == UIA_ScrollPatternNoScroll) {
            scrollPattern->Release();
            result.status = ScrollStepStatus::EndReached;
            return result;
        }
        if (before >= 99.99) {
            scrollPattern->Release();
            result.status = ScrollStepStatus::EndReached;
            return result;
        }

        hr = scrollPattern->Scroll(ScrollAmount_NoAmount, ScrollAmount_LargeIncrement);
        if (FAILED(hr)) {
            scrollPattern->Release();
            result.error = QStringLiteral("Scroll step failed");
            return result;
        }

        double after = before;
        hr = scrollPattern->get_CurrentVerticalScrollPercent(&after);
        scrollPattern->Release();
        if (FAILED(hr)) {
            result.status = ScrollStepStatus::Stepped;
            result.estimatedDeltaY = 0;
            return result;
        }

        if (after <= before + 0.001) {
            result.status = ScrollStepStatus::EndReached;
            return result;
        }

        result.status = ScrollStepStatus::Stepped;
        result.estimatedDeltaY = static_cast<int>(after - before);
        m_lastVerticalPercent = after;
        result.targetLocked = true;
        return result;
    }

    bool focusTarget() override
    {
        if (!m_hasAnchorPoint) {
            return false;
        }

        const bool ok = injectFocusClickAt(m_anchorPoint);
        if (ok) {
            m_focusInjected = true;
            m_syntheticStepsSinceFocus = 0;
        }
        return ok;
    }

    bool forceSyntheticFallback() override
    {
        if (!m_hasAnchorPoint) {
            return false;
        }

        if (m_scrollElement) {
            m_scrollElement->Release();
            m_scrollElement = nullptr;
        }
        m_mode = DriverMode::SyntheticWheel;
        m_focusInjected = false;
        m_syntheticStepsSinceFocus = 0;
        return true;
    }

    void reset() override
    {
        if (m_scrollElement) {
            m_scrollElement->Release();
            m_scrollElement = nullptr;
        }
        m_mode = DriverMode::None;
        m_lastVerticalPercent = 0.0;
        m_anchorPoint = POINT{};
        m_hasAnchorPoint = false;
        m_targetWindow = nullptr;
        m_focusInjected = false;
        m_syntheticStepsSinceFocus = 0;
    }

private:
    bool m_comInitialized = false;
    IUIAutomation *m_uia = nullptr;
    IUIAutomationElement *m_scrollElement = nullptr;
    DriverMode m_mode = DriverMode::None;
    double m_lastVerticalPercent = 0.0;
    POINT m_anchorPoint{};
    bool m_hasAnchorPoint = false;
    HWND m_targetWindow = nullptr;
    bool m_focusInjected = false;
    int m_syntheticStepsSinceFocus = 0;
};

} // namespace

IScrollAutomationDriver* createPlatformScrollAutomationDriver(QObject *parent)
{
    return new ScrollAutomationDriverWin(parent);
}

bool isPlatformScrollAutomationAvailable()
{
    return true;
}

#else

IScrollAutomationDriver* createPlatformScrollAutomationDriver(QObject *parent)
{
    Q_UNUSED(parent);
    return nullptr;
}

bool isPlatformScrollAutomationAvailable()
{
    return false;
}

#endif
