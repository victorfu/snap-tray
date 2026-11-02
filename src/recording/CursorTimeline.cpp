#include "recording/CursorTimeline.h"

#include <QBuffer>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTextStream>

#include <algorithm>

namespace SnapTray {

namespace {

constexpr auto kHeaderType = "cursor-timeline";
constexpr qint64 kBufferFlushBytes = 64 * 1024;

struct ButtonName {
    Qt::MouseButton button;
    const char* name;
};

constexpr ButtonName kButtonNames[] = {
    {Qt::LeftButton, "left"},
    {Qt::RightButton, "right"},
    {Qt::MiddleButton, "middle"},
    {Qt::BackButton, "back"},
    {Qt::ForwardButton, "forward"},
};

const char* nameFor(Qt::MouseButton button)
{
    for (const auto& entry : kButtonNames) {
        if (entry.button == button) return entry.name;
    }
    return nullptr;
}

std::optional<Qt::MouseButton> buttonFor(const QString& name)
{
    for (const auto& entry : kButtonNames) {
        if (name == QLatin1String(entry.name)) return entry.button;
    }
    return std::nullopt;
}

QJsonArray pair(int a, int b) { return QJsonArray{a, b}; }

QByteArray line(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}

bool readPair(const QJsonValue& value, int* a, int* b)
{
    const QJsonArray array = value.toArray();
    if (array.size() != 2 || !array[0].isDouble() || !array[1].isDouble()) return false;
    *a = array[0].toInt();
    *b = array[1].toInt();
    return true;
}

} // namespace

const CursorSample* CursorTimeline::sampleAt(qint64 tMs) const
{
    if (samples.empty()) return nullptr;
    auto it = std::upper_bound(samples.begin(), samples.end(), tMs,
                               [](qint64 t, const CursorSample& s) { return t < s.tMs; });
    return it == samples.begin() ? &samples.front() : &*(it - 1);
}

Qt::MouseButtons CursorTimeline::buttonsDownAt(qint64 tMs) const
{
    Qt::MouseButtons down;
    for (const auto& event : buttons) {
        if (event.tMs > tMs) break;
        down.setFlag(event.button, event.down);
    }
    return down;
}

const CursorShape* CursorTimeline::shape(quint32 id) const
{
    for (const auto& shape : shapes) {
        if (shape.id == id) return &shape;
    }
    return nullptr;
}

QString CursorTimeline::report(bool events) const
{
    QString out;
    QTextStream stream(&out);
    const auto yesNo = [](bool v) { return v ? "yes" : "no"; };
    const qint64 lastMs = std::max(samples.empty() ? 0 : samples.back().tMs,
                                   buttons.empty() ? 0 : buttons.back().tMs);
    stream << "frame " << info.frameSize.width() << 'x' << info.frameSize.height()
           << ", shapes " << yesNo(info.shapesAvailable) << ", clicks " << yesNo(info.clicksAvailable)
           << ", pointer in video " << yesNo(info.cursorInVideo) << '\n';
    stream << "complete " << yesNo(finished) << ", truncated " << yesNo(truncated)
           << ", unreadable shapes " << droppedShapes << '\n';
    int presses = 0;
    int synthetic = 0;
    for (const auto& event : buttons) {
        presses += event.down ? 1 : 0;
        synthetic += event.synthetic ? 1 : 0;
    }
    stream << shapes.size() << " shapes, " << samples.size() << " moves, " << presses << " presses ("
           << synthetic << " releases by SnapTray), last event at " << lastMs << " ms\n";
    if (!events) return out;

    // Merge both streams in time order; a press sorts after a move at the same time.
    size_t m = 0;
    size_t b = 0;
    while (m < samples.size() || b < buttons.size()) {
        const bool takeMove = b >= buttons.size() || (m < samples.size() && samples[m].tMs <= buttons[b].tMs);
        if (takeMove) {
            const auto& s = samples[m++];
            stream << s.tMs << " ms  move " << s.position.x() << ',' << s.position.y() << " shape " << s.shapeId
                   << (s.visible ? "" : " hidden") << '\n';
        } else {
            const auto& e = buttons[b++];
            stream << e.tMs << " ms  " << nameFor(e.button) << (e.down ? " down " : " up ") << e.position.x() << ','
                   << e.position.y() << (e.synthetic ? " (released by SnapTray)" : "") << '\n';
        }
    }
    return out;
}

std::optional<CursorTimeline> CursorTimeline::parse(const QByteArray& jsonl, QString* error)
{
    const auto fail = [error](const QString& message) -> std::optional<CursorTimeline> {
        if (error) *error = message;
        return std::nullopt;
    };
    const QList<QByteArray> lines = jsonl.split('\n');
    if (lines.isEmpty() || lines.first().trimmed().isEmpty()) return fail(QStringLiteral("empty file"));
    const QJsonObject header = QJsonDocument::fromJson(lines.first()).object();
    if (header.value(QStringLiteral("type")).toString() != QLatin1String(kHeaderType)) {
        return fail(QStringLiteral("not a cursor timeline"));
    }
    if (header.value(QStringLiteral("version")).toInt() != kFormatVersion) {
        return fail(QStringLiteral("unsupported version %1").arg(header.value(QStringLiteral("version")).toInt()));
    }
    CursorTimeline timeline;
    int w = 0, h = 0;
    if (!readPair(header.value(QStringLiteral("frame")), &w, &h) || w <= 0 || h <= 0) {
        return fail(QStringLiteral("missing frame size"));
    }
    timeline.info.frameSize = QSize(w, h);
    timeline.info.shapesAvailable = header.value(QStringLiteral("shapes")).toBool();
    timeline.info.clicksAvailable = header.value(QStringLiteral("clicks")).toBool();
    timeline.info.cursorInVideo = header.value(QStringLiteral("cursorInVideo")).toBool();
    const double pixelRatio = header.value(QStringLiteral("pixelRatio")).toDouble(1.0);
    timeline.info.pixelRatio = pixelRatio > 0.0 ? pixelRatio : 1.0;

    QSet<quint32> unreadableShapes;
    for (int i = 1; i < lines.size(); ++i) {
        if (lines[i].trimmed().isEmpty()) continue;
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(lines[i], &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            // A torn line from a crash: keep everything before it.
            qDebug() << "CursorTimeline: stopping at unreadable line" << i + 1;
            break;
        }
        const QJsonObject record = document.object();
        const QString type = record.value(QStringLiteral("type")).toString();
        const qint64 t = record.value(QStringLiteral("t")).toInteger();
        if (type == QLatin1String("shape")) {
            CursorShape shape;
            shape.id = quint32(record.value(QStringLiteral("id")).toInteger());
            int hx = 0, hy = 0;
            readPair(record.value(QStringLiteral("hotspot")), &hx, &hy);
            shape.hotspot = QPoint(hx, hy);
            const QByteArray png = QByteArray::fromBase64(record.value(QStringLiteral("png")).toString().toLatin1());
            shape.image.loadFromData(png, "PNG");
            if (shape.id == 0 || shape.image.isNull()) {
                ++timeline.droppedShapes;
                unreadableShapes.insert(shape.id);
                continue;
            }
            timeline.shapes.push_back(std::move(shape));
        } else if (type == QLatin1String("move")) {
            CursorSample sample;
            sample.tMs = t;
            sample.position = QPoint(record.value(QStringLiteral("x")).toInt(), record.value(QStringLiteral("y")).toInt());
            sample.visible = record.value(QStringLiteral("visible")).toBool(true);
            sample.shapeId = quint32(record.value(QStringLiteral("shape")).toInteger());
            if (unreadableShapes.contains(sample.shapeId)) sample.shapeId = 0;
            timeline.samples.push_back(sample);
        } else if (type == QLatin1String("button")) {
            const auto button = buttonFor(record.value(QStringLiteral("button")).toString());
            if (!button) continue;
            CursorButtonEvent event;
            event.tMs = t;
            event.button = *button;
            event.down = record.value(QStringLiteral("down")).toBool();
            event.position = QPoint(record.value(QStringLiteral("x")).toInt(), record.value(QStringLiteral("y")).toInt());
            event.synthetic = record.value(QStringLiteral("synthetic")).toBool();
            timeline.buttons.push_back(event);
        } else if (type == QLatin1String("end")) {
            timeline.finished = true;
            timeline.truncated = record.value(QStringLiteral("truncated")).toBool();
            break;
        }
        // Unknown record types from a later minor addition are skipped.
    }
    return timeline;
}

CursorTimelineWriter::CursorTimelineWriter(QString path, qint64 maxBytes)
    : m_file(std::move(path))
    , m_maxBytes(maxBytes)
{
}

CursorTimelineWriter::~CursorTimelineWriter()
{
    flush();
}

bool CursorTimelineWriter::open(const CursorTimelineInfo& info)
{
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "CursorTimelineWriter: cannot open" << m_file.fileName() << m_file.errorString();
        m_failed = true;
        return false;
    }
    append(line({{QStringLiteral("type"), QLatin1String(kHeaderType)},
                 {QStringLiteral("version"), CursorTimeline::kFormatVersion},
                 {QStringLiteral("frame"), pair(info.frameSize.width(), info.frameSize.height())},
                 {QStringLiteral("coordinates"), QStringLiteral("video-pixels")},
                 {QStringLiteral("shapes"), info.shapesAvailable},
                 {QStringLiteral("clicks"), info.clicksAvailable},
                 {QStringLiteral("cursorInVideo"), info.cursorInVideo},
                 {QStringLiteral("pixelRatio"), info.pixelRatio}}),
           true);
    flush();
    return !m_failed;
}

bool CursorTimelineWriter::addShape(const CursorShape& shape)
{
    if (shape.id == 0 || shape.image.isNull() || m_shapeCount >= kMaxShapes
        || shape.image.width() > kMaxShapeEdge || shape.image.height() > kMaxShapeEdge) {
        return false;
    }
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!shape.image.save(&buffer, "PNG")) return false;
    const qint64 before = m_bytes + m_buffer.size();
    append(line({{QStringLiteral("type"), QStringLiteral("shape")},
                 {QStringLiteral("id"), qint64(shape.id)},
                 {QStringLiteral("hotspot"), pair(shape.hotspot.x(), shape.hotspot.y())},
                 {QStringLiteral("size"), pair(shape.image.width(), shape.image.height())},
                 {QStringLiteral("png"), QString::fromLatin1(png.toBase64())}}),
           false);
    if (m_bytes + m_buffer.size() == before) return false;
    ++m_shapeCount;
    return true;
}

void CursorTimelineWriter::addSample(const CursorSample& sample)
{
    append(line({{QStringLiteral("type"), QStringLiteral("move")},
                 {QStringLiteral("t"), sample.tMs},
                 {QStringLiteral("x"), sample.position.x()},
                 {QStringLiteral("y"), sample.position.y()},
                 {QStringLiteral("visible"), sample.visible},
                 {QStringLiteral("shape"), qint64(sample.shapeId)}}),
           false);
}

void CursorTimelineWriter::addButton(const CursorButtonEvent& event)
{
    const char* name = nameFor(event.button);
    if (!name) return;
    // A release closes a press; it is written even at the size limit so no
    // press is left open.
    append(line({{QStringLiteral("type"), QStringLiteral("button")},
                 {QStringLiteral("t"), event.tMs},
                 {QStringLiteral("button"), QLatin1String(name)},
                 {QStringLiteral("down"), event.down},
                 {QStringLiteral("x"), event.position.x()},
                 {QStringLiteral("y"), event.position.y()},
                 {QStringLiteral("synthetic"), event.synthetic}}),
           !event.down);
}

void CursorTimelineWriter::append(const QByteArray& data, bool essential)
{
    if (m_failed || m_finished || !m_file.isOpen()) return;
    if (!essential && m_bytes + m_buffer.size() + data.size() > m_maxBytes) {
        if (!m_truncated) {
            qWarning() << "CursorTimelineWriter: size limit reached; later pointer events are not recorded";
        }
        m_truncated = true;
        return;
    }
    m_buffer += data;
    if (m_buffer.size() >= kBufferFlushBytes) flush();
}

void CursorTimelineWriter::flush()
{
    if (m_buffer.isEmpty() || m_failed || !m_file.isOpen()) return;
    if (m_file.write(m_buffer) != m_buffer.size() || !m_file.flush()) {
        qWarning() << "CursorTimelineWriter: cannot write" << m_file.fileName() << m_file.errorString();
        m_failed = true;
        m_file.close();
        return;
    }
    m_bytes += m_buffer.size();
    m_buffer.clear();
}

bool CursorTimelineWriter::finish()
{
    if (m_finished) return !m_failed;
    if (m_file.isOpen()) {
        append(line({{QStringLiteral("type"), QStringLiteral("end")},
                     {QStringLiteral("truncated"), m_truncated}}),
               true);
        flush();
        m_file.close();
    }
    m_finished = true;
    return !m_failed;
}

namespace CursorTimelineSidecar {

QString pathFor(const QString& videoPath)
{
    return videoPath + QLatin1String(kSuffix);
}

std::optional<CursorTimeline> read(const QString& videoPath)
{
    QFile file(pathFor(videoPath));
    if (!file.exists()) return std::nullopt;
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "CursorTimelineSidecar: cannot read" << file.fileName() << file.errorString();
        return std::nullopt;
    }
    QString error;
    auto timeline = CursorTimeline::parse(file.readAll(), &error);
    if (!timeline) {
        qDebug() << "CursorTimelineSidecar: ignoring" << file.fileName() << error;
    }
    return timeline;
}

void remove(const QString& videoPath)
{
    const QString path = pathFor(videoPath);
    if (QFile::exists(path) && !QFile::remove(path)) {
        qWarning() << "CursorTimelineSidecar: cannot remove" << path;
    }
}

} // namespace CursorTimelineSidecar

} // namespace SnapTray
