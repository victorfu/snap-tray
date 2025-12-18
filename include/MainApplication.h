#ifndef MAINAPPLICATION_H
#define MAINAPPLICATION_H

#include <QObject>

class QSystemTrayIcon;
class QMenu;
class QAction;
class QHotkey;
class CaptureManager;
class PinWindowManager;

class MainApplication : public QObject
{
    Q_OBJECT

public:
    explicit MainApplication(QObject *parent = nullptr);
    ~MainApplication();

    void initialize();

private slots:
    void onRegionCapture();
    void onCloseAllPins();
    void onSettings();

public:
    bool updateHotkey(const QString &newHotkey);

private:
    void setupHotkey();
    void updateTrayMenuHotkeyText(const QString &hotkey);

    QSystemTrayIcon *m_trayIcon;
    QMenu *m_trayMenu;
    QHotkey *m_regionHotkey;
    CaptureManager *m_captureManager;
    PinWindowManager *m_pinWindowManager;
    QAction *m_regionCaptureAction;
};

#endif // MAINAPPLICATION_H
