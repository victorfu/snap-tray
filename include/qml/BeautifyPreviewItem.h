#pragma once

#include <QPointer>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

namespace SnapTray {
class BeautifyPanelBackend;
}

class BeautifyPreviewItem : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QObject* backend READ backend WRITE setBackend NOTIFY backendChanged)

public:
    explicit BeautifyPreviewItem(QQuickItem* parent = nullptr);

    QObject* backend() const;
    void setBackend(QObject* backendObject);

    void paint(QPainter* painter) override;

signals:
    void backendChanged();

private:
    void bindBackend(SnapTray::BeautifyPanelBackend* backend);

    QPointer<SnapTray::BeautifyPanelBackend> m_backend;
    QMetaObject::Connection m_previewConnection;
    QMetaObject::Connection m_settingsConnection;
};
