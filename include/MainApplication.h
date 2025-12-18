#ifndef MAINAPPLICATION_H
#define MAINAPPLICATION_H

#include <QObject>

class QSystemTrayIcon;
class QMenu;
class QAction;
class QHotkey;
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

public:
    bool updateHotkey(const QString &newHotkey);
    bool updateCanvasHotkey(const QString &newHotkey);

private:
    void setupHotkey();
    void setupCanvasHotkey();
    void updateTrayMenuHotkeyText(const QString &hotkey);
    void updateTrayMenuCanvasHotkeyText(const QString &hotkey);

    QSystemTrayIcon *m_trayIcon;
    QMenu *m_trayMenu;
    QHotkey *m_regionHotkey;
    QHotkey *m_canvasHotkey;
    CaptureManager *m_captureManager;
    PinWindowManager *m_pinWindowManager;
    ScreenCanvasManager *m_screenCanvasManager;
    QAction *m_regionCaptureAction;
    QAction *m_screenCanvasAction;
};

#endif // MAINAPPLICATION_H
