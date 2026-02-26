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
class QRadioButton;
class QGroupBox;
class QSpinBox;
class QDoubleSpinBox;
class QVBoxLayout;
class UpdateChecker;
struct ReleaseInfo;

namespace SnapTray {
class HotkeySettingsTab;
}

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog();

signals:
    void startOnLoginChanged(bool enabled);
    void toolbarStyleChanged(ToolbarStyleType style);
    void ocrLanguagesChanged(const QStringList &languages);


private slots:
    void onSave();
    void onCancel();
    void onAccepted();
    void onAddOcrLanguage();
    void onRemoveOcrLanguage();
    void onTabChanged(int index);
    void loadOcrLanguages();
    void onCheckForUpdates();
    void onUpdateAvailable(const ReleaseInfo& release);
    void onNoUpdateAvailable();
    void onUpdateCheckFailed(const QString& error);

private:
    void setupUi();
    void setupGeneralTab(QWidget *tab);
    void setupHotkeysTab(QWidget *tab);
    void setupAdvancedTab(QWidget *tab);
    void setupWatermarkTab(QWidget *tab);
    void setupOcrTab(QWidget *tab);
    void setupRecordingTab(QWidget *tab);
    void setupUpdatesTab(QWidget *tab);
    void updateCheckNowButtonState(bool checking);

    // UI elements
    QTabWidget *m_tabWidget;
    QCheckBox *m_startOnLoginCheckbox;
    QCheckBox *m_showShortcutHintsCheckbox;
    QCheckBox *m_scrollAutomationCheckbox;
    QComboBox *m_scrollAutoStartModeCombo;
    QCheckBox *m_scrollInlinePreviewCollapsedCheckbox;
    QGroupBox *m_scrollingAdvancedGroup;
    QDoubleSpinBox *m_scrollGoodThresholdSpin;
    QDoubleSpinBox *m_scrollPartialThresholdSpin;
    QSpinBox *m_scrollMinScrollAmountSpin;
    QSpinBox *m_scrollAutoStepDelaySpin;
    QSpinBox *m_scrollMaxFramesSpin;
    QSpinBox *m_scrollMaxOutputPixelsSpin;
    QCheckBox *m_scrollAutoIgnoreBottomEdgeCheckbox;
    QSpinBox *m_scrollSettleStableFramesSpin;
    QSpinBox *m_scrollSettleTimeoutMsSpin;
    QSpinBox *m_scrollProbeGridDensitySpin;
    QComboBox *m_languageCombo;
    QComboBox *m_toolbarStyleCombo;
    SnapTray::HotkeySettingsTab *m_hotkeySettingsTab;

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

    // Recording UI elements
    QComboBox *m_recordingFrameRateCombo;
    QComboBox *m_recordingOutputFormatCombo;
    QCheckBox *m_recordingShowPreviewCheckbox;

    // Countdown settings
    QCheckBox *m_countdownEnabledCheckbox;
    QComboBox *m_countdownSecondsCombo;

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

    // Watermark UI helper methods
    void updateWatermarkImagePreview();

    // Recording UI helper methods
    void onOutputFormatChanged(int index);
    void populateAudioDevices();
    void onAudioSourceChanged(int index);

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
    QSlider *m_pinWindowMaxCacheFilesSlider;
    QLabel *m_pinWindowMaxCacheFilesLabel;

    // Files tab
    void setupFilesTab(QWidget *tab);

    // About tab
    void setupAboutTab(QWidget *tab);
    void updateFilenamePreview();
    QLineEdit *m_screenshotPathEdit;
    QPushButton *m_screenshotPathBrowseBtn;
    QLineEdit *m_recordingPathEdit;
    QPushButton *m_recordingPathBrowseBtn;
    QLineEdit *m_filenameTemplateEdit;
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

    // OCR behavior settings
    QRadioButton *m_ocrDirectCopyRadio;
    QRadioButton *m_ocrShowEditorRadio;

    // Updates tab
    void setupUpdatesTabForDirectDownload(QVBoxLayout* layout, QWidget* tab);
    QCheckBox *m_autoCheckUpdatesCheckbox;
    QComboBox *m_checkFrequencyCombo;
    QLabel *m_lastCheckedLabel;
    QPushButton *m_checkNowButton;
    QLabel *m_currentVersionLabel;
    UpdateChecker *m_updateChecker;

    // CLI Installation
    QLabel *m_cliStatusLabel;
    QPushButton *m_cliInstallButton;
    void updateCLIStatus();
    void onCLIButtonClicked();

#ifdef Q_OS_MAC
    // Permissions (macOS only)
    QLabel *m_screenRecordingStatusLabel;
    QPushButton *m_screenRecordingSettingsBtn;
    QLabel *m_accessibilityStatusLabel;
    QPushButton *m_accessibilitySettingsBtn;
    void updatePermissionStatus();
#endif
};

#endif // SETTINGSDIALOG_H
