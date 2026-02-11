#ifndef FILENAMETEMPLATEENGINE_H
#define FILENAMETEMPLATEENGINE_H

#include <QDateTime>
#include <QString>

class FilenameTemplateEngine
{
public:
    struct Context {
        QDateTime timestamp = QDateTime::currentDateTime();
        QString type = QStringLiteral("Screenshot");
        QString prefix;
        int width = -1;
        int height = -1;
        QString monitor;
        QString windowTitle;
        QString appName;
        int regionIndex = -1;
        QString ext = QStringLiteral("png");
        QString dateFormat = QStringLiteral("yyyyMMdd_HHmmss");
        int counter = 0;
        QString outputDir;
    };

    struct Result {
        QString filename;
        bool usedFallback = false;
        QString error;
    };

    static QString defaultTemplate();

    static Result renderFilename(const QString& templ, const Context& context);

    static QString buildUniqueFilePath(const QString& outputDir,
                                       const QString& templ,
                                       const Context& context,
                                       int maxAttempts = 100,
                                       QString* error = nullptr,
                                       bool* usedFallback = nullptr);

    static QString sanitizeFilename(const QString& raw);

private:
    static QString ensureExtension(const QString& filename, const QString& ext);
    static QString enforceLengthLimit(const QString& filename, const QString& outputDir);
    static QString appendCounter(const QString& filename, int counter);
    static bool hasCounterToken(const QString& templ);
};

#endif // FILENAMETEMPLATEENGINE_H
