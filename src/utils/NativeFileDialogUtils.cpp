#include "utils/NativeFileDialogUtils.h"

#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QObject>
#include <QWidget>
#include <QWindow>

namespace NativeFileDialogUtils {

namespace {

QString appendDefaultSuffixIfMissing(QString filePath, const QString& nameFilter)
{
    if (filePath.isEmpty() || !QFileInfo(filePath).suffix().isEmpty()) {
        return filePath;
    }

    const QString suffix = suffixForNameFilter(nameFilter);
    if (suffix.isEmpty()) {
        return filePath;
    }

    return filePath + QStringLiteral(".") + suffix;
}

QWindow* resolveTransientParent(QWidget* parent, QWindow* transientParent)
{
    if (transientParent) {
        return transientParent;
    }

    if (!parent) {
        return nullptr;
    }

    if (QWindow* windowHandle = parent->windowHandle()) {
        return windowHandle;
    }

    QWidget* topLevelWindow = parent->window();
    return topLevelWindow ? topLevelWindow->windowHandle() : nullptr;
}

} // namespace

QString suffixForNameFilter(const QString& nameFilter)
{
    const int openParen = nameFilter.indexOf(QLatin1Char('('));
    const int closeParen = nameFilter.indexOf(QLatin1Char(')'), openParen + 1);
    if (openParen < 0 || closeParen <= openParen) {
        return QString();
    }

    const QString patterns = nameFilter.mid(openParen + 1, closeParen - openParen - 1);
    const QStringList entries = patterns.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString& entry : entries) {
        if (!entry.startsWith(QStringLiteral("*."))) {
            continue;
        }

        const QString suffix = entry.mid(2);
        if (!suffix.isEmpty() && suffix != QStringLiteral("*")) {
            return suffix;
        }
    }

    return QString();
}

QString getSaveFileName(QWidget* parent,
                        const QString& title,
                        const QString& defaultPath,
                        const QStringList& nameFilters,
                        QWindow* transientParent)
{
    QFileDialog dialog(parent, title);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);

    const QFileInfo defaultInfo(defaultPath);
    if (!defaultInfo.absolutePath().isEmpty()) {
        dialog.setDirectory(defaultInfo.absolutePath());
    }
    if (!defaultInfo.fileName().isEmpty()) {
        dialog.selectFile(defaultInfo.fileName());
    }

    if (!nameFilters.isEmpty()) {
        dialog.setNameFilters(nameFilters);
        dialog.selectNameFilter(nameFilters.value(0));
        dialog.setDefaultSuffix(suffixForNameFilter(dialog.selectedNameFilter()));
        QObject::connect(&dialog, &QFileDialog::filterSelected, &dialog, [&dialog](const QString& filter) {
            dialog.setDefaultSuffix(suffixForNameFilter(filter));
        });
    }

    dialog.setWindowModality((parent || transientParent) ? Qt::WindowModal : Qt::ApplicationModal);
    dialog.winId();

    if (QWindow* parentWindow = resolveTransientParent(parent, transientParent);
        parentWindow && dialog.windowHandle()) {
        dialog.windowHandle()->setTransientParent(parentWindow);
    }

    if (dialog.exec() != QDialog::Accepted) {
        return QString();
    }

    return appendDefaultSuffixIfMissing(dialog.selectedFiles().value(0), dialog.selectedNameFilter());
}

QString getSaveFileName(QWidget* parent,
                        const QString& title,
                        const QString& defaultPath,
                        const QString& filtersText,
                        QWindow* transientParent)
{
    return getSaveFileName(
        parent,
        title,
        defaultPath,
        filtersText.split(QStringLiteral(";;")),
        transientParent);
}

} // namespace NativeFileDialogUtils
