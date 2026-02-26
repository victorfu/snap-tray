#include "compose/RichClipboardExporter.h"

#include "compose/ComposeSettingsManager.h"

#include <QBuffer>
#include <QByteArray>
#include <QMimeData>
#include <QRegularExpression>
#include <QStringList>
#include <QTextDocument>

namespace {
QString extractHtmlBodyFragment(const QString& html)
{
    if (html.isEmpty()) {
        return QString();
    }

    static const QRegularExpression bodyRegex(
        QStringLiteral("<body[^>]*>([\\s\\S]*)</body>"),
        QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch match = bodyRegex.match(html);
    if (match.hasMatch()) {
        return match.captured(1).trimmed();
    }

    return html.trimmed();
}

QString metadataLine(const QString& label, const QString& value)
{
    if (value.trimmed().isEmpty()) {
        return QString();
    }
    return QStringLiteral("%1: %2").arg(label, value.trimmed());
}

QString sanitizedPlainText(const QString& plainText)
{
    QString sanitized = plainText;
    sanitized.replace(QChar::ObjectReplacementCharacter, QChar(' '));
    sanitized.replace(QRegularExpression(QStringLiteral("[ \\t]+")), QStringLiteral(" "));
    sanitized.replace(QRegularExpression(QStringLiteral("\\n{3,}")), QStringLiteral("\n\n"));
    return sanitized.trimmed();
}

bool hasMeaningfulText(const QString& markdown)
{
    return markdown.contains(QRegularExpression(QStringLiteral("[\\p{L}\\p{N}]")));
}

QString markdownWithoutImageSyntax(const QString& markdown)
{
    QString stripped = markdown;
    stripped.replace(QRegularExpression(QStringLiteral("!\\[[^\\]]*\\]\\([^\\)]*\\)")), QString());
    stripped.replace(QRegularExpression(
        QStringLiteral("<img\\s+[^>]*>"),
        QRegularExpression::CaseInsensitiveOption), QString());
    return stripped;
}
} // namespace

QMimeData* RichClipboardExporter::exportToClipboard(const ComposeContent& content, ClipboardFlavor flavor)
{
    auto* mimeData = new QMimeData();

    const QString markdown = toMarkdown(content, true);
    const QByteArray markdownUtf8 = markdown.toUtf8();

    if (flavor == ClipboardFlavor::HtmlPreferred) {
        const QString html = toHtml(content);
        mimeData->setHtml(html);
        mimeData->setText(markdown);
        mimeData->setData("text/markdown", markdownUtf8);
        mimeData->setData("text/x-markdown", markdownUtf8);
    } else {
        mimeData->setText(markdown);
        mimeData->setData("text/markdown", markdownUtf8);
        mimeData->setData("text/x-markdown", markdownUtf8);
    }

    if (flavor == ClipboardFlavor::HtmlPreferred && !content.screenshot.isNull()) {
        mimeData->setImageData(content.screenshot.toImage());
        mimeData->setData("image/png", pixmapToPngBytes(content.screenshot));
    }

    return mimeData;
}

QString RichClipboardExporter::toHtml(const ComposeContent& content)
{
    QString html;
    html += QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\"></head><body>");

    if (!content.title.trimmed().isEmpty()) {
        html += QStringLiteral("<h3>%1</h3>").arg(escapeHtml(content.title.trimmed()));
    }

    const QString descriptionFragment = extractHtmlBodyFragment(content.descriptionHtml);
    const bool descriptionContainsImage = descriptionFragment.contains(QStringLiteral("<img"), Qt::CaseInsensitive);
    if (!descriptionFragment.isEmpty()) {
        html += descriptionFragment;
    } else if (!content.descriptionMarkdown.trimmed().isEmpty()) {
        QTextDocument markdownDoc;
        markdownDoc.setMarkdown(content.descriptionMarkdown, QTextDocument::MarkdownDialectGitHub);
        html += extractHtmlBodyFragment(markdownDoc.toHtml());
    } else if (!content.descriptionPlainText.trimmed().isEmpty()) {
        html += QStringLiteral("<p>%1</p>")
            .arg(content.descriptionPlainText.trimmed().toHtmlEscaped().replace('\n', QStringLiteral("<br/>")));
    }

    if (!content.screenshot.isNull() && !descriptionContainsImage) {
        const QString dataUri = pixmapToBase64DataUri(content.screenshot, true);
        html += QStringLiteral(
            "<p><img src=\"%1\" style=\"display:block;max-width:100%%;border:1px solid #d0d0d0;border-radius:4px;\"/></p>")
            .arg(dataUri);
    }

    html += QStringLiteral("<hr/><p style=\"color:#888;font-size:12px;\">");
    bool hasMetadata = false;
    auto appendMetadataField = [&html, &hasMetadata](const QString& field) {
        if (field.trimmed().isEmpty()) {
            return;
        }
        if (hasMetadata) {
            html += QStringLiteral(" | ");
        }
        html += field;
        hasMetadata = true;
    };

    appendMetadataField(QStringLiteral("Captured: %1").arg(escapeHtml(content.timestamp)));
    appendMetadataField(QStringLiteral("Window: %1").arg(escapeHtml(content.windowTitle)));
    appendMetadataField(QStringLiteral("App: %1").arg(escapeHtml(content.appName)));
    appendMetadataField(QStringLiteral("Screen: %1").arg(escapeHtml(content.screenName)));

    if (content.screenResolution.isValid()) {
        appendMetadataField(QStringLiteral("Resolution: %1x%2")
            .arg(content.screenResolution.width())
            .arg(content.screenResolution.height()));
    }

    if (content.captureRegion.isValid()) {
        appendMetadataField(QStringLiteral("Region: %1x%2")
            .arg(content.captureRegion.width())
            .arg(content.captureRegion.height()));
    }

    if (!hasMetadata) {
        html += QStringLiteral("Captured with SnapTray");
    }

    html += QStringLiteral("</p></body></html>");
    return html;
}

QString RichClipboardExporter::toMarkdown(const ComposeContent& content, bool embedImage)
{
    QString markdown;
    const QString plainTextBody = sanitizedPlainText(content.descriptionPlainText);

    if (!content.title.trimmed().isEmpty()) {
        markdown += QStringLiteral("### %1\n\n").arg(escapeMarkdown(content.title.trimmed()));
    }

    const QString markdownBody = content.descriptionMarkdown.trimmed();
    const bool markdownHasText = hasMeaningfulText(markdownWithoutImageSyntax(markdownBody));
    const bool bodyContainsImage = markdownBody.contains(QStringLiteral("data:image"), Qt::CaseInsensitive)
        || markdownBody.contains(QStringLiteral("!["), Qt::CaseSensitive);
    if (!markdownBody.isEmpty()) {
        markdown += markdownBody;
        markdown += QStringLiteral("\n\n");
    }

    if (!plainTextBody.isEmpty() && (!markdownHasText || markdownBody.isEmpty())) {
        markdown += plainTextBody;
        markdown += QStringLiteral("\n\n");
    }

    if (embedImage && !content.screenshot.isNull() && !bodyContainsImage) {
        markdown += QStringLiteral("![Screenshot](%1)\n\n")
            .arg(pixmapToBase64DataUri(content.screenshot, true));
    }

    QStringList metadataFields;
    if (!content.timestamp.trimmed().isEmpty()) {
        metadataFields << metadataLine(QStringLiteral("Captured"), escapeMarkdown(content.timestamp));
    }
    if (!content.windowTitle.trimmed().isEmpty()) {
        metadataFields << metadataLine(QStringLiteral("Window"), escapeMarkdown(content.windowTitle));
    }
    if (!content.appName.trimmed().isEmpty()) {
        metadataFields << metadataLine(QStringLiteral("App"), escapeMarkdown(content.appName));
    }
    if (!content.screenName.trimmed().isEmpty()) {
        metadataFields << metadataLine(QStringLiteral("Screen"), escapeMarkdown(content.screenName));
    }
    if (content.screenResolution.isValid()) {
        metadataFields << QStringLiteral("Resolution: %1x%2")
            .arg(content.screenResolution.width())
            .arg(content.screenResolution.height());
    }
    if (content.captureRegion.isValid()) {
        metadataFields << QStringLiteral("Region: %1x%2")
            .arg(content.captureRegion.width())
            .arg(content.captureRegion.height());
    }

    markdown += QStringLiteral("---\n");
    if (metadataFields.isEmpty()) {
        markdown += QStringLiteral("- Captured with SnapTray\n");
    } else {
        for (const QString& field : metadataFields) {
            markdown += QStringLiteral("- %1\n").arg(field);
        }
    }

    return markdown;
}

QString RichClipboardExporter::toPlainText(const ComposeContent& content)
{
    QString text;

    if (!content.title.trimmed().isEmpty()) {
        text += content.title.trimmed();
        text += QStringLiteral("\n\n");
    }

    if (!content.descriptionPlainText.trimmed().isEmpty()) {
        text += content.descriptionPlainText.trimmed();
        text += QStringLiteral("\n\n");
    }

    QStringList metadataFields;
    if (!content.timestamp.trimmed().isEmpty()) {
        metadataFields << metadataLine(QStringLiteral("Captured"), content.timestamp);
    }
    if (!content.windowTitle.trimmed().isEmpty()) {
        metadataFields << metadataLine(QStringLiteral("Window"), content.windowTitle);
    }
    if (!content.appName.trimmed().isEmpty()) {
        metadataFields << metadataLine(QStringLiteral("App"), content.appName);
    }
    if (!content.screenName.trimmed().isEmpty()) {
        metadataFields << metadataLine(QStringLiteral("Screen"), content.screenName);
    }
    if (content.screenResolution.isValid()) {
        metadataFields << QStringLiteral("Resolution: %1x%2")
            .arg(content.screenResolution.width())
            .arg(content.screenResolution.height());
    }
    if (content.captureRegion.isValid()) {
        metadataFields << QStringLiteral("Region: %1x%2")
            .arg(content.captureRegion.width())
            .arg(content.captureRegion.height());
    }

    if (!metadataFields.isEmpty()) {
        text += metadataFields.join(QStringLiteral(" | "));
    } else {
        text += QStringLiteral("Captured with SnapTray");
    }
    text += QStringLiteral("\n");

    return text;
}

QByteArray RichClipboardExporter::pixmapToPngBytes(const QPixmap& pixmap)
{
    QByteArray pngBytes;
    if (pixmap.isNull()) {
        return pngBytes;
    }

    QBuffer buffer(&pngBytes);
    buffer.open(QIODevice::WriteOnly);
    pixmap.toImage().save(&buffer, "PNG");
    return pngBytes;
}

QString RichClipboardExporter::pixmapToBase64DataUri(const QPixmap& pixmap, bool constrainWidth)
{
    const QPixmap normalized = constrainWidth ? scaledForEmbed(pixmap) : pixmap;
    const QByteArray pngBytes = pixmapToPngBytes(normalized);
    if (pngBytes.isEmpty()) {
        return QString();
    }
    return QStringLiteral("data:image/png;base64,%1")
        .arg(QString::fromLatin1(pngBytes.toBase64()));
}

QPixmap RichClipboardExporter::scaledForEmbed(const QPixmap& pixmap)
{
    if (pixmap.isNull()) {
        return pixmap;
    }

    const int maxWidth = ComposeSettingsManager::instance().screenshotMaxWidth();
    if (pixmap.width() <= maxWidth) {
        return pixmap;
    }

    return pixmap.scaledToWidth(maxWidth, Qt::SmoothTransformation);
}

QString RichClipboardExporter::escapeHtml(const QString& text)
{
    return text.toHtmlEscaped();
}

QString RichClipboardExporter::escapeMarkdown(const QString& text)
{
    QString escaped = text;
    escaped.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    escaped.replace(QStringLiteral("`"), QStringLiteral("\\`"));
    return escaped;
}
