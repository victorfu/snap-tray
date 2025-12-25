#ifndef PINWINDOW_H
#define PINWINDOW_H

#include <QWidget>
#include <QPixmap>
#include <QPoint>
#include <QElapsedTimer>
#include "WatermarkRenderer.h"
#include "LoadingSpinnerRenderer.h"

class QMenu;
class QLabel;
class QTimer;
class OCRManager;
class PinWindowManager;

class PinWindow : public QWidget
{
    Q_OBJECT

public:
    explicit PinWindow(const QPixmap &screenshot, const QPoint &position, QWidget *parent = nullptr);
    ~PinWindow();

    void setZoomLevel(qreal zoom);
    qreal zoomLevel() const { return m_zoomLevel; }

    void setOpacity(qreal opacity);
    qreal opacity() const { return m_opacity; }

    void rotateRight();  // Rotate clockwise by 90 degrees
    void rotateLeft();   // Rotate counter-clockwise by 90 degrees
    void flipHorizontal();  // Flip horizontally (mirror left-right)
    void flipVertical();    // Flip vertically (mirror top-bottom)

    // Watermark settings
    void setWatermarkSettings(const WatermarkRenderer::Settings &settings);
    WatermarkRenderer::Settings watermarkSettings() const { return m_watermarkSettings; }

    // Pin window manager
    void setPinWindowManager(PinWindowManager *manager);

signals:
    void closed(PinWindow *window);
    void saveRequested(const QPixmap &pixmap);
    void ocrCompleted(bool success, const QString &message);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    // Layout constants
    static constexpr int kShadowMargin = 8;
    static constexpr int kResizeMargin = 6;
    static constexpr int kMinSize = 50;

    // Resize edge enum
    enum class ResizeEdge {
        None,
        Left, Right, Top, Bottom,
        TopLeft, TopRight, BottomLeft, BottomRight
    };

    void updateSize();
    void createContextMenu();
    void saveToFile();
    void copyToClipboard();
    QPixmap getTransformedPixmap() const;

    // Performance optimization: ensure transform cache is valid
    void ensureTransformCacheValid();
    void onResizeFinished();

    // Resize methods
    ResizeEdge getResizeEdge(const QPoint &pos) const;
    void updateCursorForEdge(ResizeEdge edge);

    // Zoom indicator
    void showZoomIndicator();

    // Opacity indicator
    void showOpacityIndicator();

    // OCR methods
    void performOCR();
    void onOCRComplete(bool success, const QString &text, const QString &error);

    // Info methods
    void copyAllInfo();

    // Original members
    QPixmap m_originalPixmap;
    QPixmap m_displayPixmap;
    qreal m_zoomLevel;
    QPoint m_dragStartPos;
    bool m_isDragging;
    QMenu *m_contextMenu;

    // Zoom menu members
    QAction *m_currentZoomAction;
    bool m_smoothing;

    // Resize members
    ResizeEdge m_resizeEdge;
    bool m_isResizing;
    QPoint m_resizeStartPos;
    QSize m_resizeStartSize;
    QPoint m_resizeStartWindowPos;

    // Zoom indicator members
    QLabel *m_zoomLabel;
    QTimer *m_zoomLabelTimer;

    // Opacity members
    qreal m_opacity;
    QLabel *m_opacityLabel;
    QTimer *m_opacityLabelTimer;

    // Rotation members
    int m_rotationAngle;  // 0, 90, 180, 270 degrees

    // Flip members
    bool m_flipHorizontal;
    bool m_flipVertical;

    // OCR members
    OCRManager *m_ocrManager;
    bool m_ocrInProgress;
    LoadingSpinnerRenderer *m_loadingSpinner;
    QLabel *m_ocrToastLabel;
    QTimer *m_ocrToastTimer;

    // Watermark members
    WatermarkRenderer::Settings m_watermarkSettings;

    // Pin window manager
    PinWindowManager *m_pinWindowManager = nullptr;

    // Performance optimization: transform cache
    mutable QPixmap m_transformedCache;
    mutable int m_cachedRotation = -1;
    mutable bool m_cachedFlipH = false;
    mutable bool m_cachedFlipV = false;

    // Resize optimization
    QElapsedTimer m_resizeThrottleTimer;
    QTimer *m_resizeFinishTimer = nullptr;
    bool m_pendingHighQualityUpdate = false;
    static constexpr int kResizeThrottleMs = 16;  // ~60fps during resize

    // Shadow cache
    mutable QPixmap m_shadowCache;
    mutable QSize m_shadowCacheSize;
    void ensureShadowCache(const QSize &contentSize);
};

#endif // PINWINDOW_H
