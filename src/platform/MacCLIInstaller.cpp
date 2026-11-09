#include "platform/MacCLIInstaller.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QUuid>

namespace SnapTray {

QString quoteShellLiteral(const QString& value)
{
    QString escaped = value;
    escaped.replace('\'', QStringLiteral("'\"'\"'"));
    return QStringLiteral("'") + escaped + QStringLiteral("'");
}

QString quoteAppleScriptString(const QString& value)
{
    QString escaped = value;
    escaped.replace('\\', QStringLiteral("\\\\"));
    escaped.replace('"', QStringLiteral("\\\""));
    escaped.replace('\r', QStringLiteral("\\r"));
    escaped.replace('\n', QStringLiteral("\\n"));
    return QStringLiteral("\"") + escaped + QStringLiteral("\"");
}

QByteArray macCLIWrapper(const QString& executable, const QString& bundle)
{
    return (QStringLiteral("#!/bin/sh\n# SnapTray CLI wrapper v2\nexport QT_PLUGIN_PATH=")
        + quoteShellLiteral(QDir(bundle).filePath("Contents/PlugIns"))
        + QStringLiteral("\nexec ") + quoteShellLiteral(executable)
        + QStringLiteral(" \"$@\"\n")).toUtf8();
}

bool isMacCLIWrapperInstalled(const QString& scriptPath, const QString& executable, const QString& bundle)
{
    const QFileInfo binary(executable);
    const QFileInfo script(scriptPath);
    if (!binary.isFile() || !binary.isExecutable() || !script.isFile() || !script.isExecutable()) return false;
    QFile file(scriptPath);
    return file.open(QIODevice::ReadOnly) && file.readAll() == macCLIWrapper(executable, bundle);
}

bool installMacCLIWrapper(const QString& scriptPath, const QString& executable,
                         const QString& bundle, const std::function<bool(const QString&)>& runner)
{
    const QFileInfo binary(executable);
    if (!binary.isAbsolute() || !binary.isFile() || !binary.isExecutable() || !runner) return false;
    QTemporaryFile source;
    const QByteArray wrapper = macCLIWrapper(executable, bundle);
    if (!source.open() || source.write(wrapper) != wrapper.size() || !source.flush()) return false;
    const QFileInfo target(scriptPath);
    const QString staged = QDir(target.absolutePath()).filePath(
        ".snaptray-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
    // Stage beside the destination so rename is atomic. A failed install or
    // chmod cannot truncate an existing, working CLI.
    const QString command = QStringLiteral("set -e\nmkdir -p ")
        + quoteShellLiteral(target.absolutePath())
        + "\ntest ! -d " + quoteShellLiteral(target.absoluteFilePath())
        + "\ntest -x " + quoteShellLiteral(executable)
        + "\nstage=" + quoteShellLiteral(staged)
        + "\ntrap 'rm -f \"$stage\"' EXIT\n/usr/bin/install -m 755 "
        + quoteShellLiteral(source.fileName()) + " \"$stage\"\n/bin/mv -f \"$stage\" "
        + quoteShellLiteral(target.absoluteFilePath());
    return runner(command) && isMacCLIWrapperInstalled(scriptPath, executable, bundle);
}

} // namespace SnapTray
