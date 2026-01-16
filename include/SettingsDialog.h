#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include "ToolbarStyle.h"

class QSettings;
class QTabWidget;
class QCheckBox;
class QLabel;
class QPushButton;
class QLineEdit;
class QSlider;
class QComboBox;
class QListWidget;
class HotkeyEdit;

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

    // Paste hotkey settings
    static QString defaultPasteHotkey();
    static QString loadPasteHotkey();

    // Quick Pin hotkey settings
    static QString defaultQuickPinHotkey();
    static QString loadQuickPinHotkey();

    // Status display
    void showHotkeyError(const QString &message);
    void updateCaptureHotkeyStatus(bool isRegistered);
    void updateScreenCanvasHotkeyStatus(bool isRegistered);
    void updatePasteHotkeyStatus(bool isRegistered);
    void updateQuickPinHotkeyStatus(bool isRegistered);

signals:
    void hotkeyChangeRequested(const QString &newHotkey);
    void screenCanvasHotkeyChangeRequested(const QString &newHotkey);
    void pasteHotkeyChangeRequested(const QString &newHotkey);
    void quickPinHotkeyChangeRequested(const QString &newHotkey);
    void startOnLoginChanged(bool enabled);
    void toolbarStyleChanged(ToolbarStyleType style);
    void ocrLanguagesChanged(const QStringList &languages);

private slots:
    void onSave();
    void onCancel();
    void onRestoreDefaults();
    void onAccepted();
    void onAddOcrLanguage();
    void onRemoveOcrLanguage();
    void onTabChanged(int index);
    void loadOcrLanguages();

private:
    void setupUi();
    void setupGeneralTab(QWidget *tab);
    void setupHotkeysTab(QWidget *tab);
    void setupWatermarkTab(QWidget *tab);
    void setupOcrTab(QWidget *tab);
#ifdef SNAPTRAY_ENABLE_DEV_FEATURES
    void setupRecordingTab(QWidget *tab);
#endif
    void updateHotkeyStatus(QLabel *statusLabel, bool isRegistered);

    // UI elements
    QTabWidget *m_tabWidget;
    QCheckBox *m_startOnLoginCheckbox;
    QComboBox *m_toolbarStyleCombo;
    HotkeyEdit *m_hotkeyEdit;
    QLabel *m_captureHotkeyStatus;
    HotkeyEdit *m_screenCanvasHotkeyEdit;
    QLabel *m_screenCanvasHotkeyStatus;
    HotkeyEdit *m_pasteHotkeyEdit;
    QLabel *m_pasteHotkeyStatus;
    HotkeyEdit *m_quickPinHotkeyEdit;
    QLabel *m_quickPinHotkeyStatus;
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
    QCheckBox *m_watermarkApplyToRecordingCheckbox;

#ifdef SNAPTRAY_ENABLE_DEV_FEATURES
    // Recording UI elements
    QComboBox *m_recordingFrameRateCombo;
    QComboBox *m_recordingOutputFormatCombo;
    QCheckBox *m_recordingShowPreviewCheckbox;

    // Countdown settings
    QCheckBox *m_countdownEnabledCheckbox;
    QComboBox *m_countdownSecondsCombo;

    // Click highlight settings
    QCheckBox *m_clickHighlightEnabledCheckbox;

    // MP4 settings (native encoder)
    QWidget *m_mp4SettingsWidget;
    QSlider *m_recordingQualitySlider;
    QLabel *m_recordingQualityLabel;

    // GIF settings
    QWidget *m_gifSettingsWidget;
    QLabel *m_gifInfoLabel;

    // Audio settings
    QCheckBox *m_audioEnabledCheckbox;
    QComboBox *m_audioSourceCombo;
    QComboBox *m_audioDeviceCombo;
    QLabel *m_systemAudioWarningLabel;
#endif

    // Watermark UI helper methods
    void updateWatermarkImagePreview();

#ifdef SNAPTRAY_ENABLE_DEV_FEATURES
    // Recording UI helper methods
    void onOutputFormatChanged(int index);
    void populateAudioDevices();
    void onAudioSourceChanged(int index);
#endif

    // Auto-blur settings
    QSlider *m_blurIntensitySlider;
    QLabel *m_blurIntensityLabel;
    QComboBox *m_blurTypeCombo;

    // Pin Window settings (in General tab)
    QSlider *m_pinWindowOpacitySlider;
    QLabel *m_pinWindowOpacityLabel;
    QSlider *m_pinWindowOpacityStepSlider;
    QLabel *m_pinWindowOpacityStepLabel;
    QSlider *m_pinWindowZoomStepSlider;
    QLabel *m_pinWindowZoomStepLabel;

    // Files tab
    void setupFilesTab(QWidget *tab);

    // About tab
    void setupAboutTab(QWidget *tab);
    void updateFilenamePreview();
    QLineEdit *m_screenshotPathEdit;
    QPushButton *m_screenshotPathBrowseBtn;
    QLineEdit *m_recordingPathEdit;
    QPushButton *m_recordingPathBrowseBtn;
    QLineEdit *m_filenamePrefixEdit;
    QComboBox *m_dateFormatCombo;
    QLabel *m_filenamePreviewLabel;
    QCheckBox *m_autoSaveScreenshotsCheckbox;
    QCheckBox *m_autoSaveRecordingsCheckbox;

    // OCR tab UI elements
    QListWidget *m_ocrAvailableList;
    QListWidget *m_ocrSelectedList;
    QPushButton *m_ocrAddBtn;
    QPushButton *m_ocrRemoveBtn;
    QLabel *m_ocrInfoLabel;

    // OCR tab lazy loading
    QWidget *m_ocrLoadingWidget;
    QLabel *m_ocrLoadingLabel;
    QWidget *m_ocrContentWidget;
    bool m_ocrTabInitialized;
    int m_ocrTabIndex;
};

#endif // SETTINGSDIALOG_H
