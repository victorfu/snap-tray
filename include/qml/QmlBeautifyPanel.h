#pragma once

#include <QObject>
#include <QPointer>
#include <QPoint>
#include <QRect>
#include <QString>

#include "beautify/BeautifySettings.h"

class QEvent;
class QPixmap;
class QQuickItem;
class QQuickView;
class QWindow;

namespace SnapTray {

class BeautifyPanelBackend;

class QmlBeautifyPanel : public QObject
{
    Q_OBJECT

public:
    explicit QmlBeautifyPanel(QObject* parent = nullptr);
    ~QmlBeautifyPanel() override;

    void setSourcePixmap(const QPixmap& pixmap);
    void setSettings(const BeautifySettings& settings);
    void showNear(const QRect& anchorRect);
    void hide();
    void close();
    bool isVisible() const;
    QRect geometry() const;
    QWindow* window() const;

signals:
    void copyRequested(const BeautifySettings& settings);
    void saveRequested(const BeautifySettings& settings);
    void closeRequested();

private slots:
    void onDragStarted();
    void onDragMoved(double deltaX, double deltaY);
    void onDragFinished();

private:
    void ensureView();
    void positionNear(const QRect& anchorRect);
    void syncCursorSurface();
    bool eventFilter(QObject* watched, QEvent* event) override;

    QQuickView* m_view = nullptr;
    QQuickItem* m_rootItem = nullptr;
    BeautifyPanelBackend* m_backend = nullptr;
    QPoint m_dragStartWindowPos;
    QPoint m_dragStartCursorPos;
    bool m_isDragging = false;
    QString m_cursorSurfaceId;
    QString m_cursorOwnerId;
};

} // namespace SnapTray
