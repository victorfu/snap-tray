#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>

class QKeySequenceEdit;
class QSettings;
class QTabWidget;
class QCheckBox;
class QLabel;
class QPushButton;
class QLineEdit;
class QSlider;
class QComboBox;

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

    // Status display
    void showHotkeyError(const QString &message);
    void updateCaptureHotkeyStatus(bool isRegistered);

signals:
    void hotkeyChangeRequested(const QString &newHotkey);
    void startOnLoginChanged(bool enabled);

private slots:
    void onSave();
    void onCancel();
    void onRestoreDefaults();
    void onAccepted();

private:
    void setupUi();
    void setupGeneralTab(QWidget *tab);
    void setupHotkeysTab(QWidget *tab);
    void setupWatermarkTab(QWidget *tab);
    void updateHotkeyStatus(QLabel *statusLabel, bool isRegistered);

    // UI elements
    QTabWidget *m_tabWidget;
    QCheckBox *m_startOnLoginCheckbox;
    QKeySequenceEdit *m_hotkeyEdit;
    QLabel *m_captureHotkeyStatus;
    QPushButton *m_restoreDefaultsBtn;

    // Watermark UI elements
    QCheckBox *m_watermarkEnabledCheckbox;
    QComboBox *m_watermarkTypeCombo;
    QLabel *m_watermarkTextLabel;
    QLineEdit *m_watermarkTextEdit;
    QLabel *m_watermarkImageLabel;
    QLineEdit *m_watermarkImagePathEdit;
    QPushButton *m_watermarkBrowseBtn;
    QLabel *m_watermarkScaleRowLabel;
    QSlider *m_watermarkImageScaleSlider;
    QLabel *m_watermarkImageScaleLabel;
    QSlider *m_watermarkOpacitySlider;
    QLabel *m_watermarkOpacityLabel;
    QComboBox *m_watermarkPositionCombo;
    QLabel *m_watermarkImagePreview;
    QLabel *m_watermarkImageSizeLabel;
    QSize m_watermarkOriginalSize;

    // Watermark UI helper methods
    void updateWatermarkTypeVisibility(int type);
    void updateWatermarkImagePreview();
};

#endif // SETTINGSDIALOG_H
