#pragma once

#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QString>
#include <QUrl>

class QEvent;
class QQuickView;

namespace SnapTray {

class QmlDialog : public QObject
{
    Q_OBJECT

public:
    QmlDialog(const QUrl& qmlSource, QObject* viewModel,
              const QString& contextPropertyName, QObject* parent = nullptr);
    ~QmlDialog() override;

    virtual void showAt(const QPoint& pos = QPoint());
    virtual void close();
    void setModal(bool modal);

signals:
    void closed();

private:
    void ensureView();
    void syncCursorSurface();
    bool eventFilter(QObject* watched, QEvent* event) override;

    QUrl m_qmlSource;
    QPointer<QObject> m_viewModel;
    QString m_contextPropertyName;
    QQuickView* m_view = nullptr;
    bool m_modal = false;
    QString m_cursorSurfaceId;
    QString m_cursorOwnerId;
};

} // namespace SnapTray
