#ifndef PATHENVUTILS_WIN_H
#define PATHENVUTILS_WIN_H

#include <QString>
#include <QStringList>

namespace PathEnvUtils {

struct MutationResult {
    QStringList entries;
    bool changed = false;
};

QString normalizePathEntry(QString path);
QStringList splitPathEntries(const QString& pathValue);
bool containsPathEntry(const QStringList& entries, const QString& targetPath);
MutationResult installPathEntry(QStringList entries, const QString& targetPath);
MutationResult uninstallPathEntry(QStringList entries, const QString& targetPath);

} // namespace PathEnvUtils

#endif // PATHENVUTILS_WIN_H
