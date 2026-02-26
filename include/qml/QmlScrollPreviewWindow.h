#pragma once

#include <QObject>
#include <QRect>
#include <QPointer>
#include <QWindow>  // for WId
#include <QImage>
#include <QQuickImageProvider>
#include <QMutex>

class QQuickView;
class QQuickItem;
class QScreen;

namespace SnapTray {

/**
 * @brief Image provider that feeds a single QImage to QML.
 *
 * Registered on the QQuickView engine as "scrollpreview".
 * The C++ bridge calls updateImage() and then sets the QML source to
 * "image://scrollpreview/current?t=<generation>" to force a reload.
 */
class ScrollPreviewImageProvider : public QQuickImageProvider
{
public:
    ScrollPreviewImageProvider();

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

    void updateImage(const QImage& image);

private:
    QMutex m_mutex;
    QImage m_image;
};

/**
 * @brief QML-based scroll capture preview window.
 *
 * Drop-in replacement for ScrollCapturePreviewWindow (QWidget-based).
 * Uses QQuickView to render a floating card with a live preview of
 * the stitched scroll capture image.
 *
 * The window is frameless, transparent, click-through, and stays on top.
 *
 * Usage:
 *   auto* preview = new QmlScrollPreviewWindow();
 *   preview->positionNear(captureRegion, screen);
 *   preview->show();
 *   preview->setPreviewImage(stitchedImage);
 */
class QmlScrollPreviewWindow : public QObject
{
    Q_OBJECT

public:
    explicit QmlScrollPreviewWindow(QObject* parent = nullptr);
    ~QmlScrollPreviewWindow() override;

    /**
     * @brief Update the preview image shown in the card.
     */
    void setPreviewImage(const QImage& preview);

    /**
     * @brief Position the window near the capture region.
     * Prefers the right side, then left side, picking the candidate
     * with the least overlap with the capture region.
     */
    void positionNear(const QRect& captureRegion, QScreen* screen);

    void show();
    void hide();
    void close();
    WId winId() const;

    QQuickView* quickView() const { return m_view; }

private:
    void ensureView();
    void applyPlatformWindowFlags();

    QQuickView* m_view = nullptr;
    QQuickItem* m_rootItem = nullptr;
    ScrollPreviewImageProvider* m_imageProvider = nullptr;  // owned by QML engine
    int m_imageGeneration = 0;
};

} // namespace SnapTray
