#pragma once

#include <QObject>
#include <QString>

#include <memory>

// Where key presses come from while recording (#187). Passive: nothing is
// grabbed, intercepted or delayed. A probe reports every press it sees; the
// recorder decides what is kept (KeyCapturePolicy), so privacy rules live in
// one place.
namespace SnapTray {

class KeyProbe : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~KeyProbe() override = default;

    // True while the system says keys go to a secure field (a password
    // entry); nothing is recorded then. False where no such signal exists.
    virtual bool secureInputActive() const { return false; }
    // Whether secureInputActive() means anything on this system.
    virtual bool detectsSecureInput() const { return false; }
    virtual QString name() const = 0;
    // Reads, now, what the system has already queued, so the recorder can
    // take it while it is still paused (PR #200 review). No-op by default.
    virtual void poll() {}

signals:
    // A key went down. `modifiers` are the ones held with it; `text` is what
    // it types (may be empty). ageMs: how long ago, for late delivery.
    void keyPressed(int key, Qt::KeyboardModifiers modifiers, const QString& text, qint64 ageMs = 0);
};

// The probe for this platform and session, or nullptr where keys cannot be
// read (the feature is then unavailable; recording is unaffected).
std::unique_ptr<KeyProbe> createKeyProbe();
// Whether createKeyProbe() can work here, for the settings and setup UI.
bool keyRecordingSupported();

} // namespace SnapTray
