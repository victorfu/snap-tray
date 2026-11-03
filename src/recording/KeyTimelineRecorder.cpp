#include "recording/KeyTimelineRecorder.h"

#include <QDebug>

namespace SnapTray {

KeyTimelineRecorder::KeyTimelineRecorder(std::unique_ptr<KeyProbe> probe, Clock clock, KeyRecordingMode mode,
                                         const QString& sidecarPath, bool macSymbols, QObject* parent)
    : QObject(parent)
    , m_probe(std::move(probe))
    , m_clock(std::move(clock))
    , m_mode(mode)
    , m_writer(sidecarPath)
    , m_macSymbols(macSymbols)
{
    m_flushTimer.setInterval(kFlushIntervalMs);
    connect(&m_flushTimer, &QTimer::timeout, this, [this] { m_writer.flush(); });
    if (m_probe) connect(m_probe.get(), &KeyProbe::keyPressed, this, &KeyTimelineRecorder::onKeyPressed);
}

KeyTimelineRecorder::~KeyTimelineRecorder()
{
    stop();
}

bool KeyTimelineRecorder::start()
{
    if (m_running || !m_probe || m_mode == KeyRecordingMode::Off) return false;
    if (!m_writer.open({m_mode, m_macSymbols})) return false;
    m_running = true;
    m_paused = false;
    m_floorMs = m_clock();
    m_flushTimer.start();
    return true;
}

void KeyTimelineRecorder::pause()
{
    m_paused = true;
    m_writer.flush();
}

void KeyTimelineRecorder::resume()
{
    if (!m_running) return;
    m_paused = false;
    m_floorMs = qMax(m_floorMs, m_clock());
}

void KeyTimelineRecorder::stop()
{
    if (!m_running) return;
    m_running = false;
    m_flushTimer.stop();
    m_writer.finish();
}

void KeyTimelineRecorder::onKeyPressed(int key, Qt::KeyboardModifiers modifiers, const QString& text, qint64 ageMs)
{
    if (!isRunning()) return;
    // The privacy rules, in one place: the mode, and secure input.
    if (!KeyCapturePolicy::shouldRecord(m_mode, key, modifiers, m_probe->secureInputActive())) return;
    KeyStroke stroke;
    stroke.tMs = qMax(m_floorMs, m_clock() - qMax<qint64>(0, ageMs));
    stroke.key = key;
    // Shift on a shortcut is part of it; on typed text it is already in the text.
    stroke.modifiers = modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier | Qt::ShiftModifier);
    if (m_mode == KeyRecordingMode::AllKeys) stroke.text = text;
    m_floorMs = stroke.tMs;
    m_writer.add(stroke);
}

} // namespace SnapTray
