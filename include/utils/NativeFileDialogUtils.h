#ifndef NATIVEFILEDIALOGUTILS_H
#define NATIVEFILEDIALOGUTILS_H

#include <QString>
#include <QStringList>

class QWidget;
class QWindow;

namespace NativeFileDialogUtils {

QString suffixForNameFilter(const QString& nameFilter);

QString getSaveFileName(QWidget* parent,
                        const QString& title,
                        const QString& defaultPath,
                        const QStringList& nameFilters,
                        QWindow* transientParent = nullptr);

QString getSaveFileName(QWidget* parent,
                        const QString& title,
                        const QString& defaultPath,
                        const QString& filtersText,
                        QWindow* transientParent = nullptr);

} // namespace NativeFileDialogUtils

#endif // NATIVEFILEDIALOGUTILS_H
