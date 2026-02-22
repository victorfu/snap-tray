#ifndef PINWINDOWMANAGER_H
#define PINWINDOWMANAGER_H

#include <QObject>
#include <QList>
#include <QPixmap>
#include <QPoint>

class PinWindow;

class PinWindowManager : public QObject
{
    Q_OBJECT

public:
    explicit PinWindowManager(QObject *parent = nullptr);
    ~PinWindowManager();

    PinWindow* createPinWindow(const QPixmap &screenshot,
                               const QPoint &position,
                               bool autoSaveToCache = true);
    void closeAllWindows();
    void closeWindows(const QList<PinWindow*>& windows);
    void setAllPinsVisible(bool visible);
    void toggleAllPinsVisibility();
    QList<PinWindow*> windows() const { return m_windows; }
    int windowCount() const { return m_windows.count(); }
    bool arePinsHidden() const { return m_pinsHidden; }
    void updateOcrLanguages(const QStringList &languages);

signals:
    void windowCreated(PinWindow *window);
    void windowClosed(PinWindow *window);
    void allWindowsClosed();
    void allPinsVisibilityChanged(bool hidden);
    void ocrCompleted(bool success, const QString &message);
    void saveCompleted(const QPixmap &pixmap, const QString &filePath);
    void saveFailed(const QString &filePath, const QString &error);

private slots:
    void onWindowClosed(PinWindow *window);

private:
    QList<PinWindow*> m_windows;
    bool m_pinsHidden = false;
};

#endif // PINWINDOWMANAGER_H
