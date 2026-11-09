#include "platform/PathEnvUtils_win.h"

#include <QDir>
#include <QSettings>
#ifdef Q_OS_WIN
#include <QScopeGuard>
#include <qt_windows.h>
#include <vector>
#endif

namespace {

QString stripOuterQuotes(QString value)
{
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.mid(1, value.size() - 2);
    }
    return value;
}

#ifdef Q_OS_WIN
bool persistRegistryPath(QSettings& store, const PathEnvUtils::MutationResult& result)
{
    // This helper only owns the current user's PATH, never machine settings.
    QString keyPath = QDir::toNativeSeparators(store.fileName());
    while (keyPath.startsWith('\\')) keyPath.remove(0, 1);
    const QString prefix = QStringLiteral("HKEY_CURRENT_USER\\");
    if (!keyPath.startsWith(prefix, Qt::CaseInsensitive)) return false;
    keyPath.remove(0, prefix.size());
    if (!store.group().isEmpty()) keyPath += '\\' + QDir::toNativeSeparators(store.group());

    REGSAM access = KEY_QUERY_VALUE | KEY_SET_VALUE;
    if (store.format() == QSettings::Registry32Format) access |= KEY_WOW64_32KEY;
    if (store.format() == QSettings::Registry64Format) access |= KEY_WOW64_64KEY;
    HKEY key = nullptr;
    const auto* nativePath = reinterpret_cast<LPCWSTR>(keyPath.utf16());
    LONG status = RegOpenKeyExW(HKEY_CURRENT_USER, nativePath, 0, access, &key);
    if (status == ERROR_FILE_NOT_FOUND && result.changed) {
        status = RegCreateKeyExW(HKEY_CURRENT_USER, nativePath, 0, nullptr,
                                REG_OPTION_NON_VOLATILE, access, nullptr, &key, nullptr);
    }
    if (status == ERROR_FILE_NOT_FOUND && !result.changed) return result.entries.isEmpty();
    if (status != ERROR_SUCCESS) return false;
    const auto closeKey = qScopeGuard([key] { RegCloseKey(key); });

    DWORD type = REG_EXPAND_SZ;
    status = RegQueryValueExW(key, L"Path", nullptr, &type, nullptr, nullptr);
    if (status == ERROR_FILE_NOT_FOUND) {
        type = REG_EXPAND_SZ;
        if (!result.changed) return result.entries.isEmpty();
    } else if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        return false;
    }

    if (result.changed) {
        const QString expected = result.entries.join(';');
        const DWORD bytes = static_cast<DWORD>((expected.size() + 1) * sizeof(wchar_t));
        if (RegSetValueExW(key, L"Path", 0, type,
                          reinterpret_cast<const BYTE*>(expected.utf16()), bytes) != ERROR_SUCCESS) {
            return false;
        }
    }

    DWORD actualType = 0;
    DWORD bytes = 0;
    if (RegQueryValueExW(key, L"Path", nullptr, &actualType, nullptr, &bytes) != ERROR_SUCCESS
        || actualType != type || bytes % sizeof(wchar_t) != 0) return false;
    std::vector<wchar_t> value(bytes / sizeof(wchar_t) + 1, L'\0');
    if (RegQueryValueExW(key, L"Path", nullptr, &actualType,
                        reinterpret_cast<BYTE*>(value.data()), &bytes) != ERROR_SUCCESS
        || actualType != type) return false;
    return PathEnvUtils::splitPathEntries(QString::fromWCharArray(value.data())) == result.entries;
}
#endif

} // namespace

namespace PathEnvUtils {

bool persistPathEntries(QSettings& store, const MutationResult& result)
{
    if (store.status() != QSettings::NoError) return false;
#ifdef Q_OS_WIN
    if (store.format() == QSettings::NativeFormat
        || store.format() == QSettings::Registry32Format
        || store.format() == QSettings::Registry64Format) {
        store.sync();
        if (store.status() != QSettings::NoError) return false;
        return persistRegistryPath(store, result);
    }
#endif
    const QString expected = result.entries.join(';');
    if (result.changed) store.setValue("Path", expected);
    store.sync();
    if (store.status() != QSettings::NoError) return false;

    QSettings persisted(store.fileName(), store.format());
    const QString actual = persisted.value("Path").toString();
    return persisted.status() == QSettings::NoError
        && splitPathEntries(actual) == result.entries;
}

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
