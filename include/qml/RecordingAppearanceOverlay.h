#pragma once

#include "recording/RecordingAppearance.h"

#include <QPointer>
#include <QQuickPaintedItem>
#include <QRectF>
#include <QtQml/qqmlregistration.h>

#include <memory>

class RecordingPreviewBackend;

// Draws the frame around the export (#184) in the preview: background,
// shadow and border around the picture, which fills contentRect (view
// coordinates); the picture's rounded shape is left clear so the video
// shows through. The same RecordingAppearance geometry the exports use.
class RecordingAppearanceOverlay : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QObject* backend READ backend WRITE setBackend NOTIFY backendChanged)
    Q_PROPERTY(QRectF contentRect READ contentRect WRITE setContentRect NOTIFY contentRectChanged)

public:
    explicit RecordingAppearanceOverlay(QQuickItem* parent = nullptr);

    QObject* backend() const;
    void setBackend(QObject* backend);
    QRectF contentRect() const { return m_contentRect; }
    void setContentRect(const QRectF& rect);

    void paint(QPainter* painter) override;

signals:
    void backendChanged();
    void contentRectChanged();

private:
    void invalidate();

    QPointer<RecordingPreviewBackend> m_backend;
    QRectF m_contentRect;
    // Rebuilt when the look or the picture size changes.
    std::unique_ptr<SnapTray::RecordingAppearanceRenderer> m_renderer;
    SnapTray::RecordingAppearance m_rendererAppearance;
    QSize m_rendererSize;
    QImage m_backgroundImage;
};
