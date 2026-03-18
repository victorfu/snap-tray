#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <functional>

class PinWindowManager;
class QEvent;
class QQuickView;

namespace SnapTray {

class HistoryBackend;
class HistoryModel;

class QmlHistoryWindow : public QObject
{
    Q_OBJECT

public:
    explicit QmlHistoryWindow(PinWindowManager* pinWindowManager,
                              std::function<bool(const QString&)> startReplay,
                              QObject* parent = nullptr);
    ~QmlHistoryWindow() override;

    void show();
    void showNormal();
    void raise();
    void activateWindow();
    bool isVisible() const;
    bool isMinimized() const;

private:
    void ensureView();
    void syncCursorSurface();
    bool eventFilter(QObject* watched, QEvent* event) override;

    QPointer<QQuickView> m_view;
    HistoryModel* m_model = nullptr;
    HistoryBackend* m_backend = nullptr;
    PinWindowManager* m_pinWindowManager = nullptr;
    std::function<bool(const QString&)> m_startReplay;
    bool m_hasShownOnce = false;
    QString m_cursorSurfaceId;
    QString m_cursorOwnerId;
};

} // namespace SnapTray
