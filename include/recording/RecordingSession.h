#ifndef RECORDINGSESSION_H
#define RECORDINGSESSION_H

#include <QDateTime>
#include <QList>
#include <QSize>
#include <QString>

#include <functional>
#include <optional>

namespace SnapTray {

/**
 * The manifest kept next to a recording's temp file
 * ("<video>.session.json") until the recording is saved or discarded.
 *
 * It lets SnapTray find recordings an earlier run left behind (crash, power
 * loss, killed process) and tells the startup cleanup which temp files are
 * still somebody's recording. Written atomically (write-and-rename).
 */
struct RecordingSessionInfo {
    static constexpr int kVersion = 1;

    enum class State {
        Recording,  // capture was running; the file may lack a normal finish
        Finished,   // encoder finished; waiting for preview/save
    };

    int version = kVersion;
    QString sessionId;
    State state = State::Recording;
    QString videoPath;
    QString format;          // "mp4", "gif", "webp"
    QSize frameSize;
    int frameRate = 0;
    bool audioRequired = false;
    bool fragmented = false;  // a crash leaves a playable prefix
    // Recorded at the high-quality intermediate, so saving it unedited must
    // still meet the output quality. Unknown (older manifests) counts as true.
    bool intermediate = true;
    QDateTime startedAt;
    QDateTime updatedAt;
    qint64 recordedMs = 0;    // last known recorded duration
};

namespace RecordingSession {

QString manifestPathFor(const QString& videoPath);
// QLockFile path held while this process owns the session.
QString lockPathFor(const QString& videoPath);

bool write(const RecordingSessionInfo& info, QString* error = nullptr);
// nullopt with `error` set for a missing, unreadable, corrupt or
// newer-schema manifest.
std::optional<RecordingSessionInfo> read(const QString& manifestPath, QString* error = nullptr);
void remove(const QString& videoPath);

// What a probe of the video found.
struct ProbeResult {
    bool decodable = false;
    qint64 durationMs = 0;
    bool hasAudio = false;
};
using Probe = std::function<ProbeResult(const QString& videoPath)>;
// True while another live owner (this or another SnapTray) holds the session.
using IsActive = std::function<bool(const QString& videoPath)>;

struct Recoverable {
    RecordingSessionInfo info;
    QString manifestPath;
    bool decodable = false;      // a playable prefix exists
    qint64 durationMs = 0;       // what can actually be played
    bool audioMissing = false;   // audio was required but is not in the file
    bool complete = false;       // finished normally (Finished state)
    QString problem;             // why it cannot be recovered, if not
};

// Finds left-behind sessions in `directory`. Active sessions are skipped.
// Manifests whose video is gone are removed (nothing left to recover);
// unreadable manifests are reported with a problem rather than deleted.
QList<Recoverable> scan(const QString& directory, const Probe& probe, const IsActive& isActive);

// True when `videoPath` still belongs to a session (pending recovery or
// active): temp cleanup must leave it alone.
bool hasSession(const QString& videoPath);

// Deletes a session the user chose to discard: video, window timeline and
// manifest.
void discard(const QString& videoPath);

} // namespace RecordingSession
} // namespace SnapTray

#endif // RECORDINGSESSION_H
