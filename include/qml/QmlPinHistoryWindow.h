#pragma once

#include <QObject>
#include <QPointer>

class QEvent;
class QQuickView;

class PinWindowManager;

namespace SnapTray {

class PinHistoryBackend;
class PinHistoryModel;

class QmlPinHistoryWindow : public QObject
{
    Q_OBJECT

public:
    explicit QmlPinHistoryWindow(PinWindowManager* pinWindowManager, QObject* parent = nullptr);
    ~QmlPinHistoryWindow() override;

    void show();
    void showNormal();
    void raise();
    void activateWindow();
    bool isVisible() const;
    bool isMinimized() const;

private:
    void ensureView();
    bool eventFilter(QObject* watched, QEvent* event) override;

    QPointer<QQuickView> m_view;
    PinHistoryModel* m_model = nullptr;
    PinHistoryBackend* m_backend = nullptr;
    PinWindowManager* m_pinWindowManager = nullptr;
    bool m_hasShownOnce = false;
};

} // namespace SnapTray
