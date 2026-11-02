#pragma once

#include <QByteArray>
#include <QFile>
#include <QImage>
#include <QPoint>
#include <QSize>
#include <QString>

#include <optional>
#include <vector>

// The pointer of a recording, kept apart from the video so it can be hidden,
// resized or highlighted after recording (#159). Recorded only for pointer
// position, shape and button presses; never keyboard input.
//
// It travels next to the temporary recording as "<video>.cursor.jsonl": one
// JSON object per line, appended while recording. A crash leaves at most a
// torn last line, which readers ignore. Coordinates are pixels of the
// recorded video (they can lie outside the frame when the pointer leaves the
// recorded screen); times are media time in ms (recording clock minus pauses).
namespace SnapTray {

struct CursorShape {
    quint32 id = 0;      // 1-based; 0 means "unknown shape"
    QPoint hotspot;      // in image pixels
    QImage image;        // video pixels per image pixel: 1
};

struct CursorSample {
    qint64 tMs = 0;
    QPoint position;     // the hotspot, in video pixels
    bool visible = true; // false while an application hides the pointer
    quint32 shapeId = 0;
    bool operator==(const CursorSample& o) const
    {
        return position == o.position && visible == o.visible && shapeId == o.shapeId;
    }
};

struct CursorButtonEvent {
    qint64 tMs = 0;
    Qt::MouseButton button = Qt::LeftButton;
    bool down = false;
    QPoint position;
    // Released by SnapTray, not the user: the recording paused or stopped
    // while the button was held, so no press is left open.
    bool synthetic = false;
};

struct CursorTimelineInfo {
    QSize frameSize;
    bool shapesAvailable = false; // false: positions only, draw a generic arrow
    bool clicksAvailable = false;
    // The video already shows the pointer: drawing it again would double it.
    bool cursorInVideo = false;
};

class CursorTimeline {
public:
    static constexpr int kFormatVersion = 1;

    CursorTimelineInfo info;
    std::vector<CursorShape> shapes;
    std::vector<CursorSample> samples;     // in time order
    std::vector<CursorButtonEvent> buttons; // in time order
    bool finished = false;   // the end record was written (not cut off by a crash)
    bool truncated = false;  // the size limit was reached; later events are missing
    int droppedShapes = 0;   // shape images that could not be read

    // Last sample at or before tMs; the first one for earlier times; nullptr when empty.
    const CursorSample* sampleAt(qint64 tMs) const;
    Qt::MouseButtons buttonsDownAt(qint64 tMs) const;
    const CursorShape* shape(quint32 id) const;

    // Human-readable summary (and, with `events`, every event in time order)
    // for checking a recording's pointer data outside SnapTray.
    QString report(bool events = false) const;

    // nullopt for a missing or unknown header (another format or a newer
    // version). Damage after the header degrades instead: a torn or invalid
    // line ends the read, an unreadable shape image becomes "unknown shape".
    static std::optional<CursorTimeline> parse(const QByteArray& jsonl, QString* error = nullptr);
};

// Appends a timeline to a file as it is recorded. Lines are buffered and
// written by flush() (or when the buffer fills), so a crash loses at most
// the last unflushed second. Never throws away the video: failures only stop
// this file and are reported once.
class CursorTimelineWriter {
public:
    static constexpr qint64 kDefaultMaxBytes = 64ll * 1024 * 1024;
    static constexpr int kMaxShapes = 256;
    static constexpr int kMaxShapeEdge = 256;

    explicit CursorTimelineWriter(QString path, qint64 maxBytes = kDefaultMaxBytes);
    ~CursorTimelineWriter();

    bool open(const CursorTimelineInfo& info);
    // False when the shape cannot be stored (too large, too many, write failed).
    bool addShape(const CursorShape& shape);
    void addSample(const CursorSample& sample);
    void addButton(const CursorButtonEvent& event);
    void flush();
    // Writes the end record and closes. Safe to call more than once.
    bool finish();

    bool isOpen() const { return m_file.isOpen(); }
    bool failed() const { return m_failed; }
    bool truncated() const { return m_truncated; }
    qint64 bytes() const { return m_bytes; }
    QString path() const { return m_file.fileName(); }

private:
    // False when the line was not kept (size limit, failure).
    bool append(const QByteArray& line, bool essential);

    QFile m_file;
    QByteArray m_buffer;
    qint64 m_maxBytes;
    qint64 m_bytes = 0;
    int m_shapeCount = 0;
    Qt::MouseButtons m_writtenDown; // presses in the file without their release yet
    bool m_failed = false;
    bool m_truncated = false;
    bool m_finished = false;
};

namespace CursorTimelineSidecar {

constexpr auto kSuffix = ".cursor.jsonl";

QString pathFor(const QString& videoPath);
// nullopt when there is no sidecar or it cannot be used.
std::optional<CursorTimeline> read(const QString& videoPath);
// No-op when there is no sidecar.
void remove(const QString& videoPath);

} // namespace CursorTimelineSidecar

} // namespace SnapTray
