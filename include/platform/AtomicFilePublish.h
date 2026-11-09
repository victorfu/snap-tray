#pragma once

#include <QString>

namespace SnapTray {
enum class FilePublishStatus { Published, Collision, Failed };
struct FilePublishResult {
    FilePublishStatus status = FilePublishStatus::Failed;
    QString error;
};

// The complete, closed source file and destination must be on the same volume.
// Never replaces an existing destination, including a dangling symbolic link.
FilePublishResult publishFileNoReplace(const QString& source, const QString& destination);
}
