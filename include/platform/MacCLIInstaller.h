#pragma once

#include <QByteArray>
#include <QString>
#include <functional>

namespace SnapTray {
QString quoteShellLiteral(const QString& value);
QString quoteAppleScriptString(const QString& value);
QByteArray macCLIWrapper(const QString& executable, const QString& bundle);
bool isMacCLIWrapperInstalled(const QString& scriptPath, const QString& executable, const QString& bundle);
// The runner executes the prepared installation command with the required
// privileges. Tests supply an unprivileged shell and a temporary destination.
bool installMacCLIWrapper(const QString& scriptPath, const QString& executable,
                         const QString& bundle, const std::function<bool(const QString&)>& runner);
}
