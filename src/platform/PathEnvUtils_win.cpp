#include "platform/PathEnvUtils_win.h"

#include <QDir>

namespace {

QString stripOuterQuotes(QString value)
{
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.mid(1, value.size() - 2);
    }
    return value;
}

} // namespace

namespace PathEnvUtils {

QString normalizePathEntry(QString path)
{
    path = stripOuterQuotes(path.trimmed()).trimmed();
    if (path.isEmpty()) {
        return QString();
    }

    path = QDir::fromNativeSeparators(path);
    path = QDir::cleanPath(path);
    return path.toLower();
}

QStringList splitPathEntries(const QString& pathValue)
{
    return pathValue.split(';', Qt::SkipEmptyParts);
}

bool containsPathEntry(const QStringList& entries, const QString& targetPath)
{
    const QString targetNormalized = normalizePathEntry(targetPath);
    if (targetNormalized.isEmpty()) {
        return false;
    }

    for (const QString& entry : entries) {
        if (normalizePathEntry(entry) == targetNormalized) {
            return true;
        }
    }
    return false;
}

MutationResult installPathEntry(QStringList entries, const QString& targetPath)
{
    MutationResult result{entries, false};
    const QString targetNormalized = normalizePathEntry(targetPath);
    if (targetNormalized.isEmpty()) {
        return result;
    }

    bool found = false;
    int index = 0;
    while (index < result.entries.size()) {
        if (normalizePathEntry(result.entries.at(index)) != targetNormalized) {
            ++index;
            continue;
        }

        if (!found) {
            found = true;
            ++index;
            continue;
        }

        result.entries.removeAt(index);
        result.changed = true;
    }

    if (!found) {
        result.entries.append(targetPath);
        result.changed = true;
    }

    return result;
}

MutationResult uninstallPathEntry(QStringList entries, const QString& targetPath)
{
    MutationResult result{entries, false};
    const QString targetNormalized = normalizePathEntry(targetPath);
    if (targetNormalized.isEmpty()) {
        return result;
    }

    int index = 0;
    while (index < result.entries.size()) {
        if (normalizePathEntry(result.entries.at(index)) == targetNormalized) {
            result.entries.removeAt(index);
            result.changed = true;
            continue;
        }
        ++index;
    }

    return result;
}

} // namespace PathEnvUtils
