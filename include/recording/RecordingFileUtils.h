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

} // namespace SnapTray::RecordingFileUtils

#endif // RECORDINGFILEUTILS_H
