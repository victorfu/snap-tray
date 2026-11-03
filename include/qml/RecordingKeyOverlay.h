#pragma once

#include <QPointer>
#include <QQuickPaintedItem>
#include <QRectF>
#include <QtQml/qqmlregistration.h>

class RecordingPreviewBackend;

// Draws the shortcut hints (#187) over the preview at the playhead, on the
// exported picture (contentRect, view coordinates), with the renderer the
// exports use. Draws nothing when the hints are off or there are no keys.
class RecordingKeyOverlay : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QObject* backend READ backend WRITE setBackend NOTIFY backendChanged)
    Q_PROPERTY(qint64 position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(QRectF contentRect READ contentRect WRITE setContentRect NOTIFY contentRectChanged)

public:
    explicit RecordingKeyOverlay(QQuickItem* parent = nullptr);

    QObject* backend() const;
    void setBackend(QObject* backend);
    qint64 position() const { return m_position; }
    void setPosition(qint64 position);
    QRectF contentRect() const { return m_contentRect; }
    void setContentRect(const QRectF& rect);

    void paint(QPainter* painter) override;

signals:
    void backendChanged();
    void positionChanged();
    void contentRectChanged();

private:
    QPointer<RecordingPreviewBackend> m_backend;
    qint64 m_position = 0;
    QRectF m_contentRect;
};
