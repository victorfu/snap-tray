#include "recording/KeyTimeline.h"

#include <QDebug>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>

#include <algorithm>
#include <iterator>

namespace SnapTray {

namespace {

constexpr auto kHeaderType = "snaptray-keys";
constexpr qint64 kBufferFlushBytes = 16 * 1024;

QByteArray line(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}

constexpr int kNamedKeys[] = {
    Qt::Key_Escape, Qt::Key_Tab, Qt::Key_Backtab, Qt::Key_Backspace, Qt::Key_Return, Qt::Key_Enter,
    Qt::Key_Insert, Qt::Key_Delete, Qt::Key_Pause, Qt::Key_Print, Qt::Key_Home, Qt::Key_End,
    Qt::Key_Left, Qt::Key_Up, Qt::Key_Right, Qt::Key_Down, Qt::Key_PageUp, Qt::Key_PageDown,
    Qt::Key_Menu, Qt::Key_Space,
};

constexpr int kModifierKeys[] = {
    Qt::Key_Shift, Qt::Key_Control, Qt::Key_Meta, Qt::Key_Alt, Qt::Key_AltGr,
    Qt::Key_CapsLock, Qt::Key_NumLock, Qt::Key_ScrollLock, Qt::Key_Super_L, Qt::Key_Super_R,
    Qt::Key_Hyper_L, Qt::Key_Hyper_R,
};

struct ModeName {
    KeyRecordingMode mode;
    const char* name;
};
constexpr ModeName kModes[] = {
    {KeyRecordingMode::Off, "off"},
    {KeyRecordingMode::Shortcuts, "shortcuts"},
    {KeyRecordingMode::AllKeys, "all"},
};

const char* modeName(KeyRecordingMode mode)
{
    for (const auto& entry : kModes) {
        if (entry.mode == mode) return entry.name;
    }
    return "shortcuts";
}

} // namespace

namespace KeyCapturePolicy {

bool isNamedKey(int key)
{
    if (key >= Qt::Key_F1 && key <= Qt::Key_F35) return true;
    return std::find(std::begin(kNamedKeys), std::end(kNamedKeys), key) != std::end(kNamedKeys);
}

bool isModifierKey(int key)
{
    return std::find(std::begin(kModifierKeys), std::end(kModifierKeys), key) != std::end(kModifierKeys);
}

bool shouldRecord(KeyRecordingMode mode, int key, Qt::KeyboardModifiers modifiers, bool secureInput)
{
    if (mode == KeyRecordingMode::Off || secureInput || key == 0 || isModifierKey(key)) return false;
    if (mode == KeyRecordingMode::AllKeys) return true;
    // Space alone is typing; with a command modifier it is a shortcut.
    const bool command = modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    if (command) return true;
    return key != Qt::Key_Space && isNamedKey(key);
}

} // namespace KeyCapturePolicy

QString keyStrokeLabel(const KeyStroke& stroke, bool macSymbols)
{
    QStringList parts;
    // The usual order for each system. In Qt's terms on macOS, Control is
    // the Cmd key and Meta is the Control key.
    struct Modifier {
        Qt::KeyboardModifier flag;
        const char* word;
        char16_t symbol;
    };
    static const Modifier kWords[] = {
        {Qt::ControlModifier, "Ctrl", 0}, {Qt::AltModifier, "Alt", 0}, {Qt::ShiftModifier, "Shift", 0},
        {Qt::MetaModifier, "Super", 0},
    };
    // ⌃ ⌥ ⇧ ⌘, as code points: no source-encoding surprises.
    static const Modifier kSymbols[] = {
        {Qt::MetaModifier, nullptr, 0x2303}, {Qt::AltModifier, nullptr, 0x2325}, {Qt::ShiftModifier, nullptr, 0x21E7},
        {Qt::ControlModifier, nullptr, 0x2318},
    };
    for (const auto& modifier : macSymbols ? kSymbols : kWords) {
        if (!(stroke.modifiers & modifier.flag)) continue;
        parts << (modifier.word ? QString::fromLatin1(modifier.word) : QString(QChar(modifier.symbol)));
    }
    QString key;
    if (!stroke.text.isEmpty() && !(stroke.modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        && !KeyCapturePolicy::isNamedKey(stroke.key)) {
        key = stroke.text; // typed text shows as typed
    } else {
        key = QKeySequence(stroke.key).toString(QKeySequence::NativeText);
        if (key.isEmpty()) key = QStringLiteral("?");
    }
    parts << key;
    return parts.join(macSymbols ? QStringLiteral(" ") : QStringLiteral(" + "));
}

std::optional<KeyTimeline> KeyTimeline::parse(const QByteArray& jsonl, QString* error)
{
    const QList<QByteArray> lines = jsonl.split('\n');
    if (lines.isEmpty()) {
        if (error) *error = QStringLiteral("empty");
        return std::nullopt;
    }
    const QJsonObject header = QJsonDocument::fromJson(lines.first()).object();
    if (header.value(QStringLiteral("type")).toString() != QLatin1String(kHeaderType)
        || header.value(QStringLiteral("version")).toInt() != kFormatVersion) {
        if (error) *error = QStringLiteral("not a key timeline of a known version");
        return std::nullopt;
    }
    KeyTimeline timeline;
    const QString mode = header.value(QStringLiteral("mode")).toString();
    for (const auto& entry : kModes) {
        if (mode == QLatin1String(entry.name)) timeline.info.mode = entry.mode;
    }
    timeline.info.macSymbols = header.value(QStringLiteral("macSymbols")).toBool(false);
    qint64 lastTime = 0;
    for (qsizetype i = 1; i < lines.size(); ++i) {
        if (lines[i].trimmed().isEmpty()) continue;
        QJsonParseError parseError;
        const QJsonObject object = QJsonDocument::fromJson(lines[i], &parseError).object();
        if (parseError.error != QJsonParseError::NoError) break; // a torn last line
        const QString type = object.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("end")) {
            timeline.finished = true;
            timeline.truncated = object.value(QStringLiteral("truncated")).toBool(false);
            break;
        }
        if (type != QLatin1String("key")) continue;
        KeyStroke stroke;
        stroke.tMs = qint64(object.value(QStringLiteral("t")).toDouble(-1));
        stroke.key = object.value(QStringLiteral("key")).toInt(0);
        stroke.modifiers = Qt::KeyboardModifiers(object.value(QStringLiteral("mods")).toInt(0));
        // Text is only kept from a recording made in the all-keys mode.
        if (timeline.info.mode == KeyRecordingMode::AllKeys) stroke.text = object.value(QStringLiteral("text")).toString();
        if (stroke.tMs < lastTime || stroke.key == 0) break;
        lastTime = stroke.tMs;
        timeline.strokes.push_back(stroke);
    }
    return timeline;
}

KeyTimelineWriter::KeyTimelineWriter(QString path, qint64 maxBytes)
    : m_file(std::move(path))
    , m_maxBytes(maxBytes)
{
}

KeyTimelineWriter::~KeyTimelineWriter()
{
    flush();
}

bool KeyTimelineWriter::open(const KeyTimelineInfo& info)
{
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "KeyTimelineWriter: cannot open" << m_file.fileName() << m_file.errorString();
        m_failed = true;
        return false;
    }
    append(line({{QStringLiteral("type"), QLatin1String(kHeaderType)},
                 {QStringLiteral("version"), KeyTimeline::kFormatVersion},
                 {QStringLiteral("mode"), QLatin1String(modeName(info.mode))},
                 {QStringLiteral("macSymbols"), info.macSymbols}}));
    flush();
    return !m_failed;
}

void KeyTimelineWriter::add(const KeyStroke& stroke)
{
    QJsonObject object{{QStringLiteral("type"), QStringLiteral("key")},
                       {QStringLiteral("t"), stroke.tMs},
                       {QStringLiteral("key"), stroke.key},
                       {QStringLiteral("mods"), int(stroke.modifiers.toInt())}};
    if (!stroke.text.isEmpty()) object.insert(QStringLiteral("text"), stroke.text);
    if (append(line(object))) ++m_count;
}

bool KeyTimelineWriter::append(const QByteArray& data)
{
    if (m_failed || m_finished || !m_file.isOpen()) return false;
    if (m_bytes + m_buffer.size() + data.size() > m_maxBytes) {
        if (!m_truncated) qWarning() << "KeyTimelineWriter: size limit reached; later keys are not recorded";
        m_truncated = true;
        return false;
    }
    m_buffer += data;
    if (m_buffer.size() >= kBufferFlushBytes) flush();
    return true;
}

void KeyTimelineWriter::flush()
{
    if (m_buffer.isEmpty() || m_failed || !m_file.isOpen()) return;
    if (m_file.write(m_buffer) != m_buffer.size() || !m_file.flush()) {
        qWarning() << "KeyTimelineWriter: cannot write" << m_file.fileName() << m_file.errorString();
        m_failed = true;
        m_file.close();
        return;
    }
    m_bytes += m_buffer.size();
    m_buffer.clear();
}

bool KeyTimelineWriter::finish()
{
    if (m_finished || !m_file.isOpen()) return !m_failed;
    const QByteArray end = line({{QStringLiteral("type"), QStringLiteral("end")},
                                 {QStringLiteral("truncated"), m_truncated}});
    m_buffer += end; // the end record is written even at the size limit
    flush();
    m_finished = true;
    m_file.close();
    return !m_failed;
}

namespace KeyTimelineSidecar {

QString pathFor(const QString& videoPath)
{
    return videoPath + QLatin1String(kSuffix);
}

std::optional<KeyTimeline> read(const QString& videoPath)
{
    QFile file(pathFor(videoPath));
    if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
    return KeyTimeline::parse(file.readAll());
}

void remove(const QString& videoPath)
{
    const QString path = pathFor(videoPath);
    if (QFileInfo::exists(path)) QFile::remove(path);
}

} // namespace KeyTimelineSidecar

} // namespace SnapTray
