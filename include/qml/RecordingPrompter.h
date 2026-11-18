#pragma once

#include <QObject>
#include <QPointer>
#include <QScreen>
#include <memory>

class QQuickView;

namespace SnapTray {

// A session-only reader on a separate, non-overlapping screen. Its native
// window is mapped empty; only a confirmed placement may reveal the text.
class RecordingPrompter final : public QObject
{
public:
    RecordingPrompter(QScreen* capture, QScreen* display, QString script, int fontSize);
    ~RecordingPrompter() override;
    static bool separate(QScreen* capture, QScreen* display);
    quintptr captureExclusionId() const;
    void setReading(bool reading);
private:
    void checkPlacement();
    void disable();
    QPointer<QScreen> m_capture;
    QPointer<QScreen> m_display;
    QString m_script;
    std::unique_ptr<QQuickView> m_view;
    bool m_reading = false;
    bool m_disabled = false;
};
}
