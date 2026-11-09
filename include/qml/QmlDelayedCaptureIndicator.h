#pragma once

#include <QObject>
#include <QPointer>

class QQuickItem;
class QQuickView;
class QScreen;
class QWindow;

namespace SnapTray {

/**
 * @brief Floating countdown pill shown while a delayed capture waits.
 *
 * Unlike QmlCountdownOverlay (recording), this window is click-through and
 * never takes focus: the user must be able to open menus, hover controls and
 * type while the countdown runs. CaptureManager hides it before grabbing the
 * screen so it never appears in the capture.
 *
 * Usage:
 *   auto* indicator = new QmlDelayedCaptureIndicator(this);
 *   indicator->show(screen, 3);
 *   indicator->setRemainingSeconds(2);
 *   indicator->hide();
 */
class QmlDelayedCaptureIndicator : public QObject
{
    Q_OBJECT

public:
    // Distance from the top of the screen's available area.
    static constexpr int kTopMargin = 24;

    explicit QmlDelayedCaptureIndicator(QObject* parent = nullptr);
    ~QmlDelayedCaptureIndicator() override;

    void show(QScreen* screen, int remainingSeconds);
    void setRemainingSeconds(int seconds);
    void hide();
    bool isVisible() const;

    // The native window, for tests and capture exclusion checks. May be null.
    QWindow* window() const;

private:
    void ensureView();
    void reposition();

    QQuickView* m_view = nullptr;
    QQuickItem* m_rootItem = nullptr;
    QPointer<QScreen> m_screen;
};

} // namespace SnapTray
