#include "utils/FilenameTemplateEngine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
#include <QUuid>
#include <QTextBoundaryFinder>
#ifdef Q_OS_LINUX
#include <unistd.h>
#endif

namespace {

QString normalizeExt(QString ext)
{
    ext = ext.trimmed();
    while (ext.startsWith('.')) {
        ext.remove(0, 1);
    }
    return ext;
}

bool isLikelyDateFormatToken(const QString& token)
{
    static const QString kDateChars = QStringLiteral("yMdHhmszAPap");
    static const QString kAllowedPunctuation = QStringLiteral("_-:. /'");

    bool hasDateSpecifier = false;
    for (const QChar ch : token) {
        if (kDateChars.contains(ch)) {
            hasDateSpecifier = true;
            continue;
        }
        if (kAllowedPunctuation.contains(ch)) {
            continue;
        }
        return false;
    }
    return hasDateSpecifier;
}

QString tokenOrUnknown(const QString& value)
{
    return value.trimmed().isEmpty() ? QStringLiteral("unknown") : value;
}

QString renderTemplateInternal(const QString& templ,
                               const FilenameTemplateEngine::Context& context,
                               QString* error)
{
    QString out;
    out.reserve(templ.size() + 32);

    const QDateTime timestamp = context.timestamp.isValid()
        ? context.timestamp
        : QDateTime::currentDateTime();
    const QString normalizedExt = normalizeExt(context.ext);

    auto resolveToken = [&](const QString& token, bool* ok) -> QString {
        *ok = true;

        if (token == QStringLiteral("#")) {
            return context.counter > 0 ? QString::number(context.counter) : QString();
        }
        if (token.startsWith(QStringLiteral("#:"))) {
            bool widthOk = false;
            const int width = token.mid(2).toInt(&widthOk);
            if (!widthOk || width <= 0) {
                *ok = false;
                if (error) {
                    *error = QStringLiteral("Invalid counter token: {%1}").arg(token);
                }
                return QString();
            }
            if (context.counter <= 0) {
                return QString();
            }
            return QStringLiteral("%1").arg(context.counter, width, 10, QChar('0'));
        }
        if (token == QStringLiteral("prefix")) {
            return context.prefix.trimmed();
        }
        if (token == QStringLiteral("type")) {
            return tokenOrUnknown(context.type);
        }
        if (token == QStringLiteral("date")) {
            const QString format = context.dateFormat.trimmed().isEmpty()
                ? QStringLiteral("yyyyMMdd_HHmmss")
                : context.dateFormat.trimmed();
            return timestamp.toString(format);
        }
        if (token.startsWith(QStringLiteral("date:"))) {
            const QString format = token.mid(5).trimmed();
            return timestamp.toString(format);
        }
        if (token == QStringLiteral("w")) {
            return context.width > 0 ? QString::number(context.width) : QStringLiteral("unknown");
        }
        if (token == QStringLiteral("h")) {
            return context.height > 0 ? QString::number(context.height) : QStringLiteral("unknown");
        }
        if (token == QStringLiteral("monitor")) {
            return tokenOrUnknown(context.monitor);
        }
        if (token == QStringLiteral("windowTitle")) {
            return tokenOrUnknown(context.windowTitle);
        }
        if (token == QStringLiteral("appName")) {
            return tokenOrUnknown(context.appName);
        }
        if (token == QStringLiteral("regionIndex")) {
            return context.regionIndex >= 0 ? QString::number(context.regionIndex) : QStringLiteral("unknown");
        }
        if (token == QStringLiteral("ext")) {
            return normalizedExt.isEmpty() ? QStringLiteral("png") : normalizedExt;
        }
        if (isLikelyDateFormatToken(token)) {
            return timestamp.toString(token);
        }

        *ok = false;
        if (error) {
            *error = QStringLiteral("Unknown token: {%1}").arg(token);
        }
        return QString();
    };

    for (int i = 0; i < templ.size();) {
        const QChar ch = templ.at(i);
        if (ch == QChar('{')) {
            if (i + 1 < templ.size() && templ.at(i + 1) == QChar('{')) {
                out.append(QChar('{'));
                i += 2;
                continue;
            }

            const int closePos = templ.indexOf(QChar('}'), i + 1);
            if (closePos < 0) {
                if (error) {
                    *error = QStringLiteral("Missing closing brace in template");
                }
                return QString();
            }

            const QString token = templ.mid(i + 1, closePos - i - 1).trimmed();
            if (token.isEmpty()) {
                if (error) {
                    *error = QStringLiteral("Empty token in template");
                }
                return QString();
            }

            bool ok = false;
            const QString value = resolveToken(token, &ok);
            if (!ok) {
                return QString();
            }

            out.append(value);
            i = closePos + 1;
            continue;
        }

        if (ch == QChar('}')) {
            if (i + 1 < templ.size() && templ.at(i + 1) == QChar('}')) {
                out.append(QChar('}'));
                i += 2;
                continue;
            }
            if (error) {
                *error = QStringLiteral("Unexpected closing brace in template");
            }
            return QString();
        }

        out.append(ch);
        ++i;
    }

    return out;
}

bool isWindowsReservedName(const QString& filename)
{
#ifdef Q_OS_WIN
    const QString baseName = QFileInfo(filename).completeBaseName().toUpper();
    static const QStringList kReserved = {
        QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"), QStringLiteral("NUL"),
        QStringLiteral("COM1"), QStringLiteral("COM2"), QStringLiteral("COM3"), QStringLiteral("COM4"),
        QStringLiteral("COM5"), QStringLiteral("COM6"), QStringLiteral("COM7"), QStringLiteral("COM8"),
        QStringLiteral("COM9"), QStringLiteral("LPT1"), QStringLiteral("LPT2"), QStringLiteral("LPT3"),
        QStringLiteral("LPT4"), QStringLiteral("LPT5"), QStringLiteral("LPT6"), QStringLiteral("LPT7"),
        QStringLiteral("LPT8"), QStringLiteral("LPT9")
    };
    return kReserved.contains(baseName);
#else
    Q_UNUSED(filename);
    return false;
#endif
}

} // namespace

QString FilenameTemplateEngine::defaultTemplate()
{
    return QStringLiteral("{prefix}_{type}_{yyyyMMdd_HHmmss}_{w}x{h}_{monitor}_{windowTitle}.{ext}");
}

FilenameTemplateEngine::Result FilenameTemplateEngine::renderFilename(const QString& templ,
                                                                      const Context& context)
{
    Result result;
    Context normalizedContext = context;

    if (!normalizedContext.timestamp.isValid()) {
        normalizedContext.timestamp = QDateTime::currentDateTime();
    }
    if (normalizedContext.dateFormat.trimmed().isEmpty()) {
        normalizedContext.dateFormat = QStringLiteral("yyyyMMdd_HHmmss");
    }
    normalizedContext.ext = normalizeExt(normalizedContext.ext);
    if (normalizedContext.ext.isEmpty()) {
        normalizedContext.ext = QStringLiteral("png");
    }

    const QString templateToUse = templ.trimmed().isEmpty() ? defaultTemplate() : templ.trimmed();

    QString renderError;
    QString rendered = renderTemplateInternal(templateToUse, normalizedContext, &renderError);
    if (rendered.isEmpty()) {
        result.usedFallback = true;
        result.error = renderError;
        QString fallbackError;
        rendered = renderTemplateInternal(defaultTemplate(), normalizedContext, &fallbackError);
        if (rendered.isEmpty()) {
            rendered = QStringLiteral("SnapTray_%1.%2")
                           .arg(normalizedContext.timestamp.toString(QStringLiteral("yyyyMMdd_HHmmss")))
                           .arg(normalizedContext.ext);
        }
    }

    QString filename = sanitizeFilename(rendered);
    if (filename.isEmpty()) {
        result.usedFallback = true;
        if (!result.error.isEmpty()) {
            result.error += QStringLiteral("; ");
        }
        result.error += QStringLiteral("Template rendered an empty filename");
        filename = QStringLiteral("SnapTray_%1.%2")
                       .arg(normalizedContext.timestamp.toString(QStringLiteral("yyyyMMdd_HHmmss")))
                       .arg(normalizedContext.ext);
    }

    filename = ensureExtension(filename, normalizedContext.ext);
    QString counterSuffix;
    if (!result.usedFallback && normalizedContext.counter > 0) {
        // Reserve a terminal counter declared by the template, never an
        // arbitrary numeric suffix that came from a window title or app name.
        static const QRegularExpression terminalCounter(
            QStringLiteral(R"(([_\-.]?)\{#(?::(\d+))?\}(?:\.(?:\{ext\}|[^{}]+))?$)"));
        const auto match = terminalCounter.match(templateToUse);
        if (match.hasMatch()) {
            counterSuffix = match.captured(1) + QString::number(normalizedContext.counter)
                .rightJustified(match.captured(2).toInt(), QChar('0'));
            if (!QFileInfo(filename).completeBaseName().endsWith(counterSuffix)) counterSuffix.clear();
        }
    }
    filename = enforceLengthLimit(filename, normalizedContext.outputDir, counterSuffix);
    if (filename.isEmpty()) {
        result.error = QStringLiteral("Filename length limit is too small for the required suffix");
    }
    result.filename = filename;
    return result;
}

QString FilenameTemplateEngine::collisionFilename(const QString& templ, const Context& context,
                                                   const QString& initialFilename, int attempt,
                                                   const QString& uuidSuffix)
{
    if (!uuidSuffix.isEmpty()) {
        const QFileInfo info(initialFilename);
        QString name = info.completeBaseName() + QChar('_') + uuidSuffix;
        if (!info.suffix().isEmpty())
            name += QChar('.') + info.suffix();
        return enforceLengthLimit(name, context.outputDir, QChar('_') + uuidSuffix);
    }
    if (attempt == 0)
        return initialFilename;
    const QString effectiveTemplate = templ.trimmed().isEmpty() ? defaultTemplate() : templ.trimmed();
    if (hasCounterToken(effectiveTemplate)) {
        Context numbered = context;
        numbered.counter = attempt;
        return renderFilename(effectiveTemplate, numbered).filename;
    }
    return enforceLengthLimit(appendCounter(initialFilename, attempt), context.outputDir,
                              QChar('_') + QString::number(attempt));
}

QString FilenameTemplateEngine::buildUniqueFilePath(const QString& outputDir,
                                                    const QString& templ,
                                                    const Context& context,
                                                    int maxAttempts,
                                                    QString* error,
                                                    bool* usedFallback)
{
    Context localContext = context;
    localContext.outputDir = outputDir;

    const Result initialResult = renderFilename(templ, localContext);
    if (usedFallback) {
        *usedFallback = initialResult.usedFallback;
    }
    if (error) {
        *error = initialResult.error;
    }
    if (initialResult.filename.isEmpty()) return {};

    const QDir dir(outputDir);
    const QString initialPath = dir.filePath(initialResult.filename);
    if (!QFile::exists(initialPath)) {
        return initialPath;
    }

    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
        const QString candidateName = collisionFilename(templ, localContext, initialResult.filename, attempt);
        if (candidateName.isEmpty()) {
            if (error) *error = QStringLiteral("Filename length limit is too small for a collision suffix");
            return {};
        }
        const QString candidatePath = dir.filePath(candidateName);
        if (!QFile::exists(candidatePath)) {
            return candidatePath;
        }
    }

    const QString uuid = QUuid::createUuid().toString(QUuid::Id128).left(8);
    const QString uuidName = collisionFilename(templ, localContext, initialResult.filename, 0, uuid);
    if (uuidName.isEmpty()) {
        if (error) *error = QStringLiteral("Filename length limit is too small for a UUID suffix");
        return {};
    }
    return dir.filePath(uuidName);
}

QString FilenameTemplateEngine::sanitizeFilename(const QString& raw)
{
    QString sanitized = raw.trimmed();
    if (sanitized.isEmpty()) {
        return QString();
    }

    sanitized.replace(QRegularExpression(QStringLiteral("[\\x00-\\x1F]")), QStringLiteral("_"));
#ifdef Q_OS_WIN
    sanitized.replace(QRegularExpression(QStringLiteral(R"([<>:"/\\|?*])")), QStringLiteral("_"));
#else
    sanitized.replace(QRegularExpression(QStringLiteral(R"([/:])")), QStringLiteral("_"));
#endif
    sanitized.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral("_"));
    sanitized.replace(QRegularExpression(QStringLiteral("_+")), QStringLiteral("_"));
    sanitized.replace(QRegularExpression(QStringLiteral("_+\\.")), QStringLiteral("."));
    sanitized.replace(QRegularExpression(QStringLiteral("\\._+")), QStringLiteral("."));
    sanitized.replace(QRegularExpression(QStringLiteral("^_+")), QString());
    sanitized.replace(QRegularExpression(QStringLiteral("_+$")), QString());
#ifdef Q_OS_WIN
    sanitized.replace(QRegularExpression(QStringLiteral("[\\.\\s]+$")), QString());
#endif

    if (isWindowsReservedName(sanitized)) {
        sanitized.prepend(QChar('_'));
    }

    return sanitized.trimmed();
}

QString FilenameTemplateEngine::ensureExtension(const QString& filename, const QString& ext)
{
    QString output = filename.trimmed();
    QString normalizedExt = normalizeExt(ext);
    if (normalizedExt.isEmpty()) {
        return output;
    }
    if (output.endsWith(QStringLiteral(".") + normalizedExt, Qt::CaseInsensitive)) {
        return output;
    }
    if (output.endsWith(QChar('.'))) {
        output.chop(1);
    }
    if (output.isEmpty()) {
        output = QStringLiteral("SnapTray");
    }
    return output + QStringLiteral(".") + normalizedExt;
}

QString FilenameTemplateEngine::enforceLengthLimit(const QString& filename, const QString& outputDir,
                                                   const QString& collisionSuffix)
{
    constexpr int kDefaultComponentLimit = 255;
    int maxNameLength = kDefaultComponentLimit;
#ifdef Q_OS_WIN
    if (!outputDir.isEmpty()) {
        const int maxFromPath = 240 - outputDir.length() - 1;
        if (maxFromPath > 0) {
            maxNameLength = qMin(maxNameLength, maxFromPath);
        }
    }
#elif defined(Q_OS_LINUX)
    if (!outputDir.isEmpty()) {
        const long filesystemLimit = ::pathconf(QFile::encodeName(outputDir).constData(), _PC_NAME_MAX);
        if (filesystemLimit > 0) {
            maxNameLength = static_cast<int>(qMin<long>(maxNameLength, filesystemLimit));
        }
    }
#else
    Q_UNUSED(outputDir);
#endif
#ifdef Q_OS_LINUX
    constexpr bool kUseUtf8Bytes = true;
#else
    constexpr bool kUseUtf8Bytes = false;
#endif
    return limitFilenameComponent(filename, maxNameLength, kUseUtf8Bytes, collisionSuffix);
}

QString FilenameTemplateEngine::limitFilenameComponent(const QString& filename, int limit, bool utf8,
                                                       const QString& collisionSuffix)
{
    const auto length = [utf8](const QString& text) {
        return utf8 ? text.toUtf8().size() : text.size();
    };
    if (length(filename) <= limit) {
        return filename;
    }

    const int dotPos = filename.lastIndexOf(QChar('.'));
    const QString ext = dotPos > 0 ? filename.mid(dotPos) : QString();
    QString base = dotPos > 0 ? filename.left(dotPos) : filename;
    const QString hash = QString::number(qHash(filename), 16).rightJustified(6, QChar('0')).right(6);
    QString suffix = collisionSuffix;
    if (!suffix.isEmpty() && base.endsWith(suffix)) {
        base.chop(suffix.size());
    }
    const QString tail = QChar('~') + hash + suffix + ext;
    const qsizetype budget = limit - length(tail);
    if (budget < 0) return {};

    // Do not split surrogate pairs, combining marks or ZWJ emoji sequences.
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, base);
    qsizetype used = 0;
    qsizetype end = 0;
    for (qsizetype next = finder.toNextBoundary(); next >= 0; next = finder.toNextBoundary()) {
        used += length(base.mid(end, next - end));
        if (used > budget) break;
        end = next;
    }
    return base.left(end) + tail;
}

QString FilenameTemplateEngine::appendCounter(const QString& filename, int counter)
{
    if (counter <= 0) {
        return filename;
    }

    const QFileInfo info(filename);
    const QString baseName = info.completeBaseName();
    const QString ext = info.suffix();
    if (ext.isEmpty()) {
        return QStringLiteral("%1_%2").arg(baseName).arg(counter);
    }
    return QStringLiteral("%1_%2.%3").arg(baseName).arg(counter).arg(ext);
}

bool FilenameTemplateEngine::hasCounterToken(const QString& templ)
{
    static const QRegularExpression counterToken(QStringLiteral(R"(\{#(?::\d+)?\})"));
    return counterToken.match(templ).hasMatch();
}
