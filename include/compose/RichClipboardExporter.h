#ifndef RICHCLIPBOARDEXPORTER_H
#define RICHCLIPBOARDEXPORTER_H

#include <QPixmap>
#include <QRect>
#include <QString>
#include <QSize>

class QMimeData;

class RichClipboardExporter
{
public:
    enum class ClipboardFlavor {
        HtmlPreferred,
        MarkdownPreferred
    };

    struct ComposeContent {
        QString title;
        QString descriptionHtml;
        QString descriptionMarkdown;
        QString descriptionPlainText;
        QPixmap screenshot;
        QString windowTitle;
        QString appName;
        QString timestamp;
        QString screenName;
        QSize screenResolution;
        QRect captureRegion;
    };

    static QMimeData* exportToClipboard(const ComposeContent& content, ClipboardFlavor flavor);
    static QString toMarkdown(const ComposeContent& content, bool embedImage = false);
    static QString toHtml(const ComposeContent& content);
    static QString toPlainText(const ComposeContent& content);

private:
    static QByteArray pixmapToPngBytes(const QPixmap& pixmap);
    static QString pixmapToBase64DataUri(const QPixmap& pixmap, bool constrainWidth = false);
    static QPixmap scaledForEmbed(const QPixmap& pixmap);
    static QString escapeHtml(const QString& text);
    static QString escapeMarkdown(const QString& text);
};

#endif // RICHCLIPBOARDEXPORTER_H
