#pragma once

#include <QObject>
#include <QPointer>
#include <QRect>

class QQuickView;
class QQuickItem;
class QScreen;

namespace SnapTray {

// Chooses an area of one screen before recording (#194). A recording-only
// overlay (the screenshot Region Selector has no recording semantics): it
// covers `screen`, and the area never crosses to another screen.
class QmlRecordingAreaSelector : public QObject
{
    Q_OBJECT

public:
    explicit QmlRecordingAreaSelector(QObject* parent = nullptr);
    ~QmlRecordingAreaSelector() override;

    void start(QScreen* screen);
    void close();
    QScreen* screen() const { return m_screen; }
    // For tests: the QML root.
    QQuickItem* rootItem() const { return m_rootItem; }

signals:
    // Logical, global coordinates on screen().
    void areaChosen(const QRect& area);
    void cancelled();

private slots:
    void onAccepted(qreal x, qreal y, qreal width, qreal height);
    void onFullScreenChosen();

private:
    QQuickView* m_view = nullptr;
    QQuickItem* m_rootItem = nullptr;
    QPointer<QScreen> m_screen;
    bool m_closing = false; // close() itself, not the window manager
};

} // namespace SnapTray
