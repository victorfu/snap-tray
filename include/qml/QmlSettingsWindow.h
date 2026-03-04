#pragma once

#include <QObject>
#include <QPointer>

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

    QPointer<QQuickView> m_view;
    SettingsBackend* m_backend = nullptr;
};
} // namespace SnapTray
