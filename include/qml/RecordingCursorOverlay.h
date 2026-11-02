#pragma once

#include <QPointer>
#include <QQuickPaintedItem>
#include <QRectF>
#include <QtQml/qqmlregistration.h>

class RecordingPreviewBackend;

// Draws the pointer effects (#160) over the preview video with the same
// renderer the exports use, at the preview position, aligned to the shown
// video (contentRect). Draws nothing when the effects are not active.
class RecordingCursorOverlay : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QObject* backend READ backend WRITE setBackend NOTIFY backendChanged)
    Q_PROPERTY(qint64 position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(QRectF contentRect READ contentRect WRITE setContentRect NOTIFY contentRectChanged)

public:
    explicit RecordingCursorOverlay(QQuickItem* parent = nullptr);

    // A RecordingPreviewBackend (exposed to QML as a context object).
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
