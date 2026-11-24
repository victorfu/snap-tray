#pragma once

#include "ToolbarStyle.h"

#include <QMap>
#include <QTimer>
#include <QWidget>

class QLabel;
class QMenu;
class QPushButton;

// One window per capture. Closing or replacing it also retires its countdown.
class PostCaptureCard : public QWidget
{
    Q_OBJECT
public:
    enum class Action { Copy, Save, Pin, Edit, Beautify };
    PostCaptureCard(const QPixmap& pixmap, QScreen* screen);
    ~PostCaptureCard() override;
    void setReplayAvailable(bool available);
    void setBusy(Action action, bool busy);
    void setError(Action action, const QString& message);

signals:
    void actionRequested(Action action);

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void hideEvent(QHideEvent*) override;

private:
    friend class tst_FullScreenCapture;
    void updateCountdown();
    static constexpr int kLifetimeMs = 6000;
    const ToolbarStyleConfig& m_style;
    QTimer m_timer;
    int m_remainingMs = kLifetimeMs;
    bool m_hovered = false;
    bool m_dragging = false;
    bool m_keyboardFocus = false;
    bool m_menuOpen = false;
    QPoint m_dragOffset;
    QPointF m_dragGrabPosition;
    QLabel* m_status;
    QPushButton* m_copy;
    QPushButton* m_save;
    QMenu* m_menu;
    QAction* m_edit;
    QMap<Action, QString> m_errors;
};
