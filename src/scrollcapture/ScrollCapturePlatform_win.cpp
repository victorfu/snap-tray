// Scroll area detection and scroll injection for Windows, through UI
// Automation (ScrollPattern) and SendInput.
#include "scrollcapture/ScrollAreaLocator.h"
#include "scrollcapture/ScrollInjector.h"
#include "scrollcapture/ScrollTargetPoints.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QScreen>
#include <QThread>

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <uiautomation.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace snaptray::scrollcapture {

namespace {

constexpr int kMaxAncestorWalk = 64;
constexpr DWORD kUiaTimeoutMs = 200;
constexpr double kEdgePercentEpsilon = 0.01;
constexpr double kPercentScale = 100.0;
constexpr int kVersionStringLength = 64;

struct ControlTypeName {
    CONTROLTYPEID id;
    const char* name;
};

constexpr ControlTypeName kControlTypeNames[] = {
    {UIA_PaneControlTypeId, "Pane"},
    {UIA_DocumentControlTypeId, "Document"},
    {UIA_ListControlTypeId, "List"},
    {UIA_TreeControlTypeId, "Tree"},
    {UIA_DataGridControlTypeId, "DataGrid"},
    {UIA_TableControlTypeId, "Table"},
    {UIA_EditControlTypeId, "Edit"},
    {UIA_GroupControlTypeId, "Group"},
    {UIA_CustomControlTypeId, "Custom"},
    {UIA_WindowControlTypeId, "Window"},
    {UIA_TextControlTypeId, "Text"},
    {UIA_TabControlTypeId, "Tab"},
    {UIA_ComboBoxControlTypeId, "ComboBox"},
    {UIA_MenuControlTypeId, "Menu"},
    {UIA_ScrollBarControlTypeId, "ScrollBar"},
    {UIA_ListItemControlTypeId, "ListItem"},
    {UIA_TreeItemControlTypeId, "TreeItem"},
    {UIA_DataItemControlTypeId, "DataItem"},
    {UIA_HyperlinkControlTypeId, "Hyperlink"},
    {UIA_ImageControlTypeId, "Image"},
    {UIA_ButtonControlTypeId, "Button"},
};

QString controlTypeName(CONTROLTYPEID id)
{
    for (const auto& entry : kControlTypeNames) {
        if (entry.id == id) {
            return QLatin1String(entry.name);
        }
    }
    return QStringLiteral("ControlType%1").arg(id);
}

QString takeBstr(BSTR value)
{
    QString text = value ? QString::fromWCharArray(value, static_cast<int>(SysStringLen(value))) : QString();
    SysFreeString(value);
    return text;
}

QString hresultText(HRESULT hr)
{
    return QStringLiteral("0x%1").arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0'));
}

// Qt keeps each screen's top-left corner in native pixels and scales from
// there, so logical and physical points map through the screen they are on.
QPoint logicalToPhysical(const QPoint& point)
{
    QScreen* screen = QGuiApplication::screenAt(point);
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return point;
    }
    const QPoint origin = screen->geometry().topLeft();
    return origin + (point - origin) * screen->devicePixelRatio();
}

QPointF physicalToLogical(const QPointF& point)
{
    for (QScreen* screen : QGuiApplication::screens()) {
        const qreal ratio = screen->devicePixelRatio();
        const QRect geometry = screen->geometry();
        const QRectF native(QPointF(geometry.topLeft()), QSizeF(geometry.size()) * ratio);
        if (native.contains(point)) {
            return QPointF(geometry.topLeft()) + (point - QPointF(geometry.topLeft())) / ratio;
        }
    }
    return point;
}

QRect physicalToLogical(const RECT& rect)
{
    const QPointF topLeft = physicalToLogical(QPointF(rect.left, rect.top));
    const QPointF bottomRight = physicalToLogical(QPointF(rect.right, rect.bottom));
    return QRectF(topLeft, bottomRight).toAlignedRect();
}

QJsonObject rectToJson(const RECT& rect)
{
    return QJsonObject{
        {QStringLiteral("left"), static_cast<int>(rect.left)},
        {QStringLiteral("top"), static_cast<int>(rect.top)},
        {QStringLiteral("right"), static_cast<int>(rect.right)},
        {QStringLiteral("bottom"), static_cast<int>(rect.bottom)},
    };
}

// Mandatory integrity level (SECURITY_MANDATORY_*_RID) of a process.
std::optional<DWORD> processIntegrityLevel(DWORD processId)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) {
        return std::nullopt;
    }
    std::optional<DWORD> level;
    HANDLE token = nullptr;
    if (OpenProcessToken(process, TOKEN_QUERY, &token)) {
        DWORD size = 0;
        GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &size);
        std::vector<BYTE> buffer(size);
        if (size > 0 && GetTokenInformation(token, TokenIntegrityLevel, buffer.data(), size, &size)) {
            const auto* label = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(buffer.data());
            const UCHAR count = *GetSidSubAuthorityCount(label->Label.Sid);
            if (count > 0) {
                level = *GetSidSubAuthority(label->Label.Sid, count - 1);
            }
        }
        CloseHandle(token);
    }
    CloseHandle(process);
    return level;
}

struct ProcessInfo {
    QString name;
    QString version;
    std::optional<DWORD> integrity;
};

QString fileVersion(const std::wstring& path)
{
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (size == 0) {
        return {};
    }
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) {
        return {};
    }
    VS_FIXEDFILEINFO* info = nullptr;
    UINT length = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) || !info) {
        return {};
    }
    return QStringLiteral("%1.%2.%3.%4")
        .arg(HIWORD(info->dwProductVersionMS))
        .arg(LOWORD(info->dwProductVersionMS))
        .arg(HIWORD(info->dwProductVersionLS))
        .arg(LOWORD(info->dwProductVersionLS));
}

ProcessInfo readProcessInfo(DWORD processId)
{
    ProcessInfo info;
    info.integrity = processIntegrityLevel(processId);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) {
        return info;
    }
    std::wstring path(MAX_PATH, L'\0');
    DWORD length = static_cast<DWORD>(path.size());
    if (QueryFullProcessImageNameW(process, 0, path.data(), &length)) {
        path.resize(length);
        info.name = QFileInfo(QString::fromStdWString(path)).completeBaseName();
        info.version = fileVersion(path);
    }
    CloseHandle(process);
    return info;
}

class WinScrollArea final : public ScrollArea
{
public:
    WinScrollArea(ComPtr<IUIAutomation> automation, ComPtr<IUIAutomationTreeWalker> walker,
                  ComPtr<IUIAutomationElement> element, ComPtr<IUIAutomationScrollPattern> pattern,
                  DWORD processId, int level, int availableLevels, ProcessInfo process,
                  QJsonObject hitDiagnostics)
        : m_automation(std::move(automation))
        , m_walker(std::move(walker))
        , m_element(std::move(element))
        , m_pattern(std::move(pattern))
        , m_processId(processId)
        , m_level(level)
        , m_availableLevels(availableLevels)
        , m_process(std::move(process))
        , m_hitDiagnostics(std::move(hitDiagnostics))
    {
    }

    IUIAutomationScrollPattern* pattern() const { return m_pattern.Get(); }

    // Whether this area is the innermost vertically scrollable element at a
    // physical point, i.e. the one wheel input there would scroll.
    bool isInnermostScrollAreaAt(const POINT& point) const
    {
        ComPtr<IUIAutomationElement> current;
        if (FAILED(m_automation->ElementFromPoint(point, &current))) {
            return false;
        }
        for (int depth = 0; depth < kMaxAncestorWalk && current; ++depth) {
            ComPtr<IUIAutomationScrollPattern> pattern;
            BOOL vertical = FALSE;
            if (SUCCEEDED(current->GetCurrentPatternAs(UIA_ScrollPatternId, IID_PPV_ARGS(&pattern))) && pattern &&
                SUCCEEDED(pattern->get_CurrentVerticallyScrollable(&vertical)) && vertical) {
                BOOL same = FALSE;
                return SUCCEEDED(m_automation->CompareElements(current.Get(), m_element.Get(), &same)) && same;
            }
            ComPtr<IUIAutomationElement> parent;
            if (FAILED(m_walker->GetParentElement(current.Get(), &parent))) {
                return false;
            }
            current = parent;
        }
        return false;
    }

    ScrollAreaSnapshot snapshot() const override
    {
        ScrollAreaSnapshot snapshot;
        snapshot.level = m_level;
        snapshot.availableLevels = m_availableLevels;
        snapshot.processId = m_processId;
        snapshot.appName = m_process.name;
        snapshot.appVersion = m_process.version;
        snapshot.diagnostics = m_hitDiagnostics;

        CONTROLTYPEID controlType = 0;
        if (SUCCEEDED(m_element->get_CurrentControlType(&controlType))) {
            snapshot.role = controlTypeName(controlType);
        }
        BSTR className = nullptr;
        if (SUCCEEDED(m_element->get_CurrentClassName(&className))) {
            snapshot.diagnostics.insert(QStringLiteral("className"), takeBstr(className));
        }
        BSTR frameworkId = nullptr;
        if (SUCCEEDED(m_element->get_CurrentFrameworkId(&frameworkId))) {
            snapshot.diagnostics.insert(QStringLiteral("frameworkId"), takeBstr(frameworkId));
        }

        RECT rect{};
        if (FAILED(m_element->get_CurrentBoundingRectangle(&rect))) {
            snapshot.diagnostics.insert(QStringLiteral("boundsReadable"), false);
            return snapshot;
        }
        snapshot.bounds = physicalToLogical(rect);
        snapshot.diagnostics.insert(QStringLiteral("physicalBounds"), rectToJson(rect));
        readPosition(snapshot);
        return snapshot;
    }

private:
    void readPosition(ScrollAreaSnapshot& snapshot) const
    {
        ScrollPosition& position = snapshot.position;
        double percent = 0.0;
        double viewSize = 0.0;
        BOOL scrollable = FALSE;
        const HRESULT percentResult = m_pattern->get_CurrentVerticalScrollPercent(&percent);
        const HRESULT viewResult = m_pattern->get_CurrentVerticalViewSize(&viewSize);
        m_pattern->get_CurrentVerticallyScrollable(&scrollable);
        snapshot.diagnostics.insert(QStringLiteral("verticallyScrollable"), scrollable != FALSE);
        if (FAILED(percentResult)) {
            snapshot.diagnostics.insert(QStringLiteral("scrollPercentError"), hresultText(percentResult));
            return;
        }
        snapshot.diagnostics.insert(QStringLiteral("verticalScrollPercent"), percent);
        if (SUCCEEDED(viewResult)) {
            snapshot.diagnostics.insert(QStringLiteral("verticalViewSize"), viewSize);
        }

        if (percent == UIA_ScrollPatternNoScroll) {
            snapshot.diagnostics.insert(QStringLiteral("noScroll"), true);
            position.atTop = true;
            position.atBottom = true;
            position.sources << QStringLiteral("ScrollPattern.NoScroll");
            return;
        }

        position.fraction = std::clamp(percent / kPercentScale, 0.0, 1.0);
        position.sources << QStringLiteral("ScrollPattern.VerticalScrollPercent");
        position.atTop = percent <= kEdgePercentEpsilon;
        position.atBottom = percent >= kPercentScale - kEdgePercentEpsilon;
        if (SUCCEEDED(viewResult) && viewSize > 0.0) {
            const double ratio = std::min(1.0, viewSize / kPercentScale);
            const double viewport = snapshot.bounds.height();
            position.viewportRatio = ratio;
            position.contentHeight = viewport / ratio;
            position.offset = *position.fraction * (*position.contentHeight - viewport);
            position.sources << QStringLiteral("ScrollPattern.VerticalViewSize");
        }
    }

    ComPtr<IUIAutomation> m_automation;
    ComPtr<IUIAutomationTreeWalker> m_walker;
    ComPtr<IUIAutomationElement> m_element;
    ComPtr<IUIAutomationScrollPattern> m_pattern;
    DWORD m_processId = 0;
    int m_level = 0;
    int m_availableLevels = 0;
    ProcessInfo m_process;
    QJsonObject m_hitDiagnostics;
};

class WinScrollAreaLocator final : public ScrollAreaLocator
{
public:
    WinScrollAreaLocator()
    {
        const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        m_comInitialized = SUCCEEDED(init);

        ComPtr<IUIAutomation2> automation2;
        if (SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation8), nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&automation2)))) {
            automation2->put_ConnectionTimeout(kUiaTimeoutMs);
            automation2->put_TransactionTimeout(kUiaTimeoutMs);
            m_automation = automation2;
        } else {
            CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_automation));
        }
        if (m_automation) {
            m_automation->get_RawViewWalker(&m_walker);
        }
        m_ownIntegrity = processIntegrityLevel(GetCurrentProcessId());
    }

    ~WinScrollAreaLocator() override
    {
        m_walker.Reset();
        m_automation.Reset();
        if (m_comInitialized) {
            CoUninitialize();
        }
    }

    bool isAvailable() const override { return m_automation && m_walker; }

    LocateResult locateAt(const QPoint& globalPos, const LocateOptions& options) override
    {
        LocateResult result;
        if (!isAvailable()) {
            result.status = LocateStatus::Failed;
            result.detail = QStringLiteral("UI Automation is not available");
            return result;
        }

        const int maxAttempts = std::max(1, options.maxAttempts);
        for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
            result = locateOnce(globalPos, options.level);
            result.attempts = attempt;
            if (result.status != LocateStatus::NoScrollArea || attempt == maxAttempts) {
                break;
            }
            QThread::msleep(static_cast<unsigned long>(std::max(0, options.retryDelayMs)));
        }
        return result;
    }

    QJsonObject environment() const override
    {
        QJsonObject environment;
        UINT wheelLines = 0;
        if (SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &wheelLines, 0)) {
            environment.insert(QStringLiteral("wheelScrollLines"), static_cast<int>(wheelLines));
        }
        BOOL smoothListBoxes = FALSE;
        if (SystemParametersInfoW(SPI_GETLISTBOXSMOOTHSCROLLING, 0, &smoothListBoxes, 0)) {
            environment.insert(QStringLiteral("listBoxSmoothScrolling"), smoothListBoxes != FALSE);
        }
        DWORD routing = 0;
        DWORD size = sizeof(routing);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"MouseWheelRouting",
                         RRF_RT_REG_DWORD, nullptr, &routing, &size) == ERROR_SUCCESS) {
            environment.insert(QStringLiteral("mouseWheelRouting"), static_cast<int>(routing));
        }
        if (m_ownIntegrity) {
            environment.insert(QStringLiteral("probeIntegrityLevel"), static_cast<qint64>(*m_ownIntegrity));
        }
        return environment;
    }

private:
    LocateResult locateOnce(const QPoint& globalPos, int level) const
    {
        LocateResult result;
        const QPoint physical = logicalToPhysical(globalPos);
        const POINT point{physical.x(), physical.y()};
        const HWND hit = WindowFromPoint(point);
        const HWND root = hit ? GetAncestor(hit, GA_ROOT) : nullptr;
        DWORD processId = 0;
        if (root) {
            GetWindowThreadProcessId(root, &processId);
        }
        if (!root || processId == 0 || processId == GetCurrentProcessId()) {
            result.status = LocateStatus::NoWindow;
            result.detail = QStringLiteral("no other application's window under the point");
            return result;
        }
        result.processId = processId;

        ProcessInfo process = readProcessInfo(processId);
        QJsonObject hitDiagnostics{
            {QStringLiteral("physicalPoint"), QJsonObject{{QStringLiteral("x"), physical.x()},
                                                          {QStringLiteral("y"), physical.y()}}},
        };
        if (process.integrity) {
            hitDiagnostics.insert(QStringLiteral("targetIntegrityLevel"), static_cast<qint64>(*process.integrity));
        }
        // UIPI silently drops input to, and hides UI of, higher-integrity
        // windows; neither SendInput nor UIA reports it.
        const bool blocked = process.integrity && m_ownIntegrity && *process.integrity > *m_ownIntegrity;
        if (blocked) {
            result.status = LocateStatus::Blocked;
            result.detail = QStringLiteral("%1 runs at a higher integrity level (elevated); "
                                           "Windows blocks input and UI Automation from this process")
                                .arg(process.name);
            return result;
        }

        ComPtr<IUIAutomationElement> current;
        const HRESULT hitResult = m_automation->ElementFromPoint(point, &current);
        if (FAILED(hitResult) || !current) {
            result.status = LocateStatus::NoScrollArea;
            result.detail = QStringLiteral("ElementFromPoint failed (%1)").arg(hresultText(hitResult));
            return result;
        }

        struct Candidate {
            ComPtr<IUIAutomationElement> element;
            ComPtr<IUIAutomationScrollPattern> pattern;
        };
        std::vector<Candidate> candidates;
        QJsonArray rolePath;
        int horizontalOnly = 0;
        for (int depth = 0; depth < kMaxAncestorWalk && current; ++depth) {
            CONTROLTYPEID controlType = 0;
            current->get_CurrentControlType(&controlType);
            rolePath.append(controlTypeName(controlType));

            ComPtr<IUIAutomationScrollPattern> pattern;
            if (SUCCEEDED(current->GetCurrentPatternAs(UIA_ScrollPatternId, IID_PPV_ARGS(&pattern))) && pattern) {
                BOOL vertical = FALSE;
                if (SUCCEEDED(pattern->get_CurrentVerticallyScrollable(&vertical)) && vertical) {
                    candidates.push_back({current, pattern});
                } else {
                    ++horizontalOnly;
                }
            }

            UIA_HWND nativeWindow = nullptr;
            current->get_CurrentNativeWindowHandle(&nativeWindow);
            if (static_cast<HWND>(nativeWindow) == root) {
                break;
            }
            ComPtr<IUIAutomationElement> parent;
            if (FAILED(m_walker->GetParentElement(current.Get(), &parent))) {
                break;
            }
            current = parent;
        }
        hitDiagnostics.insert(QStringLiteral("rolePath"), rolePath);
        hitDiagnostics.insert(QStringLiteral("nonVerticalScrollPatterns"), horizontalOnly);

        const int available = static_cast<int>(candidates.size());
        if (level < 0 || level >= available) {
            result.status = LocateStatus::NoScrollArea;
            result.detail = available == 0
                ? QStringLiteral("no vertically scrollable ScrollPattern between the point and its window")
                : QStringLiteral("only %1 nested scroll area(s) under the point").arg(available);
            return result;
        }

        Candidate& chosen = candidates[static_cast<size_t>(level)];
        result.status = LocateStatus::Found;
        result.area = std::make_unique<WinScrollArea>(
            m_automation, m_walker, chosen.element, chosen.pattern, processId, level, available, std::move(process),
            std::move(hitDiagnostics));
        return result;
    }

    bool m_comInitialized = false;
    ComPtr<IUIAutomation> m_automation;
    ComPtr<IUIAutomationTreeWalker> m_walker;
    std::optional<DWORD> m_ownIntegrity;
};

class WinScrollInjector final : public ScrollInjector
{
public:
    QList<ScrollMethod> supportedMethods() const override
    {
        return {ScrollMethod::WheelDelta, ScrollMethod::PatternSmallIncrement, ScrollMethod::PatternSetPercent};
    }

    InjectResult scroll(const ScrollArea& area, const ScrollRequest& request) override
    {
        const auto* windowsArea = dynamic_cast<const WinScrollArea*>(&area);
        if (!windowsArea) {
            return {false, QStringLiteral("not a Windows scroll area")};
        }
        if (request.method == ScrollMethod::WheelDelta) {
            return sendWheel(*windowsArea, request.amount);
        }
        if (request.method == ScrollMethod::PatternSmallIncrement) {
            return scrollIncrements(*windowsArea, request.amount);
        }
        if (request.method == ScrollMethod::PatternSetPercent) {
            const double percent = std::clamp(request.percent, 0.0, kPercentScale);
            const HRESULT hr = windowsArea->pattern()->SetScrollPercent(UIA_ScrollPatternNoScroll, percent);
            return {SUCCEEDED(hr), QStringLiteral("SetScrollPercent(%1) %2").arg(percent).arg(hresultText(hr))};
        }
        return {false, QStringLiteral("%1 is not available on Windows").arg(QLatin1String(scrollMethodName(request.method)))};
    }

private:
    static InjectResult sendWheel(const WinScrollArea& area, int amount)
    {
        const QRect bounds = area.snapshot().bounds;
        if (bounds.isEmpty()) {
            return {false, QStringLiteral("scroll area bounds are not readable")};
        }
        // Wheel input goes to the innermost scroll area under the cursor, so
        // use a point where that is the selected area itself.
        std::optional<QPoint> chosen;
        for (const QPoint& point : scrollTargetPoints(bounds)) {
            const QPoint physical = logicalToPhysical(point);
            if (area.isInnermostScrollAreaAt(POINT{physical.x(), physical.y()})) {
                chosen = physical;
                break;
            }
        }
        if (!chosen) {
            return {false, QStringLiteral("no point inside the selected scroll area reaches it: "
                                          "nested scroll areas or other windows cover every probe point")};
        }
        const QPoint centre = *chosen;
        if (!SetCursorPos(centre.x(), centre.y())) {
            return {false, QStringLiteral("SetCursorPos failed")};
        }
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_WHEEL;
        // Positive wheel data scrolls toward the top, so negate.
        input.mi.mouseData = static_cast<DWORD>(-amount);
        const UINT sent = SendInput(1, &input, sizeof(INPUT));
        return {sent == 1, QStringLiteral("SendInput wheel %1 at (%2, %3)").arg(-amount).arg(centre.x()).arg(centre.y())};
    }

    static InjectResult scrollIncrements(const WinScrollArea& area, int amount)
    {
        const ScrollAmount step = amount >= 0 ? ScrollAmount_SmallIncrement : ScrollAmount_SmallDecrement;
        const int count = std::abs(amount);
        for (int i = 0; i < count; ++i) {
            const HRESULT hr = area.pattern()->Scroll(ScrollAmount_NoAmount, step);
            if (FAILED(hr)) {
                return {false, QStringLiteral("Scroll failed at step %1 (%2)").arg(i + 1).arg(hresultText(hr))};
            }
        }
        return {true, QStringLiteral("%1 small %2(s)").arg(count).arg(QLatin1String(amount >= 0 ? "increment" : "decrement"))};
    }
};

} // namespace

std::unique_ptr<ScrollAreaLocator> ScrollAreaLocator::create()
{
    return std::make_unique<WinScrollAreaLocator>();
}

std::unique_ptr<ScrollInjector> ScrollInjector::create()
{
    return std::make_unique<WinScrollInjector>();
}

} // namespace snaptray::scrollcapture
