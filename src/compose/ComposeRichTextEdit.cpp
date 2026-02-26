#include "compose/ComposeRichTextEdit.h"

#include <QBuffer>
#include <QFileInfo>
#include <QImage>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QTextList>
#include <QUrl>
#include <QScrollBar>
#include <QtMath>

namespace {
bool hasImageExtension(const QString& filePath)
{
    const QFileInfo info(filePath);
    const QString suffix = info.suffix().toLower();
    static const QStringList kImageSuffixes = {
        QStringLiteral("png"),
        QStringLiteral("jpg"),
        QStringLiteral("jpeg"),
        QStringLiteral("bmp"),
        QStringLiteral("gif"),
        QStringLiteral("webp"),
        QStringLiteral("tiff"),
        QStringLiteral("tif")
    };
    return kImageSuffixes.contains(suffix);
}
} // namespace

ComposeRichTextEdit::ComposeRichTextEdit(QWidget* parent)
    : QTextEdit(parent)
{
    setAcceptRichText(true);
    setMouseTracking(true);

    auto refreshSelectionRect = [this]() {
        if (m_selectedImagePosition >= 0) {
            updateSelectedImageRect();
            viewport()->update();
        }
    };
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, refreshSelectionRect);
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, refreshSelectionRect);
}

void ComposeRichTextEdit::setImageMaxWidth(int maxWidth)
{
    m_imageMaxWidth = qBound(kMinImageWidth, maxWidth, 4096);
}

bool ComposeRichTextEdit::insertImageFromPixmap(const QPixmap& pixmap, bool insertAtStart)
{
    if (pixmap.isNull()) {
        return false;
    }
    return insertImageFromImage(pixmap.toImage(), insertAtStart);
}

bool ComposeRichTextEdit::insertImageFromFile(const QString& filePath)
{
    if (filePath.trimmed().isEmpty()) {
        return false;
    }

    const QImage loadedImage(filePath);
    if (loadedImage.isNull()) {
        return false;
    }

    return insertImageFromImage(loadedImage, false);
}

bool ComposeRichTextEdit::insertImageFromMimeData(const QMimeData* source)
{
    if (!source) {
        return false;
    }

    if (source->hasImage()) {
        const QVariant imageVariant = source->imageData();
        if (imageVariant.canConvert<QImage>()) {
            return insertImageFromImage(imageVariant.value<QImage>(), false);
        }
    }

    if (source->hasUrls()) {
        const QList<QUrl> urls = source->urls();
        for (const QUrl& url : urls) {
            if (!url.isLocalFile()) {
                continue;
            }
            const QString localFile = url.toLocalFile();
            if (!hasImageExtension(localFile)) {
                continue;
            }
            if (insertImageFromFile(localFile)) {
                return true;
            }
        }
    }

    return false;
}

bool ComposeRichTextEdit::selectImageAtCursor()
{
    QTextCursor cursor = textCursor();
    int position = cursor.position();
    if (!isImagePosition(position) && position > 0 && isImagePosition(position - 1)) {
        position -= 1;
    }

    if (!isImagePosition(position)) {
        clearImageSelection();
        return false;
    }

    m_selectedImagePosition = position;
    QTextCursor imageCursor(document());
    imageCursor.setPosition(position);
    setTextCursor(imageCursor);
    updateSelectedImageRect();
    viewport()->update();
    return true;
}

bool ComposeRichTextEdit::resizeSelectedImageToWidth(int width)
{
    if (m_selectedImagePosition < 0 || !isImagePosition(m_selectedImagePosition)) {
        return false;
    }

    QTextCursor cursor(document());
    cursor.setPosition(m_selectedImagePosition);
    const QTextImageFormat imageFormat = cursor.charFormat().toImageFormat();
    int currentWidth = qRound(imageFormat.width());
    int currentHeight = qRound(imageFormat.height());
    if (currentWidth <= 0 || currentHeight <= 0) {
        return false;
    }

    const int clampedWidth = qBound(kMinImageWidth, width, 4096);
    const qreal ratio = static_cast<qreal>(currentHeight) / static_cast<qreal>(currentWidth);
    const int newHeight = qMax(1, qRound(static_cast<qreal>(clampedWidth) * ratio));
    return applyImageSize(m_selectedImagePosition, clampedWidth, newHeight);
}

QSize ComposeRichTextEdit::selectedImageSize() const
{
    if (m_selectedImagePosition < 0 || !isImagePosition(m_selectedImagePosition)) {
        return {};
    }

    QTextCursor cursor(document());
    cursor.setPosition(m_selectedImagePosition);
    const QTextImageFormat imageFormat = cursor.charFormat().toImageFormat();
    return QSize(qRound(imageFormat.width()), qRound(imageFormat.height()));
}

QString ComposeRichTextEdit::toMarkdownContent() const
{
    return document()->toMarkdown(QTextDocument::MarkdownDialectGitHub);
}

bool ComposeRichTextEdit::hasEmbeddedImage() const
{
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid()) {
                continue;
            }
            if (fragment.charFormat().isImageFormat()) {
                return true;
            }
        }
    }
    return false;
}

void ComposeRichTextEdit::insertFromMimeData(const QMimeData* source)
{
    if (insertImageFromMimeData(source)) {
        return;
    }
    QTextEdit::insertFromMimeData(source);
}

void ComposeRichTextEdit::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        const ResizeHandle handle = hitTestHandle(event->position().toPoint());
        if (m_selectedImagePosition >= 0 && handle != ResizeHandle::None) {
            m_isResizing = true;
            m_activeHandle = handle;
            m_resizeStartPos = event->position().toPoint();
            m_resizeStartSize = selectedImageSize();
            event->accept();
            return;
        }

        int imagePosition = -1;
        if (findImageAt(event->position().toPoint(), &imagePosition)) {
            m_selectedImagePosition = imagePosition;
            QTextCursor cursor(document());
            cursor.setPosition(imagePosition);
            setTextCursor(cursor);
            updateSelectedImageRect();
            viewport()->update();
            event->accept();
            return;
        }

        clearImageSelection();
    }

    QTextEdit::mousePressEvent(event);
}

void ComposeRichTextEdit::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isResizing && m_selectedImagePosition >= 0 && !m_resizeStartSize.isEmpty()) {
        const QPoint delta = event->position().toPoint() - m_resizeStartPos;

        qreal scaleX = 1.0;
        qreal scaleY = 1.0;

        switch (m_activeHandle) {
        case ResizeHandle::TopLeft:
            scaleX = static_cast<qreal>(m_resizeStartSize.width() - delta.x()) / m_resizeStartSize.width();
            scaleY = static_cast<qreal>(m_resizeStartSize.height() - delta.y()) / m_resizeStartSize.height();
            break;
        case ResizeHandle::TopRight:
            scaleX = static_cast<qreal>(m_resizeStartSize.width() + delta.x()) / m_resizeStartSize.width();
            scaleY = static_cast<qreal>(m_resizeStartSize.height() - delta.y()) / m_resizeStartSize.height();
            break;
        case ResizeHandle::BottomLeft:
            scaleX = static_cast<qreal>(m_resizeStartSize.width() - delta.x()) / m_resizeStartSize.width();
            scaleY = static_cast<qreal>(m_resizeStartSize.height() + delta.y()) / m_resizeStartSize.height();
            break;
        case ResizeHandle::BottomRight:
            scaleX = static_cast<qreal>(m_resizeStartSize.width() + delta.x()) / m_resizeStartSize.width();
            scaleY = static_cast<qreal>(m_resizeStartSize.height() + delta.y()) / m_resizeStartSize.height();
            break;
        case ResizeHandle::None:
            break;
        }

        qreal scale = qMax(0.1, qMax(scaleX, scaleY));
        if (!qIsFinite(scale)) {
            scale = 1.0;
        }

        const int newWidth = qBound(kMinImageWidth, qRound(m_resizeStartSize.width() * scale), 4096);
        const qreal ratio = static_cast<qreal>(m_resizeStartSize.height()) / m_resizeStartSize.width();
        const int newHeight = qMax(1, qRound(static_cast<qreal>(newWidth) * ratio));
        if (applyImageSize(m_selectedImagePosition, newWidth, newHeight)) {
            event->accept();
            return;
        }
    }

    if (m_selectedImagePosition >= 0) {
        const ResizeHandle handle = hitTestHandle(event->position().toPoint());
        if (handle != ResizeHandle::None) {
            viewport()->setCursor(cursorShapeForHandle(handle));
        } else {
            viewport()->unsetCursor();
        }
    } else {
        viewport()->unsetCursor();
    }

    QTextEdit::mouseMoveEvent(event);
}

void ComposeRichTextEdit::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_isResizing) {
        m_isResizing = false;
        m_activeHandle = ResizeHandle::None;
        viewport()->unsetCursor();
        event->accept();
        return;
    }

    QTextEdit::mouseReleaseEvent(event);
}

void ComposeRichTextEdit::paintEvent(QPaintEvent* event)
{
    QTextEdit::paintEvent(event);

    if (m_selectedImagePosition >= 0) {
        updateSelectedImageRect();
    }
    if (m_selectedImagePosition < 0 || m_selectedImageRect.isEmpty()) {
        return;
    }

    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(palette().highlight().color(), 1, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(m_selectedImageRect.adjusted(0, 0, -1, -1));

    painter.setPen(Qt::NoPen);
    painter.setBrush(palette().highlight().color());
    const QVector<QRect> handleRects = handleRectsForImage(m_selectedImageRect);
    for (const QRect& rect : handleRects) {
        painter.drawRect(rect);
    }
}

bool ComposeRichTextEdit::insertImageFromImage(const QImage& image, bool insertAtStart)
{
    if (image.isNull()) {
        return false;
    }

    const QImage normalized = scaleImageToMaxWidth(image);
    if (normalized.isNull()) {
        return false;
    }

    QTextCursor cursor = textCursor();
    if (insertAtStart) {
        cursor = QTextCursor(document());
        cursor.movePosition(QTextCursor::Start);
        setTextCursor(cursor);
    }

    const QString dataUri = imageToDataUri(normalized);
    QTextImageFormat imageFormat;
    imageFormat.setName(dataUri);
    imageFormat.setWidth(normalized.width());
    imageFormat.setHeight(normalized.height());

    cursor.insertImage(imageFormat);
    cursor.insertBlock();
    setTextCursor(cursor);
    return true;
}

QImage ComposeRichTextEdit::scaleImageToMaxWidth(const QImage& image) const
{
    if (image.isNull()) {
        return {};
    }
    if (image.width() <= m_imageMaxWidth) {
        return image;
    }
    return image.scaledToWidth(m_imageMaxWidth, Qt::SmoothTransformation);
}

bool ComposeRichTextEdit::isImagePosition(int position) const
{
    if (position < 0 || position >= document()->characterCount()) {
        return false;
    }

    QTextCursor cursor(document());
    cursor.setPosition(position);
    return cursor.charFormat().isImageFormat();
}

bool ComposeRichTextEdit::findImageAt(const QPoint& pos, int* outPosition) const
{
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || !fragment.charFormat().isImageFormat()) {
                continue;
            }

            const int fragmentPos = fragment.position();
            const QRect fragmentRect = imageRectForPosition(fragmentPos);
            if (fragmentRect.contains(pos)) {
                if (outPosition) {
                    *outPosition = fragmentPos;
                }
                return true;
            }
        }
    }

    return false;
}

QRect ComposeRichTextEdit::imageRectForPosition(int position) const
{
    if (!isImagePosition(position)) {
        return {};
    }

    QTextCursor cursor(document());
    cursor.setPosition(position);
    const QTextImageFormat imageFormat = cursor.charFormat().toImageFormat();
    const QRect caretRect = cursorRect(cursor);

    int width = qRound(imageFormat.width());
    int height = qRound(imageFormat.height());
    if (width <= 0) {
        width = qMax(kMinImageWidth, caretRect.width());
    }
    if (height <= 0) {
        height = qMax(kHandleSize * 2, caretRect.height());
    }

    width = qMax(width, kMinImageWidth);
    height = qMax(height, kHandleSize * 2);
    return QRect(caretRect.topLeft(), QSize(width, height));
}

void ComposeRichTextEdit::updateSelectedImageRect()
{
    if (m_selectedImagePosition < 0) {
        m_selectedImageRect = {};
        return;
    }
    m_selectedImageRect = imageRectForPosition(m_selectedImagePosition);
}

QVector<QRect> ComposeRichTextEdit::handleRectsForImage(const QRect& imageRect) const
{
    if (imageRect.isEmpty()) {
        return {};
    }

    const int half = kHandleSize / 2;
    return {
        QRect(imageRect.topLeft() - QPoint(half, half), QSize(kHandleSize, kHandleSize)),
        QRect(imageRect.topRight() - QPoint(half, half), QSize(kHandleSize, kHandleSize)),
        QRect(imageRect.bottomLeft() - QPoint(half, half), QSize(kHandleSize, kHandleSize)),
        QRect(imageRect.bottomRight() - QPoint(half, half), QSize(kHandleSize, kHandleSize))
    };
}

ComposeRichTextEdit::ResizeHandle ComposeRichTextEdit::hitTestHandle(const QPoint& pos) const
{
    if (m_selectedImagePosition < 0 || m_selectedImageRect.isEmpty()) {
        return ResizeHandle::None;
    }

    const QVector<QRect> handleRects = handleRectsForImage(m_selectedImageRect);
    if (handleRects.size() != 4) {
        return ResizeHandle::None;
    }

    if (handleRects[0].contains(pos)) {
        return ResizeHandle::TopLeft;
    }
    if (handleRects[1].contains(pos)) {
        return ResizeHandle::TopRight;
    }
    if (handleRects[2].contains(pos)) {
        return ResizeHandle::BottomLeft;
    }
    if (handleRects[3].contains(pos)) {
        return ResizeHandle::BottomRight;
    }
    return ResizeHandle::None;
}

bool ComposeRichTextEdit::applyImageSize(int position, int width, int height)
{
    if (!isImagePosition(position)) {
        return false;
    }

    QTextCursor cursor(document());
    cursor.setPosition(position);
    QTextImageFormat imageFormat = cursor.charFormat().toImageFormat();
    imageFormat.setWidth(width);
    imageFormat.setHeight(height);
    cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 1);
    cursor.setCharFormat(imageFormat);

    m_selectedImagePosition = position;
    updateSelectedImageRect();
    viewport()->update();
    return true;
}

QByteArray ComposeRichTextEdit::imageToPngBytes(const QImage& image)
{
    QByteArray pngBytes;
    if (image.isNull()) {
        return pngBytes;
    }

    QBuffer buffer(&pngBytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return pngBytes;
}

QString ComposeRichTextEdit::imageToDataUri(const QImage& image)
{
    const QByteArray pngBytes = imageToPngBytes(image);
    if (pngBytes.isEmpty()) {
        return {};
    }
    return QStringLiteral("data:image/png;base64,%1")
        .arg(QString::fromLatin1(pngBytes.toBase64()));
}

void ComposeRichTextEdit::clearImageSelection()
{
    m_isResizing = false;
    m_activeHandle = ResizeHandle::None;
    m_selectedImagePosition = -1;
    m_selectedImageRect = {};
    viewport()->unsetCursor();
    viewport()->update();
}

Qt::CursorShape ComposeRichTextEdit::cursorShapeForHandle(ResizeHandle handle) const
{
    switch (handle) {
    case ResizeHandle::TopLeft:
    case ResizeHandle::BottomRight:
        return Qt::SizeFDiagCursor;
    case ResizeHandle::TopRight:
    case ResizeHandle::BottomLeft:
        return Qt::SizeBDiagCursor;
    case ResizeHandle::None:
        break;
    }
    return Qt::ArrowCursor;
}
