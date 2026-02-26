#ifndef COMPOSERICHTEXTEDIT_H
#define COMPOSERICHTEXTEDIT_H

#include <QPoint>
#include <QRect>
#include <QSize>
#include <QTextEdit>

class QImage;
class QMimeData;
class QMouseEvent;
class QPaintEvent;
class QPixmap;

class ComposeRichTextEdit : public QTextEdit
{
    Q_OBJECT

public:
    explicit ComposeRichTextEdit(QWidget* parent = nullptr);

    void setImageMaxWidth(int maxWidth);
    int imageMaxWidth() const { return m_imageMaxWidth; }

    bool insertImageFromPixmap(const QPixmap& pixmap, bool insertAtStart = false);
    bool insertImageFromFile(const QString& filePath);
    bool insertImageFromMimeData(const QMimeData* source);

    bool selectImageAtCursor();
    bool resizeSelectedImageToWidth(int width);
    QSize selectedImageSize() const;
    QRect selectedImageRect() const { return m_selectedImageRect; }

    QString toMarkdownContent() const;
    bool hasEmbeddedImage() const;

protected:
    void insertFromMimeData(const QMimeData* source) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    enum class ResizeHandle {
        None,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight
    };

    static constexpr int kMinImageWidth = 32;
    static constexpr int kHandleSize = 12;

    bool insertImageFromImage(const QImage& image, bool insertAtStart);
    QImage scaleImageToMaxWidth(const QImage& image) const;
    bool isImagePosition(int position) const;
    bool findImageAt(const QPoint& pos, int* outPosition) const;
    QRect imageRectForPosition(int position) const;
    void updateSelectedImageRect();
    QVector<QRect> handleRectsForImage(const QRect& imageRect) const;
    ResizeHandle hitTestHandle(const QPoint& pos) const;
    bool applyImageSize(int position, int width, int height);
    static QByteArray imageToPngBytes(const QImage& image);
    static QString imageToDataUri(const QImage& image);
    void clearImageSelection();
    Qt::CursorShape cursorShapeForHandle(ResizeHandle handle) const;

    int m_imageMaxWidth = 600;
    int m_selectedImagePosition = -1;
    QRect m_selectedImageRect;

    bool m_isResizing = false;
    ResizeHandle m_activeHandle = ResizeHandle::None;
    QPoint m_resizeStartPos;
    QSize m_resizeStartSize;
};

#endif // COMPOSERICHTEXTEDIT_H
