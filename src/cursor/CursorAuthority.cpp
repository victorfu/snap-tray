#include "cursor/CursorAuthority.h"

#include <QByteArray>
#include <QMetaObject>
#include <QObject>
#include <QWidget>
#include <QWindow>

#include <algorithm>

namespace {
QString groupTail(const QString& className)
{
    if (className.isEmpty()) {
        return QStringLiteral("UnknownSurface");
    }

    const QStringList parts = className.split(QStringLiteral("::"), Qt::SkipEmptyParts);
    return parts.isEmpty() ? className : parts.last();
}
}  // namespace

CursorAuthority& CursorAuthority::instance()
{
    static CursorAuthority authority;
    return authority;
}

CursorSurfaceMode CursorAuthority::defaultManagedModeForGroup(const QString& group)
{
    const QString normalized = normalizedGroupName(group);
    const QByteArray envKey = QStringLiteral("SNAPTRAY_CURSOR_MODE_GROUP_%1")
                                  .arg(normalized)
                                  .toUtf8();
    const QByteArray envValue = qgetenv(envKey.constData());
    if (!envValue.isEmpty()) {
        return parseModeOverride(QString::fromUtf8(envValue), CursorSurfaceMode::Shadow);
    }

    static const QStringList kAuthorityGroups = {
        QStringLiteral("REGIONSELECTOR"),
        QStringLiteral("SCREENCANVAS"),
        QStringLiteral("PINWINDOW"),
        QStringLiteral("QMLRECORDINGREGIONSELECTOR"),
        QStringLiteral("QMLRECORDINGCONTROLBAR"),
        QStringLiteral("QMLCOUNTDOWNOVERLAY"),
        QStringLiteral("RECORDINGPREVIEWBACKEND"),
        QStringLiteral("QMLDIALOG"),
        QStringLiteral("QMLBEAUTIFYPANEL"),
        QStringLiteral("QMLSETTINGSWINDOW"),
        QStringLiteral("QMLPINHISTORYWINDOW"),
        QStringLiteral("QMLFLOATINGTOOLBAR"),
        QStringLiteral("QMLFLOATINGSUBTOOLBAR"),
        QStringLiteral("QMLWINDOWEDTOOLBAR"),
        QStringLiteral("QMLOVERLAYPANEL"),
        QStringLiteral("QMLEMOJIPICKERPOPUP"),
    };

    return kAuthorityGroups.contains(normalized)
        ? CursorSurfaceMode::Authority
        : CursorSurfaceMode::Shadow;
}

QString CursorAuthority::defaultGroupForObject(const QObject* object) const
{
    if (!object || !object->metaObject()) {
        return QStringLiteral("UnknownSurface");
    }

    return groupTail(QString::fromLatin1(object->metaObject()->className()));
}

CursorSurfaceMode CursorAuthority::defaultManagedModeForWidget(const QWidget* widget) const
{
    return defaultManagedModeForGroup(defaultGroupForObject(widget));
}

CursorSurfaceMode CursorAuthority::defaultManagedModeForWindow(const QWindow* window,
                                                               const QString& explicitGroup) const
{
    if (!explicitGroup.isEmpty()) {
        return defaultManagedModeForGroup(explicitGroup);
    }
    return defaultManagedModeForGroup(defaultGroupForObject(window));
}

QString CursorAuthority::registerWidgetSurface(QWidget* widget, const QString& group,
                                               CursorSurfaceMode mode)
{
    if (!widget) {
        return QString();
    }

    const QString resolvedGroup = group.isEmpty() ? defaultGroupForObject(widget) : group;
    const QString surfaceId = surfaceIdForPointer(resolvedGroup, widget);

    SurfaceState& state = m_surfaceStates[surfaceId];
    state.surfaceId = surfaceId;
    state.group = resolvedGroup;
    state.mode = mode;
    state.widget = widget;
    m_widgetSurfaceIds[widget] = surfaceId;
    return surfaceId;
}

QString CursorAuthority::registerWindowSurface(QWindow* window, const QString& group,
                                               CursorSurfaceMode mode)
{
    if (!window) {
        return QString();
    }

    const QString resolvedGroup = group.isEmpty() ? defaultGroupForObject(window) : group;
    const QString surfaceId = surfaceIdForPointer(resolvedGroup, window);

    SurfaceState& state = m_surfaceStates[surfaceId];
    state.surfaceId = surfaceId;
    state.group = resolvedGroup;
    state.mode = mode;
    state.window = window;
    m_windowSurfaceIds[window] = surfaceId;
    return surfaceId;
}

void CursorAuthority::unregisterWidgetSurface(QWidget* widget)
{
    if (!widget) {
        return;
    }
    unregisterSurface(surfaceIdForWidget(widget));
}

void CursorAuthority::unregisterWindowSurface(QWindow* window)
{
    if (!window) {
        return;
    }
    unregisterSurface(surfaceIdForWindow(window));
}

void CursorAuthority::unregisterSurface(const QString& surfaceId)
{
    if (surfaceId.isEmpty()) {
        return;
    }

    if (const SurfaceState* state = stateForSurface(surfaceId)) {
        if (state->widget) {
            m_widgetSurfaceIds.remove(state->widget.data());
        }
        if (state->window) {
            m_windowSurfaceIds.remove(state->window.data());
        }
    }

    m_surfaceStates.remove(surfaceId);
}

void CursorAuthority::submitRequest(const QString& surfaceId, const QString& ownerId,
                                    CursorRequestSource source, const CursorStyleSpec& style,
                                    int priority)
{
    SurfaceState* state = stateForSurface(surfaceId);
    if (!state || ownerId.isEmpty()) {
        return;
    }

    RequestState request;
    request.ownerId = ownerId;
    request.source = source;
    request.style = style;
    request.priority = priority >= 0 ? priority : cursorPriorityForRequestSource(source);
    request.sequence = ++m_sequence;
    state->requests.insert(ownerId, request);
}

void CursorAuthority::clearRequest(const QString& surfaceId, const QString& ownerId)
{
    SurfaceState* state = stateForSurface(surfaceId);
    if (!state || ownerId.isEmpty()) {
        return;
    }

    state->requests.remove(ownerId);
}

void CursorAuthority::clearSurfaceRequests(const QString& surfaceId)
{
    SurfaceState* state = stateForSurface(surfaceId);
    if (!state) {
        return;
    }

    state->requests.clear();
}

void CursorAuthority::submitWidgetRequest(QWidget* widget, const QString& ownerId,
                                          CursorRequestSource source,
                                          const CursorStyleSpec& style, int priority)
{
    submitRequest(surfaceIdForWidget(widget), ownerId, source, style, priority);
}

void CursorAuthority::clearWidgetRequest(QWidget* widget, const QString& ownerId)
{
    clearRequest(surfaceIdForWidget(widget), ownerId);
}

void CursorAuthority::clearWidgetRequests(QWidget* widget)
{
    clearSurfaceRequests(surfaceIdForWidget(widget));
}

void CursorAuthority::submitWindowRequest(QWindow* window, const QString& ownerId,
                                          CursorRequestSource source,
                                          const CursorStyleSpec& style, int priority)
{
    submitRequest(surfaceIdForWindow(window), ownerId, source, style, priority);
}

void CursorAuthority::clearWindowRequest(QWindow* window, const QString& ownerId)
{
    clearRequest(surfaceIdForWindow(window), ownerId);
}

void CursorAuthority::clearWindowRequests(QWindow* window)
{
    clearSurfaceRequests(surfaceIdForWindow(window));
}

bool CursorAuthority::hasResolvedRequest(const QString& surfaceId) const
{
    const SurfaceState* state = stateForSurface(surfaceId);
    return state && winningRequest(*state);
}

bool CursorAuthority::hasResolvedRequestForWidget(QWidget* widget) const
{
    return hasResolvedRequest(surfaceIdForWidget(widget));
}

bool CursorAuthority::hasResolvedRequestForWindow(QWindow* window) const
{
    return hasResolvedRequest(surfaceIdForWindow(window));
}

CursorStyleSpec CursorAuthority::resolvedStyle(const QString& surfaceId) const
{
    const SurfaceState* state = stateForSurface(surfaceId);
    if (!state) {
        return CursorStyleSpec::fromShape(Qt::ArrowCursor);
    }

    if (const RequestState* request = winningRequest(*state)) {
        return request->style;
    }

    return state->hasLegacyApplied ? state->legacyApplied : CursorStyleSpec::fromShape(Qt::ArrowCursor);
}

CursorStyleSpec CursorAuthority::resolvedStyleForWidget(QWidget* widget) const
{
    return resolvedStyle(surfaceIdForWidget(widget));
}

CursorStyleSpec CursorAuthority::resolvedStyleForWindow(QWindow* window) const
{
    return resolvedStyle(surfaceIdForWindow(window));
}

QString CursorAuthority::resolvedOwner(const QString& surfaceId) const
{
    const SurfaceState* state = stateForSurface(surfaceId);
    if (!state) {
        return QString();
    }

    if (const RequestState* request = winningRequest(*state)) {
        return request->ownerId;
    }

    return QString();
}

CursorRequestSource CursorAuthority::resolvedSource(const QString& surfaceId) const
{
    const SurfaceState* state = stateForSurface(surfaceId);
    if (!state) {
        return CursorRequestSource::SurfaceDefault;
    }

    if (const RequestState* request = winningRequest(*state)) {
        return request->source;
    }

    return CursorRequestSource::SurfaceDefault;
}

CursorRequestSource CursorAuthority::resolvedSourceForWidget(QWidget* widget) const
{
    return resolvedSource(surfaceIdForWidget(widget));
}

CursorRequestSource CursorAuthority::resolvedSourceForWindow(QWindow* window) const
{
    return resolvedSource(surfaceIdForWindow(window));
}

CursorSurfaceMode CursorAuthority::surfaceMode(const QString& surfaceId) const
{
    const SurfaceState* state = stateForSurface(surfaceId);
    return state ? state->mode : CursorSurfaceMode::Legacy;
}

CursorSurfaceMode CursorAuthority::modeForWidget(QWidget* widget) const
{
    return surfaceMode(surfaceIdForWidget(widget));
}

CursorSurfaceMode CursorAuthority::modeForWindow(QWindow* window) const
{
    return surfaceMode(surfaceIdForWindow(window));
}

QString CursorAuthority::surfaceIdForWidget(QWidget* widget) const
{
    if (!widget) {
        return QString();
    }

    return m_widgetSurfaceIds.value(widget);
}

QString CursorAuthority::surfaceIdForWindow(QWindow* window) const
{
    if (!window) {
        return QString();
    }

    return m_windowSurfaceIds.value(window);
}

void CursorAuthority::recordLegacyApplied(const QString& surfaceId, const CursorStyleSpec& style)
{
    SurfaceState* state = stateForSurface(surfaceId);
    if (!state) {
        return;
    }

    state->legacyApplied = style;
    state->hasLegacyApplied = true;

    if (const RequestState* request = winningRequest(*state)) {
        state->lastLegacyMismatch = (request->style != style);
    } else {
        state->lastLegacyMismatch = false;
    }
}

void CursorAuthority::recordLegacyAppliedForWidget(QWidget* widget, const CursorStyleSpec& style)
{
    recordLegacyApplied(surfaceIdForWidget(widget), style);
}

void CursorAuthority::recordLegacyAppliedForWindow(QWindow* window, const CursorStyleSpec& style)
{
    recordLegacyApplied(surfaceIdForWindow(window), style);
}

bool CursorAuthority::lastLegacyComparisonMismatched(const QString& surfaceId) const
{
    const SurfaceState* state = stateForSurface(surfaceId);
    return state && state->lastLegacyMismatch;
}

QString CursorAuthority::normalizedGroupName(const QString& group)
{
    QString value = groupTail(group).toUpper();
    value.remove(QStringLiteral("_"));
    value.remove(QStringLiteral("-"));
    value.remove(QStringLiteral(" "));
    return value;
}

CursorSurfaceMode CursorAuthority::parseModeOverride(const QString& value,
                                                     CursorSurfaceMode fallback)
{
    const QString lowered = value.trimmed().toLower();
    if (lowered == QStringLiteral("legacy")) {
        return CursorSurfaceMode::Legacy;
    }
    if (lowered == QStringLiteral("shadow")) {
        return CursorSurfaceMode::Shadow;
    }
    if (lowered == QStringLiteral("authority")) {
        return CursorSurfaceMode::Authority;
    }
    return fallback;
}

CursorAuthority::SurfaceState* CursorAuthority::stateForSurface(const QString& surfaceId)
{
    if (surfaceId.isEmpty()) {
        return nullptr;
    }
    auto it = m_surfaceStates.find(surfaceId);
    return it == m_surfaceStates.end() ? nullptr : &it.value();
}

const CursorAuthority::SurfaceState* CursorAuthority::stateForSurface(const QString& surfaceId) const
{
    if (surfaceId.isEmpty()) {
        return nullptr;
    }
    auto it = m_surfaceStates.constFind(surfaceId);
    return it == m_surfaceStates.cend() ? nullptr : &it.value();
}

const CursorAuthority::RequestState* CursorAuthority::winningRequest(const SurfaceState& state) const
{
    if (state.requests.isEmpty()) {
        return nullptr;
    }

    const RequestState* winner = nullptr;
    for (auto it = state.requests.cbegin(); it != state.requests.cend(); ++it) {
        const RequestState& request = it.value();
        if (!winner ||
            request.priority > winner->priority ||
            (request.priority == winner->priority && request.sequence > winner->sequence)) {
            winner = &request;
        }
    }

    return winner;
}

QString CursorAuthority::surfaceIdForPointer(const QString& group, const void* ptr) const
{
    return QStringLiteral("%1:%2")
        .arg(groupTail(group))
        .arg(reinterpret_cast<quintptr>(ptr), 0, 16);
}
