#include "recording/WindowTimeline.h"

#include "utils/CoordinateHelper.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace SnapTray {

namespace {

constexpr auto kKeyVersion = "version";
constexpr auto kKeyFrameSize = "frameSize";
constexpr auto kKeyEntries = "entries";
constexpr auto kKeyTime = "t";
constexpr auto kKeyWindows = "windows";
constexpr auto kKeyId = "id";
constexpr auto kKeyRect = "rect";
constexpr auto kKeyZ = "z";
constexpr auto kKeyApp = "app";
constexpr auto kKeyType = "type";
constexpr auto kTypeWindow = "window";
constexpr auto kTypeDialog = "dialog";
constexpr int kRectComponents = 4;

QJsonArray rectToJson(const QRect& rect)
{
    return QJsonArray{rect.x(), rect.y(), rect.width(), rect.height()};
}

std::optional<QRect> rectFromJson(const QJsonValue& value)
{
    const QJsonArray array = value.toArray();
    if (array.size() != kRectComponents) {
        return std::nullopt;
    }
    for (const QJsonValue& component : array) {
        if (!component.isDouble()) {
            return std::nullopt;
        }
    }
    return QRect(array[0].toInt(), array[1].toInt(), array[2].toInt(), array[3].toInt());
}

std::optional<ElementType> typeFromJson(const QString& type)
{
    if (type == QLatin1String(kTypeWindow)) {
        return ElementType::Window;
    }
    if (type == QLatin1String(kTypeDialog)) {
        return ElementType::Dialog;
    }
    return std::nullopt;
}

} // namespace

bool WindowSample::operator==(const WindowSample& other) const
{
    return windowId == other.windowId && rect == other.rect && z == other.z && ownerApp == other.ownerApp
        && type == other.type;
}

void WindowTimeline::append(qint64 tMs, std::vector<WindowSample> windows)
{
    if (!m_entries.empty() && m_entries.back().windows == windows) {
        return;
    }
    m_entries.push_back({tMs, std::move(windows)});
}

const std::vector<WindowSample>* WindowTimeline::windowsAt(qint64 tMs) const
{
    if (m_entries.empty()) {
        return nullptr;
    }
    // First entry with t > tMs; the one before it is the entry in force.
    const auto after = std::upper_bound(m_entries.begin(), m_entries.end(), tMs,
                                        [](qint64 t, const WindowTimelineEntry& entry) { return t < entry.tMs; });
    if (after == m_entries.begin()) {
        return &m_entries.front().windows;
    }
    return &std::prev(after)->windows;
}

std::optional<WindowSample> WindowTimeline::hitTest(const QPoint& videoPoint, qint64 tMs) const
{
    const std::vector<WindowSample>* windows = windowsAt(tMs);
    if (!windows) {
        return std::nullopt;
    }
    std::optional<WindowSample> best;
    for (const WindowSample& window : *windows) {
        if (window.rect.contains(videoPoint) && (!best || window.z < best->z)) {
            best = window;
        }
    }
    return best;
}

QByteArray WindowTimeline::toJson() const
{
    QJsonArray entries;
    for (const WindowTimelineEntry& entry : m_entries) {
        QJsonArray windows;
        for (const WindowSample& window : entry.windows) {
            windows.append(QJsonObject{
                {QLatin1String(kKeyId), static_cast<qint64>(window.windowId)},
                {QLatin1String(kKeyRect), rectToJson(window.rect)},
                {QLatin1String(kKeyZ), window.z},
                {QLatin1String(kKeyApp), window.ownerApp},
                {QLatin1String(kKeyType),
                 QLatin1String(window.type == ElementType::Dialog ? kTypeDialog : kTypeWindow)},
            });
        }
        entries.append(QJsonObject{{QLatin1String(kKeyTime), entry.tMs}, {QLatin1String(kKeyWindows), windows}});
    }
    const QJsonObject root{
        {QLatin1String(kKeyVersion), kFormatVersion},
        {QLatin1String(kKeyFrameSize), QJsonArray{m_frameSize.width(), m_frameSize.height()}},
        {QLatin1String(kKeyEntries), entries},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

std::optional<WindowTimeline> WindowTimeline::fromJson(const QByteArray& json)
{
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (!document.isObject()) {
        return std::nullopt;
    }
    const QJsonObject root = document.object();
    if (root.value(QLatin1String(kKeyVersion)).toInt() != kFormatVersion) {
        return std::nullopt;
    }
    const QJsonArray frameSize = root.value(QLatin1String(kKeyFrameSize)).toArray();
    if (frameSize.size() != 2) {
        return std::nullopt;
    }
    WindowTimeline timeline;
    timeline.m_frameSize = QSize(frameSize[0].toInt(), frameSize[1].toInt());
    if (timeline.m_frameSize.isEmpty()) {
        return std::nullopt;
    }
    for (const QJsonValue& entryValue : root.value(QLatin1String(kKeyEntries)).toArray()) {
        const QJsonObject entryObject = entryValue.toObject();
        WindowTimelineEntry entry;
        entry.tMs = static_cast<qint64>(entryObject.value(QLatin1String(kKeyTime)).toDouble());
        for (const QJsonValue& windowValue : entryObject.value(QLatin1String(kKeyWindows)).toArray()) {
            const QJsonObject windowObject = windowValue.toObject();
            const std::optional<QRect> rect = rectFromJson(windowObject.value(QLatin1String(kKeyRect)));
            const std::optional<ElementType> type = typeFromJson(windowObject.value(QLatin1String(kKeyType)).toString());
            if (!rect || !type) {
                return std::nullopt;
            }
            WindowSample window;
            window.windowId = static_cast<quint32>(windowObject.value(QLatin1String(kKeyId)).toDouble());
            window.rect = *rect;
            window.z = windowObject.value(QLatin1String(kKeyZ)).toInt();
            window.ownerApp = windowObject.value(QLatin1String(kKeyApp)).toString();
            window.type = *type;
            entry.windows.push_back(window);
        }
        timeline.m_entries.push_back(std::move(entry));
    }
    return timeline;
}

bool WindowTimeline::hasWindows() const
{
    return std::any_of(m_entries.begin(), m_entries.end(),
                       [](const WindowTimelineEntry& entry) { return !entry.windows.empty(); });
}

WindowFrameMapping WindowFrameMapping::fromCapture(const QRect& logicalRegion, const QRect& logicalScreen,
                                                   const QRect& physicalScreen, qreal dpr,
                                                   const QRect& mappedPhysicalRegion)
{
    QRect region = mappedPhysicalRegion;
    if (physicalScreen.isEmpty()) {
        region.moveTopLeft(CoordinateHelper::toPhysicalCoveringRect(
            logicalRegion.translated(-logicalScreen.topLeft()), dpr).topLeft());
    }
    return {logicalScreen, physicalScreen, dpr, region};
}

QRect WindowFrameMapping::toVideoRectFromPhysical(const QRect& nativeBounds) const
{
    if (!isValid() || physicalScreen.isEmpty() || nativeBounds.isEmpty()) return {};
    return nativeBounds.intersected(physicalRegion).translated(-physicalRegion.topLeft());
}

QRect WindowFrameMapping::toVideoRect(const QRect& logicalBounds) const
{
    if (!isValid()) {
        return {};
    }
    // toPhysicalScreenRect() refuses a rect that leaves the screen, so clip
    // first: a window spanning two screens keeps the part on this one.
    const QRect onScreen = logicalBounds.intersected(logicalScreen);
    if (onScreen.isEmpty()) {
        return {};
    }
    QRect physical;
    if (!physicalScreen.isEmpty()) {
        physical = CoordinateHelper::toPhysicalScreenRect(onScreen, logicalScreen, physicalScreen, devicePixelRatio);
    } else {
        physical = CoordinateHelper::toPhysicalCoveringRect(onScreen.translated(-logicalScreen.topLeft()),
                                                           devicePixelRatio);
    }
    physical.translate(-physicalRegion.topLeft());
    return physical.intersected(QRect(QPoint(0, 0), physicalRegion.size()));
}

} // namespace SnapTray
