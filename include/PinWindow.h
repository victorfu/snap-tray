#ifndef PINWINDOW_H
#define PINWINDOW_H

#include <QWidget>
#include <QPixmap>
#include <QPoint>

class QMenu;
class QLabel;
class QTimer;

#ifdef Q_OS_MACOS
class OCRManager;
#endif

class PinWindow : public QWidget
{
    Q_OBJECT

public:
    explicit PinWindow(const QPixmap &screenshot, const QPoint &position, QWidget *parent = nullptr);
    ~PinWindow();

    void setZoomLevel(qreal zoom);
    qreal zoomLevel() const { return m_zoomLevel; }

    void rotateRight();  // Rotate clockwise by 90 degrees
    void rotateLeft();   // Rotate counter-clockwise by 90 degrees

signals:
    void closed(PinWindow *window);
    void saveRequested(const QPixmap &pixmap);

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

    // Resize methods
    ResizeEdge getResizeEdge(const QPoint &pos) const;
    void updateCursorForEdge(ResizeEdge edge);

    // Zoom indicator
    void showZoomIndicator();

#ifdef Q_OS_MACOS
    // OCR methods (macOS only)
    void performOCR();
    void onOCRComplete(bool success, const QString &text, const QString &error);
#endif

    // Original members
    QPixmap m_originalPixmap;
    QPixmap m_displayPixmap;
    qreal m_zoomLevel;
    QPoint m_dragStartPos;
    bool m_isDragging;
    QMenu *m_contextMenu;

    // Resize members
    ResizeEdge m_resizeEdge;
    bool m_isResizing;
    QPoint m_resizeStartPos;
    QSize m_resizeStartSize;
    QPoint m_resizeStartWindowPos;

    // Zoom indicator members
    QLabel *m_zoomLabel;
    QTimer *m_zoomLabelTimer;

    // Rotation members
    int m_rotationAngle;  // 0, 90, 180, 270 degrees

#ifdef Q_OS_MACOS
    // OCR members (macOS only)
    OCRManager *m_ocrManager;
    bool m_ocrInProgress;
#endif
};

#endif // PINWINDOW_H
