#ifndef PINWINDOWMANAGER_H
#define PINWINDOWMANAGER_H

#include <QObject>
#include <QList>
#include <QPixmap>
#include <QPoint>

class PinWindow;
class QHotkey;

class PinWindowManager : public QObject
{
    Q_OBJECT

public:
    explicit PinWindowManager(QObject *parent = nullptr);
    ~PinWindowManager();

    PinWindow* createPinWindow(const QPixmap &screenshot, const QPoint &position);
    void closeAllWindows();
    void disableClickThroughAll();
    int windowCount() const { return m_windows.count(); }

    // Called by PinWindow when click-through state changes
    void onClickThroughChanged();

signals:
    void windowCreated(PinWindow *window);
    void windowClosed(PinWindow *window);
    void allWindowsClosed();
    void ocrCompleted(bool success, const QString &message);

private slots:
    void onWindowClosed(PinWindow *window);

private:
    void updateClickThroughHotkey();

    QList<PinWindow*> m_windows;
    QHotkey *m_clickThroughHotkey = nullptr;
};

#endif // PINWINDOWMANAGER_H
