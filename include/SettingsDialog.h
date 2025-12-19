#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>

class QKeySequenceEdit;
class QSettings;
class QTabWidget;
class QCheckBox;
class QLabel;
class QPushButton;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog();

    // Hotkey settings
    static QString defaultHotkey();
    static QString loadHotkey();
    static void saveHotkey(const QString &keySequence);

    static QString defaultCanvasHotkey();
    static QString loadCanvasHotkey();
    static void saveCanvasHotkey(const QString &keySequence);

    // Status display
    void showHotkeyError(const QString &message);
    void updateCaptureHotkeyStatus(bool isRegistered);
    void updateCanvasHotkeyStatus(bool isRegistered);

signals:
    void hotkeyChangeRequested(const QString &newHotkey);
    void canvasHotkeyChangeRequested(const QString &newHotkey);
    void startOnLoginChanged(bool enabled);

private slots:
    void onSave();
    void onCancel();
    void onRestoreDefaults();

private:
    void setupUi();
    void setupGeneralTab(QWidget *tab);
    void setupHotkeysTab(QWidget *tab);
    void updateHotkeyStatus(QLabel *statusLabel, bool isRegistered);

    // UI elements
    QTabWidget *m_tabWidget;
    QCheckBox *m_startOnLoginCheckbox;
    QKeySequenceEdit *m_hotkeyEdit;
    QKeySequenceEdit *m_canvasHotkeyEdit;
    QLabel *m_captureHotkeyStatus;
    QLabel *m_canvasHotkeyStatus;
    QPushButton *m_restoreDefaultsBtn;
};

#endif // SETTINGSDIALOG_H
