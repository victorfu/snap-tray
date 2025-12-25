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

    // Screen Canvas hotkey settings
    static QString defaultScreenCanvasHotkey();
    static QString loadScreenCanvasHotkey();

    // Status display
    void showHotkeyError(const QString &message);
    void updateCaptureHotkeyStatus(bool isRegistered);
    void updateScreenCanvasHotkeyStatus(bool isRegistered);

signals:
    void hotkeyChangeRequested(const QString &newHotkey);
    void screenCanvasHotkeyChangeRequested(const QString &newHotkey);
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
    void setupRecordingTab(QWidget *tab);
    void updateHotkeyStatus(QLabel *statusLabel, bool isRegistered);

    // UI elements
    QTabWidget *m_tabWidget;
    QCheckBox *m_startOnLoginCheckbox;
    QKeySequenceEdit *m_hotkeyEdit;
    QLabel *m_captureHotkeyStatus;
    QKeySequenceEdit *m_screenCanvasHotkeyEdit;
    QLabel *m_screenCanvasHotkeyStatus;
    QPushButton *m_restoreDefaultsBtn;

    // Watermark UI elements
    QCheckBox *m_watermarkEnabledCheckbox;
    QLineEdit *m_watermarkImagePathEdit;
    QPushButton *m_watermarkBrowseBtn;
    QSlider *m_watermarkImageScaleSlider;
    QLabel *m_watermarkImageScaleLabel;
    QSlider *m_watermarkOpacitySlider;
    QLabel *m_watermarkOpacityLabel;
    QSlider *m_watermarkMarginSlider;
    QLabel *m_watermarkMarginLabel;
    QComboBox *m_watermarkPositionCombo;
    QLabel *m_watermarkImagePreview;
    QLabel *m_watermarkImageSizeLabel;
    QSize m_watermarkOriginalSize;

    // Recording UI elements
    QComboBox *m_recordingFrameRateCombo;
    QComboBox *m_recordingOutputFormatCombo;
    QCheckBox *m_recordingAutoSaveCheckbox;

    // MP4 settings (native encoder)
    QWidget *m_mp4SettingsWidget;
    QSlider *m_recordingQualitySlider;
    QLabel *m_recordingQualityLabel;

    // GIF settings (FFmpeg)
    QWidget *m_gifSettingsWidget;
    QLabel *m_gifWarningLabel;
    QLineEdit *m_ffmpegPathEdit;
    QPushButton *m_ffmpegBrowseBtn;
    QLabel *m_ffmpegStatusLabel;

    // Watermark UI helper methods
    void updateWatermarkImagePreview();

    // Recording UI helper methods
    void updateFFmpegStatus();
    void onOutputFormatChanged(int index);
};

#endif // SETTINGSDIALOG_H
