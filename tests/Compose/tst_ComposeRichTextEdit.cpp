#include <QtTest/QtTest>

#include "compose/ComposeRichTextEdit.h"

#include <QMimeData>
#include <QPixmap>
#include <QTextCursor>
#include <QTextImageFormat>
#include <memory>

class tst_ComposeRichTextEdit : public QObject
{
    Q_OBJECT

private slots:
    void testInsertImageFromPixmapEmbedsDataUri();
    void testInsertImageFromMimeData();
    void testResizeSelectedImage();
};

void tst_ComposeRichTextEdit::testInsertImageFromPixmapEmbedsDataUri()
{
    ComposeRichTextEdit editor;
    editor.setImageMaxWidth(300);

    QPixmap pixmap(120, 80);
    pixmap.fill(Qt::red);

    QVERIFY(editor.insertImageFromPixmap(pixmap));
    QVERIFY(editor.hasEmbeddedImage());

    const QString html = editor.toHtml();
    const QString markdown = editor.toMarkdownContent();
    QVERIFY(html.contains(QStringLiteral("data:image/png;base64,")));
    QVERIFY(markdown.contains(QStringLiteral("data:image/png;base64,")));
}

void tst_ComposeRichTextEdit::testInsertImageFromMimeData()
{
    ComposeRichTextEdit editor;

    auto mimeData = std::make_unique<QMimeData>();
    QImage image(64, 48, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::blue);
    mimeData->setImageData(image);

    QVERIFY(editor.insertImageFromMimeData(mimeData.get()));
    QVERIFY(editor.hasEmbeddedImage());
}

void tst_ComposeRichTextEdit::testResizeSelectedImage()
{
    ComposeRichTextEdit editor;
    QPixmap pixmap(200, 100);
    pixmap.fill(Qt::green);

    QVERIFY(editor.insertImageFromPixmap(pixmap));

    QTextCursor cursor(editor.document());
    cursor.movePosition(QTextCursor::Start);
    editor.setTextCursor(cursor);

    QVERIFY(editor.selectImageAtCursor());
    QVERIFY(editor.resizeSelectedImageToWidth(120));

    const QSize resizedSize = editor.selectedImageSize();
    QCOMPARE(resizedSize.width(), 120);
    QCOMPARE(resizedSize.height(), 60);

    QTextCursor inspectCursor(editor.document());
    inspectCursor.movePosition(QTextCursor::Start);
    const QTextImageFormat imageFormat = inspectCursor.charFormat().toImageFormat();
    QCOMPARE(qRound(imageFormat.width()), 120);
}

QTEST_MAIN(tst_ComposeRichTextEdit)
#include "tst_ComposeRichTextEdit.moc"
