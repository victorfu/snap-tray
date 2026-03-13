#pragma once

#include <QObject>
#include <QRect>
#include <QString>

class QEvent;
class QQuickView;
class QQuickItem;

/**
 * @brief QML-based countdown overlay for recording.
 *
 * Drop-in replacement for CountdownOverlay (QWidget-based).
 * Uses QQuickView to render a countdown animation before recording starts.
 *
 * The window is frameless, transparent, and stays on top. Unlike
 * QmlRecordingBoundary, it is NOT click-through — it needs keyboard
 * focus to handle Escape for cancellation.
 *
 * Usage:
 *   auto* overlay = new QmlCountdownOverlay(this);
 *   overlay->setRegion(region);
 *   overlay->setCountdownSeconds(3);
 *   connect(overlay, &QmlCountdownOverlay::countdownFinished, ...);
 *   connect(overlay, &QmlCountdownOverlay::countdownCancelled, ...);
 *   overlay->start();
 */
class QmlCountdownOverlay : public QObject
{
    Q_OBJECT

public:
    explicit QmlCountdownOverlay(QObject* parent = nullptr);
    ~QmlCountdownOverlay() override;

    void setRegion(const QRect& region);
    void setCountdownSeconds(int seconds);

    void start();
    void cancel();
    void close();
    bool isRunning() const { return m_running; }

signals:
    void countdownFinished();
    void countdownCancelled();

private:
    void ensureView();
    void applyPlatformWindowFlags();
    void syncCursorSurface();
    bool eventFilter(QObject* watched, QEvent* event) override;

    QQuickView* m_view = nullptr;
    QQuickItem* m_rootItem = nullptr;
    QRect m_region;
    int m_countdownSeconds = 3;
    bool m_running = false;
    QString m_cursorSurfaceId;
    QString m_cursorOwnerId;
};
