#ifndef MAINAPPLICATION_H
#define MAINAPPLICATION_H

#include <QObject>
#include <QElapsedTimer>

class QSystemTrayIcon;
class QMenu;
class QAction;
class QHotkey;
class QTimer;
class CaptureManager;
class PinWindowManager;
class ScreenCanvasManager;

class MainApplication : public QObject
{
    Q_OBJECT

public:
    explicit MainApplication(QObject *parent = nullptr);
    ~MainApplication();

    void initialize();

private slots:
    void onRegionCapture();
    void onScreenCanvas();
    void onCloseAllPins();
    void onSettings();
    void onF2Pressed();
    void onDoublePressTimeout();

public:
    bool updateHotkey(const QString &newHotkey);

private:
    void setupHotkey();
    void updateTrayMenuHotkeyText(const QString &hotkey);

    QSystemTrayIcon *m_trayIcon;
    QMenu *m_trayMenu;
    QHotkey *m_regionHotkey;
    QTimer *m_doublePressTimer;
    QElapsedTimer m_lastF2PressTime;
    bool m_waitingForSecondPress;
    CaptureManager *m_captureManager;
    PinWindowManager *m_pinWindowManager;
    ScreenCanvasManager *m_screenCanvasManager;
    QAction *m_regionCaptureAction;
    QAction *m_screenCanvasAction;
};

#endif // MAINAPPLICATION_H
