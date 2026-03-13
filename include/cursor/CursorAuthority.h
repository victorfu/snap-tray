#ifndef CURSORAUTHORITY_H
#define CURSORAUTHORITY_H

#include <QHash>
#include <QPointer>
#include <QString>

#include "cursor/CursorTypes.h"

class QObject;
class QWidget;
class QWindow;

class CursorAuthority
{
public:
    static CursorAuthority& instance();

    CursorAuthority(const CursorAuthority&) = delete;
    CursorAuthority& operator=(const CursorAuthority&) = delete;

    static CursorSurfaceMode defaultManagedModeForGroup(const QString& group);

    QString defaultGroupForObject(const QObject* object) const;
    CursorSurfaceMode defaultManagedModeForWidget(const QWidget* widget) const;
    CursorSurfaceMode defaultManagedModeForWindow(const QWindow* window,
                                                  const QString& explicitGroup = QString()) const;

    QString registerWidgetSurface(QWidget* widget, const QString& group = QString(),
                                  CursorSurfaceMode mode = CursorSurfaceMode::Shadow);
    QString registerWindowSurface(QWindow* window, const QString& group,
                                  CursorSurfaceMode mode = CursorSurfaceMode::Shadow);
    void unregisterWidgetSurface(QWidget* widget);
    void unregisterWindowSurface(QWindow* window);
    void unregisterSurface(const QString& surfaceId);

    void submitRequest(const QString& surfaceId, const QString& ownerId,
                       CursorRequestSource source, const CursorStyleSpec& style,
                       int priority = -1);
    void clearRequest(const QString& surfaceId, const QString& ownerId);
    void clearSurfaceRequests(const QString& surfaceId);

    void submitWidgetRequest(QWidget* widget, const QString& ownerId,
                             CursorRequestSource source, const CursorStyleSpec& style,
                             int priority = -1);
    void clearWidgetRequest(QWidget* widget, const QString& ownerId);
    void clearWidgetRequests(QWidget* widget);

    void submitWindowRequest(QWindow* window, const QString& ownerId,
                             CursorRequestSource source, const CursorStyleSpec& style,
                             int priority = -1);
    void clearWindowRequest(QWindow* window, const QString& ownerId);
    void clearWindowRequests(QWindow* window);

    bool hasResolvedRequest(const QString& surfaceId) const;
    bool hasResolvedRequestForWidget(QWidget* widget) const;
    bool hasResolvedRequestForWindow(QWindow* window) const;

    CursorStyleSpec resolvedStyle(const QString& surfaceId) const;
    CursorStyleSpec resolvedStyleForWidget(QWidget* widget) const;
    CursorStyleSpec resolvedStyleForWindow(QWindow* window) const;

    QString resolvedOwner(const QString& surfaceId) const;
    CursorRequestSource resolvedSource(const QString& surfaceId) const;
    CursorRequestSource resolvedSourceForWidget(QWidget* widget) const;
    CursorRequestSource resolvedSourceForWindow(QWindow* window) const;

    CursorSurfaceMode surfaceMode(const QString& surfaceId) const;
    CursorSurfaceMode modeForWidget(QWidget* widget) const;
    CursorSurfaceMode modeForWindow(QWindow* window) const;

    QString surfaceIdForWidget(QWidget* widget) const;
    QString surfaceIdForWindow(QWindow* window) const;

    void recordLegacyApplied(const QString& surfaceId, const CursorStyleSpec& style);
    void recordLegacyAppliedForWidget(QWidget* widget, const CursorStyleSpec& style);
    void recordLegacyAppliedForWindow(QWindow* window, const CursorStyleSpec& style);
    bool lastLegacyComparisonMismatched(const QString& surfaceId) const;

private:
    CursorAuthority() = default;

    struct RequestState {
        QString ownerId;
        CursorRequestSource source = CursorRequestSource::SurfaceDefault;
        CursorStyleSpec style;
        int priority = 0;
        quint64 sequence = 0;
    };

    struct SurfaceState {
        QString surfaceId;
        QString group;
        CursorSurfaceMode mode = CursorSurfaceMode::Shadow;
        QPointer<QWidget> widget;
        QPointer<QWindow> window;
        QHash<QString, RequestState> requests;
        CursorStyleSpec legacyApplied;
        bool hasLegacyApplied = false;
        bool lastLegacyMismatch = false;
    };

    static QString normalizedGroupName(const QString& group);
    static CursorSurfaceMode parseModeOverride(const QString& value,
                                               CursorSurfaceMode fallback);

    SurfaceState* stateForSurface(const QString& surfaceId);
    const SurfaceState* stateForSurface(const QString& surfaceId) const;
    const RequestState* winningRequest(const SurfaceState& state) const;

    QString surfaceIdForPointer(const QString& group, const void* ptr) const;

    QHash<QString, SurfaceState> m_surfaceStates;
    QHash<const QWidget*, QString> m_widgetSurfaceIds;
    QHash<const QWindow*, QString> m_windowSurfaceIds;
    quint64 m_sequence = 0;
};

#endif // CURSORAUTHORITY_H
