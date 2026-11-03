#pragma once

#include <QByteArray>
#include <QFile>
#include <QString>
#include <QtGlobal>

#include <optional>
#include <vector>

// The keys pressed while recording (#187), kept apart from the video so the
// preview can show them as shortcut hints. Privacy first:
//
// - Nothing is recorded unless the user switched it on before recording.
// - The default mode keeps only shortcuts (keys held with Ctrl, Alt or
//   Cmd/Super) and named keys (Enter, Esc, arrows, F-keys...). Plain typing,
//   Shift+letter included, needs the separate "all keys" mode.
// - Nothing is recorded while the system reports secure input (password
//   fields), and no window titles or application names are ever stored.
//
// It travels next to the temporary recording as "<video>.keys.jsonl", one
// JSON object per line, and goes with it: never into the saved file or the
// History. Times are media time in ms, like the pointer sidecar (#159).
namespace SnapTray {

enum class KeyRecordingMode { Off = 0, Shortcuts = 1, AllKeys = 2 };

struct KeyStroke {
    qint64 tMs = 0;
    int key = 0;                      // Qt::Key
    Qt::KeyboardModifiers modifiers;  // held with it (Ctrl, Alt, Shift, Meta)
    QString text;                     // the typed character, only in AllKeys mode
    bool operator==(const KeyStroke& o) const
    {
        return tMs == o.tMs && key == o.key && modifiers == o.modifiers && text == o.text;
    }
};

struct KeyTimelineInfo {
    KeyRecordingMode mode = KeyRecordingMode::Shortcuts;
    // Draw modifiers as macOS symbols (⌘ ⌥ ⌃ ⇧) rather than words.
    bool macSymbols = false;
};

class KeyTimeline {
public:
    static constexpr int kFormatVersion = 1;

    KeyTimelineInfo info;
    std::vector<KeyStroke> strokes; // in time order
    bool finished = false;
    bool truncated = false;

    // nullopt for a missing or unknown header; a torn line ends the read.
    static std::optional<KeyTimeline> parse(const QByteArray& jsonl, QString* error = nullptr);
};

namespace KeyCapturePolicy {
// Keys that are never text: Enter, Esc, Tab, Backspace, Delete, arrows,
// Home/End/Page Up/Page Down, Insert, F1-F35, and the like.
bool isNamedKey(int key);
bool isModifierKey(int key);
// What a mode keeps. A shortcut is a non-modifier key held with Ctrl, Alt
// or Meta (Shift alone is typing); named keys are kept in Shortcuts mode.
bool shouldRecord(KeyRecordingMode mode, int key, Qt::KeyboardModifiers modifiers, bool secureInput);
} // namespace KeyCapturePolicy

// "Ctrl + Shift + T", "⌘ ⇧ T", "Enter", "a".
QString keyStrokeLabel(const KeyStroke& stroke, bool macSymbols);

class KeyTimelineWriter {
public:
    static constexpr qint64 kDefaultMaxBytes = 8ll * 1024 * 1024;

    explicit KeyTimelineWriter(QString path, qint64 maxBytes = kDefaultMaxBytes);
    ~KeyTimelineWriter();

    bool open(const KeyTimelineInfo& info);
    void add(const KeyStroke& stroke);
    void flush();
    bool finish(); // writes the end record; safe to call more than once

    bool isOpen() const { return m_file.isOpen(); }
    bool failed() const { return m_failed; }
    bool truncated() const { return m_truncated; }
    int count() const { return m_count; }

private:
    bool append(const QByteArray& line);

    QFile m_file;
    QByteArray m_buffer;
    qint64 m_maxBytes;
    qint64 m_bytes = 0;
    int m_count = 0;
    bool m_failed = false;
    bool m_truncated = false;
    bool m_finished = false;
};

namespace KeyTimelineSidecar {
constexpr auto kSuffix = ".keys.jsonl";
QString pathFor(const QString& videoPath);
std::optional<KeyTimeline> read(const QString& videoPath);
void remove(const QString& videoPath);
} // namespace KeyTimelineSidecar

} // namespace SnapTray
