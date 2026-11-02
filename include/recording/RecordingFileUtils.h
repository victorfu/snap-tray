#ifndef RECORDINGFILEUTILS_H
#define RECORDINGFILEUTILS_H

#include <QString>

namespace SnapTray::RecordingFileUtils {

/**
 * @brief Move a completed recording into place without exposing an overwrite gap.
 *
 * If destinationPath exists, it is replaced only after the complete source has
 * been copied into a same-directory temporary file. On failure, both the source
 * recording and the existing destination remain untouched.
 */
bool replaceRecordingFile(const QString &sourcePath,
                          const QString &destinationPath,
                          QString *errorMessage = nullptr);

/**
 * @brief Whether replaceRecordingFile() will write a second copy of the source.
 *
 * Only a destination that does not exist yet on the source's volume is a
 * rename; replacing an existing file or moving to another volume copies the
 * whole recording first and needs room for it. The same file needs nothing.
 */
bool saveCopiesData(const QString &sourcePath, const QString &destinationPath);

} // namespace SnapTray::RecordingFileUtils

#endif // RECORDINGFILEUTILS_H
