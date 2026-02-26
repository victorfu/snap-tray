#include <QtTest/QtTest>

#include "compose/RichClipboardExporter.h"

#include <QMimeData>

class tst_RichClipboardExporter : public QObject
{
    Q_OBJECT

private slots:
    void testHtmlOutputContainsTitle();
    void testMarkdownOutputPrefersMarkdownBody();
    void testMarkdownFallbackUsesPlainTextWhenMarkdownHasNoText();
    void testClipboardMimeDataHtmlFlavorContainsExpectedFormats();
    void testClipboardMimeDataMarkdownFlavorContainsMarkdownOnly();
    void testHtmlContainsBase64Image();
};

void tst_RichClipboardExporter::testHtmlOutputContainsTitle()
{
    RichClipboardExporter::ComposeContent content;
    content.title = QStringLiteral("Login button broken");
    content.descriptionHtml = QStringLiteral("<p>The login button does not respond</p>");

    const QString html = RichClipboardExporter::toHtml(content);
    QVERIFY(html.contains(QStringLiteral("<h3>Login button broken</h3>")));
    QVERIFY(html.contains(QStringLiteral("The login button does not respond")));
}

void tst_RichClipboardExporter::testMarkdownOutputPrefersMarkdownBody()
{
    RichClipboardExporter::ComposeContent content;
    content.title = QStringLiteral("Test Issue");
    content.descriptionMarkdown = QStringLiteral("- item 1\n- item 2\n\n**bold** and [link](https://example.com)");
    content.descriptionPlainText = QStringLiteral("fallback plain text");
    content.timestamp = QStringLiteral("2025-02-26T14:30:00");

    const QString markdown = RichClipboardExporter::toMarkdown(content);
    QVERIFY(markdown.contains(QStringLiteral("### Test Issue")));
    QVERIFY(markdown.contains(QStringLiteral("- item 1")));
    QVERIFY(markdown.contains(QStringLiteral("[link](https://example.com)")));
    QVERIFY(markdown.contains(QStringLiteral("Captured: 2025-02-26T14:30:00")));
}

void tst_RichClipboardExporter::testClipboardMimeDataHtmlFlavorContainsExpectedFormats()
{
    RichClipboardExporter::ComposeContent content;
    content.title = QStringLiteral("Test");
    content.descriptionPlainText = QStringLiteral("Plain fallback text");
    content.screenshot = QPixmap(100, 100);
    content.screenshot.fill(Qt::red);

    QMimeData* mimeData = RichClipboardExporter::exportToClipboard(
        content,
        RichClipboardExporter::ClipboardFlavor::HtmlPreferred);

    QVERIFY(mimeData->hasHtml());
    QVERIFY(mimeData->hasText());
    QVERIFY(mimeData->hasImage());
    QVERIFY(mimeData->hasFormat("image/png"));
    QVERIFY(mimeData->hasFormat("text/markdown"));
    QVERIFY(mimeData->hasFormat("text/x-markdown"));
    QCOMPARE(mimeData->text(), RichClipboardExporter::toMarkdown(content, true));
    QVERIFY(mimeData->text().contains(QStringLiteral("data:image/png;base64,")));
    delete mimeData;
}

void tst_RichClipboardExporter::testClipboardMimeDataMarkdownFlavorContainsMarkdownOnly()
{
    RichClipboardExporter::ComposeContent content;
    content.title = QStringLiteral("Markdown Test");
    content.descriptionMarkdown = QStringLiteral("1. first\n2. second");

    QMimeData* mimeData = RichClipboardExporter::exportToClipboard(
        content,
        RichClipboardExporter::ClipboardFlavor::MarkdownPreferred);

    QVERIFY(!mimeData->hasHtml());
    QVERIFY(mimeData->hasText());
    QVERIFY(!mimeData->hasImage());
    QVERIFY(!mimeData->hasFormat("image/png"));
    QVERIFY(mimeData->hasFormat("text/markdown"));
    QVERIFY(mimeData->hasFormat("text/x-markdown"));
    QVERIFY(mimeData->text().contains(QStringLiteral("1. first")));
    delete mimeData;
}

void tst_RichClipboardExporter::testMarkdownFallbackUsesPlainTextWhenMarkdownHasNoText()
{
    RichClipboardExporter::ComposeContent content;
    content.descriptionMarkdown = QStringLiteral("![Screenshot](data:image/png;base64,AAAA)");
    content.descriptionPlainText = QStringLiteral("Actual note text");

    const QString markdown = RichClipboardExporter::toMarkdown(content, false);
    QVERIFY(markdown.contains(QStringLiteral("![Screenshot]")));
    QVERIFY(markdown.contains(QStringLiteral("Actual note text")));
}

void tst_RichClipboardExporter::testHtmlContainsBase64Image()
{
    RichClipboardExporter::ComposeContent content;
    content.screenshot = QPixmap(50, 50);
    content.screenshot.fill(Qt::blue);

    const QString html = RichClipboardExporter::toHtml(content);
    QVERIFY(html.contains(QStringLiteral("data:image/png;base64,")));
}

QTEST_MAIN(tst_RichClipboardExporter)
#include "tst_RichClipboardExporter.moc"
