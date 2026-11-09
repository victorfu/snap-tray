#include "platform/AtomicFilePublish.h"

#include <QFile>
#include <QDebug>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#ifdef Q_OS_LINUX
#include <linux/fs.h>
#include <sys/syscall.h>
#else
#include <stdio.h>
#endif
#endif

namespace SnapTray {
namespace {
#ifndef Q_OS_WIN
FilePublishResult unixError(int code)
{
    return {code == EEXIST ? FilePublishStatus::Collision : FilePublishStatus::Failed,
            QString::fromLocal8Bit(std::strerror(code))};
}
#endif
}

FilePublishResult publishFileNoReplace(const QString& source, const QString& destination)
{
#ifdef Q_OS_WIN
    if (MoveFileExW(reinterpret_cast<LPCWSTR>(source.utf16()),
                    reinterpret_cast<LPCWSTR>(destination.utf16()), MOVEFILE_WRITE_THROUGH)) {
        return {FilePublishStatus::Published, {}};
    }
    const DWORD code = GetLastError();
    return {code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS
                ? FilePublishStatus::Collision : FilePublishStatus::Failed,
            QStringLiteral("Unable to publish file (Windows error %1)").arg(code)};
#else
    const QByteArray from = QFile::encodeName(source);
    const QByteArray to = QFile::encodeName(destination);
#if defined(Q_OS_MACOS)
    if (::renameatx_np(AT_FDCWD, from.constData(), AT_FDCWD, to.constData(), RENAME_EXCL) == 0)
        return {FilePublishStatus::Published, {}};
    if (errno != ENOTSUP && errno != EINVAL)
        return unixError(errno);
#elif defined(Q_OS_LINUX) && defined(SYS_renameat2)
    if (::syscall(SYS_renameat2, AT_FDCWD, from.constData(), AT_FDCWD,
                  to.constData(), RENAME_NOREPLACE) == 0)
        return {FilePublishStatus::Published, {}};
    if (errno != ENOSYS && errno != EINVAL && errno != EOPNOTSUPP)
        return unixError(errno);
#endif
    // Hard-link creation also publishes complete contents without replacing to.
    // Unsupported filesystems fail here; ordinary rename is never a fallback.
    if (::link(from.constData(), to.constData()) != 0)
        return unixError(errno);
    if (::unlink(from.constData()) != 0)
        qWarning() << "Published image but could not remove temporary link:" << source;
    return {FilePublishStatus::Published, {}};
#endif
}
}
