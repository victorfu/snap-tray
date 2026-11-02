#include "recording/CursorTimelineRecorder.h"

#include <QCryptographicHash>
#include <QDebug>

namespace SnapTray {

namespace {

constexpr Qt::MouseButton kRecordedButtons[] = {
    Qt::LeftButton, Qt::RightButton, Qt::MiddleButton, Qt::BackButton, Qt::ForwardButton,
};

bool isFullyTransparent(const QImage& image)
{
    const QImage argb = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < argb.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(argb.constScanLine(y));
        for (int x = 0; x < argb.width(); ++x) {
            if (qAlpha(row[x]) != 0) return false;
        }
    }
    return true;
}

} // namespace

CursorTimelineRecorder::CursorTimelineRecorder(std::unique_ptr<CursorProbe> probe, Clock clock,
                                               WindowFrameMapping mapping, const QString& sidecarPath,
                                               bool cursorInVideo, QObject* parent)
    : QObject(parent)
    , m_probe(std::move(probe))
    , m_clock(std::move(clock))
    , m_mapping(mapping)
    , m_writer(sidecarPath)
    , m_cursorInVideo(cursorInVideo)
{
    if (m_probe) {
        m_capabilities = m_probe->capabilities();
        connect(m_probe.get(), &CursorProbe::buttonChanged, this, &CursorTimelineRecorder::onButtonChanged);
    }
    m_sampleTimer.setInterval(kSampleIntervalMs);
    m_sampleTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_sampleTimer, &QTimer::timeout, this, &CursorTimelineRecorder::sampleNow);
    m_flushTimer.setInterval(kFlushIntervalMs);
    connect(&m_flushTimer, &QTimer::timeout, this, [this] { m_writer.flush(); });
}

CursorTimelineRecorder::~CursorTimelineRecorder()
{
    stop();
}

QString CursorTimelineRecorder::probeName() const
{
    return m_probe ? m_probe->name() : QString();
}

bool CursorTimelineRecorder::start()
{
    if (m_running || m_stopped || !m_probe) return false;
    CursorTimelineInfo info;
    info.frameSize = m_mapping.physicalRegion.size();
    info.shapesAvailable = m_capabilities.shapes;
    info.clicksAvailable = m_capabilities.clicks;
    info.cursorInVideo = m_cursorInVideo;
    info.pixelRatio = m_mapping.devicePixelRatio;
    if (!m_writer.open(info)) return false;
    m_running = true;
    m_paused = false;
    m_forceSample = true;
    m_buttonFloorMs = m_clock();
    sampleNow();
    m_sampleTimer.start();
    m_flushTimer.start();
    return true;
}

void CursorTimelineRecorder::pause()
{
    if (!m_running || m_paused) return;
    sampleNow();
    releaseHeldButtons();
    m_paused = true;
    m_sampleTimer.stop();
    m_writer.flush();
}

void CursorTimelineRecorder::resume()
{
    if (!m_running || !m_paused) return;
    m_paused = false;
    // The pointer may have moved or changed shape during the pause.
    m_forceSample = true;
    m_buttonFloorMs = qMax(m_buttonFloorMs, m_clock());
    sampleNow();
    m_sampleTimer.start();
}

void CursorTimelineRecorder::stop()
{
    if (m_stopped) return;
    if (m_running && !m_paused) sampleNow();
    releaseHeldButtons();
    m_running = false;
    m_stopped = true;
    m_sampleTimer.stop();
    m_flushTimer.stop();
    m_writer.finish();
}

void CursorTimelineRecorder::sampleNow()
{
    if (!m_running || m_paused || !m_probe) return;
    const auto state = m_probe->poll();
    if (!state) return;
    CursorSample sample;
    sample.tMs = m_clock();
    sample.position = state->physical ? m_mapping.toVideoPointFromPhysical(state->position)
                                      : m_mapping.toVideoPoint(state->position);
    if (m_capabilities.shapes && (state->shapeSerial != m_lastSerial || m_forceSample)) {
        m_lastShapeId = currentShapeId();
        m_lastSerial = state->shapeSerial;
    }
    sample.shapeId = m_lastShapeId;
    sample.visible = !m_shapeHidden;
    if (!m_forceSample && m_last && *m_last == sample) return;
    m_forceSample = false;
    m_last = sample;
    m_writer.addSample(sample);
}

quint32 CursorTimelineRecorder::currentShapeId()
{
    const auto shape = m_probe->currentShape();
    if (!shape || shape->image.isNull()) {
        m_shapeHidden = false;
        return 0;
    }
    // An application hiding the pointer sets an empty image.
    m_shapeHidden = isFullyTransparent(shape->image);
    if (m_shapeHidden) return m_lastShapeId;

    QCryptographicHash hash(QCryptographicHash::Sha1);
    const QImage image = shape->image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    hash.addData(QByteArrayView(reinterpret_cast<const char*>(image.constBits()), image.sizeInBytes()));
    hash.addData(QByteArray::number(image.width()) + 'x' + QByteArray::number(image.height()) + '@'
                 + QByteArray::number(shape->hotspot.x()) + ',' + QByteArray::number(shape->hotspot.y()));
    const QByteArray key = hash.result();
    if (const auto it = m_shapeIds.constFind(key); it != m_shapeIds.cend()) {
        return it.value();
    }
    CursorShape stored{m_nextShapeId, shape->hotspot, image};
    // Shapes that cannot be stored (too large, too many) are recorded as
    // "unknown"; positions keep going either way.
    const quint32 id = m_writer.addShape(stored) ? m_nextShapeId++ : 0;
    m_shapeIds.insert(key, id);
    return id;
}

void CursorTimelineRecorder::onButtonChanged(Qt::MouseButton button, bool down, qint64 ageMs)
{
    if (!m_running || m_paused || !m_capabilities.clicks) return;
    // Position the click where it happened, not up to one poll later.
    sampleNow();
    if (m_held.testFlag(button) == down) return; // a press from before start/resume, or a repeat
    m_held.setFlag(button, down);
    CursorButtonEvent event;
    // A queued event happened ageMs before it reached us; keep that time,
    // but never before the recording (re)started or the previous event.
    event.tMs = qMax(m_clock() - qMax<qint64>(0, ageMs), m_buttonFloorMs);
    m_buttonFloorMs = event.tMs;
    event.button = button;
    event.down = down;
    event.position = m_last ? m_last->position : QPoint();
    m_writer.addButton(event);
}

void CursorTimelineRecorder::releaseHeldButtons()
{
    if (!m_held) return;
    const qint64 t = m_clock();
    for (const Qt::MouseButton button : kRecordedButtons) {
        if (!m_held.testFlag(button)) continue;
        CursorButtonEvent event;
        event.tMs = qMax(t, m_buttonFloorMs);
        event.button = button;
        event.down = false;
        event.position = m_last ? m_last->position : QPoint();
        event.synthetic = true;
        m_writer.addButton(event);
    }
    m_buttonFloorMs = qMax(m_buttonFloorMs, t);
    m_held = {};
}

} // namespace SnapTray
