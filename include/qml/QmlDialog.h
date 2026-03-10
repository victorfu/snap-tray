#pragma once

#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QUrl>

class QQuickView;

namespace SnapTray {

class QmlDialog : public QObject
{
    Q_OBJECT

public:
    QmlDialog(const QUrl& qmlSource, QObject* viewModel,
              const QString& contextPropertyName, QObject* parent = nullptr);
    ~QmlDialog() override;

    void showAt(const QPoint& pos = QPoint());
    void close();
    void setModal(bool modal);

signals:
    void closed();

private:
    void ensureView();

    QUrl m_qmlSource;
    QPointer<QObject> m_viewModel;
    QString m_contextPropertyName;
    QQuickView* m_view = nullptr;
    bool m_modal = false;
};

} // namespace SnapTray
