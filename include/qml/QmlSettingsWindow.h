#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

class QEvent;
class QQuickView;

namespace SnapTray {
class SettingsBackend;

class QmlSettingsWindow : public QObject {
    Q_OBJECT
public:
    explicit QmlSettingsWindow(QObject* parent = nullptr);
    ~QmlSettingsWindow();

    void show();
    void raise();
    void activateWindow();
    bool isVisible() const;

signals:
    void ocrLanguagesChanged(const QStringList& languages);
    void mcpEnabledChanged(bool enabled);

private:
    void ensureView();
    void syncCursorSurface();
    bool eventFilter(QObject* watched, QEvent* event) override;

    QPointer<QQuickView> m_view;
    SettingsBackend* m_backend = nullptr;
    QString m_cursorSurfaceId;
    QString m_cursorOwnerId;
};
} // namespace SnapTray
