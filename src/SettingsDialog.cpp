#include "SettingsDialog.h"
#include "version.h"
#include "AutoLaunchManager.h"
#include "PlatformFeatures.h"
#include "WatermarkRenderer.h"
#include "OCRManager.h"
#include "capture/IAudioCaptureEngine.h"
#include "settings/LanguageManager.h"
#include "settings/Settings.h"
#include "settings/AutoBlurSettingsManager.h"
#include "settings/FileSettingsManager.h"
#include "settings/PinWindowSettingsManager.h"
#include "settings/OCRSettingsManager.h"
#include "settings/SettingsTheme.h"
#include "utils/FilenameTemplateEngine.h"
#include "detection/AutoBlurManager.h"
#include "widgets/HotkeySettingsTab.h"
#include "update/UpdateChecker.h"
#include "update/UpdateDialog.h"
#include "update/UpdateSettingsManager.h"
#include <QDir>
#include <QSettings>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDebug>
#include <QMessageBox>
#include <QTabWidget>
#include <QCheckBox>
#include <QLineEdit>
#include <QSlider>
#include <QComboBox>
#include <QListWidget>
#include <QRadioButton>
#include <QGroupBox>
#include <QFileDialog>
#include <QFrame>
#include <QScrollArea>
#include <QStandardPaths>
#include <QFile>
#include <QTimer>
#include <memory>

// Hotkey constants are defined in settings/Settings.h

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
    , m_tabWidget(nullptr)
    , m_startOnLoginCheckbox(nullptr)
    , m_languageCombo(nullptr)
    , m_toolbarStyleCombo(nullptr)
    , m_hotkeySettingsTab(nullptr)
    , m_watermarkEnabledCheckbox(nullptr)
    , m_watermarkImagePathEdit(nullptr)
    , m_watermarkBrowseBtn(nullptr)
    , m_watermarkImageScaleSlider(nullptr)
    , m_watermarkImageScaleLabel(nullptr)
    , m_watermarkOpacitySlider(nullptr)
    , m_watermarkOpacityLabel(nullptr)
    , m_watermarkPositionCombo(nullptr)
    , m_recordingFrameRateCombo(nullptr)
    , m_recordingOutputFormatCombo(nullptr)
    , m_mp4SettingsWidget(nullptr)
    , m_recordingQualitySlider(nullptr)
    , m_recordingQualityLabel(nullptr)
    , m_gifSettingsWidget(nullptr)
    , m_gifInfoLabel(nullptr)
    , m_pinWindowOpacitySlider(nullptr)
    , m_pinWindowOpacityLabel(nullptr)
    , m_pinWindowOpacityStepSlider(nullptr)
    , m_pinWindowOpacityStepLabel(nullptr)
    , m_pinWindowZoomStepSlider(nullptr)
    , m_pinWindowZoomStepLabel(nullptr)
    , m_screenshotPathEdit(nullptr)
    , m_screenshotPathBrowseBtn(nullptr)
    , m_recordingPathEdit(nullptr)
    , m_recordingPathBrowseBtn(nullptr)
    , m_filenameTemplateEdit(nullptr)
    , m_filenamePreviewLabel(nullptr)
    , m_ocrAvailableList(nullptr)
    , m_ocrSelectedList(nullptr)
    , m_ocrAddBtn(nullptr)
    , m_ocrRemoveBtn(nullptr)
    , m_ocrInfoLabel(nullptr)
    , m_ocrLoadingWidget(nullptr)
    , m_ocrLoadingLabel(nullptr)
    , m_ocrContentWidget(nullptr)
    , m_ocrTabInitialized(false)
    , m_ocrTabIndex(-1)
    , m_ocrDirectCopyRadio(nullptr)
    , m_ocrShowEditorRadio(nullptr)
    , m_autoCheckUpdatesCheckbox(nullptr)
    , m_checkFrequencyCombo(nullptr)
    , m_lastCheckedLabel(nullptr)
    , m_checkNowButton(nullptr)
    , m_currentVersionLabel(nullptr)
    , m_updateChecker(nullptr)
{
    setWindowTitle(tr("%1 Settings").arg(SNAPTRAY_APP_NAME));
    setMinimumSize(520, 480);
    setupUi();

    connect(this, &QDialog::accepted, this, &SettingsDialog::onAccepted);
}

SettingsDialog::~SettingsDialog()
{
}

void SettingsDialog::setupUi()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Create tab widget
    m_tabWidget = new QTabWidget(this);

    // Tab 1: General
    QWidget* generalTab = new QWidget();
    setupGeneralTab(generalTab);
    m_tabWidget->addTab(generalTab, tr("General"));

    // Tab 2: Hotkeys
    QWidget* hotkeysTab = new QWidget();
    setupHotkeysTab(hotkeysTab);
    m_tabWidget->addTab(hotkeysTab, tr("Hotkeys"));

    // Tab 3: Advanced
    QWidget* advancedTab = new QWidget();
    setupAdvancedTab(advancedTab);
    m_tabWidget->addTab(advancedTab, tr("Advanced"));

    // Tab 4: Watermark
    QWidget* watermarkTab = new QWidget();
    setupWatermarkTab(watermarkTab);
    m_tabWidget->addTab(watermarkTab, tr("Watermark"));

    // Tab 4: OCR (lazy loaded when tab is selected)
    QWidget* ocrTab = new QWidget();
    setupOcrTab(ocrTab);
    m_ocrTabIndex = m_tabWidget->addTab(ocrTab, tr("OCR"));

    // Tab 5: Recording
    QWidget* recordingTab = new QWidget();
    setupRecordingTab(recordingTab);
    m_tabWidget->addTab(recordingTab, tr("Recording"));

    // Tab 6: Files
    QWidget* filesTab = new QWidget();
    setupFilesTab(filesTab);
    m_tabWidget->addTab(filesTab, tr("Files"));

    // Tab 7: Updates
    QWidget* updatesTab = new QWidget();
    setupUpdatesTab(updatesTab);
    m_tabWidget->addTab(updatesTab, tr("Updates"));

    // Tab 8: About
    QWidget* aboutTab = new QWidget();
    setupAboutTab(aboutTab);
    m_tabWidget->addTab(aboutTab, tr("About"));

    mainLayout->addWidget(m_tabWidget);

    // Connect tab change for lazy loading
    connect(m_tabWidget, &QTabWidget::currentChanged, this, &SettingsDialog::onTabChanged);

    // Buttons row (outside tabs)
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    QPushButton* saveButton = new QPushButton(tr("Save"), this);
    QPushButton* cancelButton = new QPushButton(tr("Cancel"), this);

    connect(saveButton, &QPushButton::clicked, this, &SettingsDialog::onSave);
    connect(cancelButton, &QPushButton::clicked, this, &SettingsDialog::onCancel);

    buttonLayout->addWidget(saveButton);
    buttonLayout->addWidget(cancelButton);
    mainLayout->addLayout(buttonLayout);
}

void SettingsDialog::setupGeneralTab(QWidget* tab)
{
    QVBoxLayout* tabLayout = new QVBoxLayout(tab);
    tabLayout->setContentsMargins(0, 0, 0, 0);

    // Create scroll area for content
    QScrollArea* scrollArea = new QScrollArea(tab);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    // Content widget inside scroll area
    QWidget* contentWidget = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(contentWidget);

    m_startOnLoginCheckbox = new QCheckBox(tr("Start on login"), contentWidget);
    m_startOnLoginCheckbox->setChecked(AutoLaunchManager::isEnabled());
    layout->addWidget(m_startOnLoginCheckbox);

    // ========== Language Section ==========
    layout->addSpacing(16);
    QLabel* languageSectionLabel = new QLabel(tr("Language"), contentWidget);
    languageSectionLabel->setStyleSheet("font-weight: bold; font-size: 12px;");
    layout->addWidget(languageSectionLabel);

    QHBoxLayout* languageLayout = new QHBoxLayout();
    QLabel* languageLabel = new QLabel(tr("Display language:"), contentWidget);
    languageLabel->setFixedWidth(120);
    m_languageCombo = new QComboBox(contentWidget);

    const auto languages = LanguageManager::instance().availableLanguages();
    const QString currentLang = LanguageManager::instance().loadLanguage();
    for (const auto& lang : languages) {
        m_languageCombo->addItem(lang.nativeName, lang.code);
    }

    int langIndex = m_languageCombo->findData(currentLang);
    if (langIndex >= 0) {
        m_languageCombo->setCurrentIndex(langIndex);
    }

    languageLayout->addWidget(languageLabel);
    languageLayout->addWidget(m_languageCombo);
    languageLayout->addStretch();
    layout->addLayout(languageLayout);

    // ========== Appearance Section ==========
    layout->addSpacing(16);
    QLabel* appearanceLabel = new QLabel(tr("Appearance"), contentWidget);
    appearanceLabel->setStyleSheet("font-weight: bold; font-size: 12px;");
    layout->addWidget(appearanceLabel);

    QHBoxLayout* styleLayout = new QHBoxLayout();
    QLabel* styleLabel = new QLabel(tr("Toolbar Style:"), contentWidget);
    styleLabel->setFixedWidth(120);
    m_toolbarStyleCombo = new QComboBox(contentWidget);
    m_toolbarStyleCombo->addItem(tr("Dark"), static_cast<int>(ToolbarStyleType::Dark));
    m_toolbarStyleCombo->addItem(tr("Light"), static_cast<int>(ToolbarStyleType::Light));
    styleLayout->addWidget(styleLabel);
    styleLayout->addWidget(m_toolbarStyleCombo);
    styleLayout->addStretch();
    layout->addLayout(styleLayout);

    // Load current setting
    int currentStyle = static_cast<int>(ToolbarStyleConfig::loadStyle());
    m_toolbarStyleCombo->setCurrentIndex(currentStyle);

#ifdef Q_OS_MAC
    // ========== Permissions Section (macOS only) ==========
    layout->addSpacing(16);
    QLabel* permissionsLabel = new QLabel(tr("Permissions"), contentWidget);
    permissionsLabel->setStyleSheet("font-weight: bold; font-size: 12px;");
    layout->addWidget(permissionsLabel);

    // Screen Recording row
    QHBoxLayout* screenRecordingLayout = new QHBoxLayout();
    QLabel* screenRecordingLabel = new QLabel(tr("Screen Recording:"), contentWidget);
    screenRecordingLabel->setFixedWidth(120);
    m_screenRecordingStatusLabel = new QLabel(contentWidget);
    m_screenRecordingSettingsBtn = new QPushButton(tr("Open Settings"), contentWidget);
    m_screenRecordingSettingsBtn->setDefault(false);
    m_screenRecordingSettingsBtn->setAutoDefault(false);
    connect(m_screenRecordingSettingsBtn, &QPushButton::clicked, this, []() {
        PlatformFeatures::openScreenRecordingSettings();
        });
    screenRecordingLayout->addWidget(screenRecordingLabel);
    screenRecordingLayout->addWidget(m_screenRecordingStatusLabel);
    screenRecordingLayout->addStretch();
    screenRecordingLayout->addWidget(m_screenRecordingSettingsBtn);
    layout->addLayout(screenRecordingLayout);

    // Accessibility row
    QHBoxLayout* accessibilityLayout = new QHBoxLayout();
    QLabel* accessibilityLabel = new QLabel(tr("Accessibility:"), contentWidget);
    accessibilityLabel->setFixedWidth(120);
    m_accessibilityStatusLabel = new QLabel(contentWidget);
    m_accessibilitySettingsBtn = new QPushButton(tr("Open Settings"), contentWidget);
    m_accessibilitySettingsBtn->setDefault(false);
    m_accessibilitySettingsBtn->setAutoDefault(false);
    connect(m_accessibilitySettingsBtn, &QPushButton::clicked, this, []() {
        PlatformFeatures::openAccessibilitySettings();
        });
    accessibilityLayout->addWidget(accessibilityLabel);
    accessibilityLayout->addWidget(m_accessibilityStatusLabel);
    accessibilityLayout->addStretch();
    accessibilityLayout->addWidget(m_accessibilitySettingsBtn);
    layout->addLayout(accessibilityLayout);

    // Update permission status
    updatePermissionStatus();
#endif

    // ========== CLI Installation Section ==========
    layout->addSpacing(16);
    QLabel* cliLabel = new QLabel(tr("Command Line Interface"), contentWidget);
    cliLabel->setStyleSheet("font-weight: bold; font-size: 12px;");
    layout->addWidget(cliLabel);

    QHBoxLayout* cliLayout = new QHBoxLayout();
    m_cliStatusLabel = new QLabel(contentWidget);
    m_cliInstallButton = new QPushButton(contentWidget);
    m_cliInstallButton->setDefault(false);
    m_cliInstallButton->setAutoDefault(false);

    updateCLIStatus();

    connect(m_cliInstallButton, &QPushButton::clicked, this, &SettingsDialog::onCLIButtonClicked);

    cliLayout->addWidget(m_cliStatusLabel);
    cliLayout->addStretch();
    cliLayout->addWidget(m_cliInstallButton);
    layout->addLayout(cliLayout);

    layout->addStretch();

    scrollArea->setWidget(contentWidget);
    tabLayout->addWidget(scrollArea);
}

void SettingsDialog::setupHotkeysTab(QWidget* tab)
{
    QVBoxLayout* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(0, 0, 0, 0);

    m_hotkeySettingsTab = new SnapTray::HotkeySettingsTab(tab);
    layout->addWidget(m_hotkeySettingsTab);
}

void SettingsDialog::setupAdvancedTab(QWidget* tab)
{
    QVBoxLayout* layout = new QVBoxLayout(tab);

    // ========== Blur Section ==========
    QLabel* autoBlurLabel = new QLabel(tr("Blur"), tab);
    autoBlurLabel->setStyleSheet("font-weight: bold; font-size: 12px;");
    layout->addWidget(autoBlurLabel);

    // Load current settings
    auto blurOptions = AutoBlurSettingsManager::instance().load();

    // Blur intensity slider
    QHBoxLayout* intensityLayout = new QHBoxLayout();
    QLabel* intensityLabel = new QLabel(tr("Blur intensity:"), tab);
    intensityLabel->setFixedWidth(120);
    m_blurIntensitySlider = new QSlider(Qt::Horizontal, tab);
    m_blurIntensitySlider->setRange(1, 100);
    m_blurIntensitySlider->setValue(blurOptions.blurIntensity);
    m_blurIntensityLabel = new QLabel(QString::number(blurOptions.blurIntensity), tab);
    m_blurIntensityLabel->setFixedWidth(30);
    connect(m_blurIntensitySlider, &QSlider::valueChanged, this, [this](int value) {
        m_blurIntensityLabel->setText(QString::number(value));
    });
    intensityLayout->addWidget(intensityLabel);
    intensityLayout->addWidget(m_blurIntensitySlider);
    intensityLayout->addWidget(m_blurIntensityLabel);
    layout->addLayout(intensityLayout);

    // Blur type combo
    QHBoxLayout* typeLayout = new QHBoxLayout();
    QLabel* typeLabel = new QLabel(tr("Blur type:"), tab);
    typeLabel->setFixedWidth(120);
    m_blurTypeCombo = new QComboBox(tab);
    m_blurTypeCombo->addItem(tr("Pixelate"), "pixelate");
    m_blurTypeCombo->addItem(tr("Gaussian"), "gaussian");
    m_blurTypeCombo->setCurrentIndex(blurOptions.blurType == AutoBlurManager::BlurType::Gaussian ? 1 : 0);
    typeLayout->addWidget(typeLabel);
    typeLayout->addWidget(m_blurTypeCombo);
    typeLayout->addStretch();
    layout->addLayout(typeLayout);

    // ========== Pin Window Section ==========
    layout->addSpacing(16);
    QLabel* pinWindowLabel = new QLabel(tr("Pin Window"), tab);
    pinWindowLabel->setStyleSheet("font-weight: bold; font-size: 12px;");
    layout->addWidget(pinWindowLabel);

    auto& pinSettings = PinWindowSettingsManager::instance();

    // Default opacity slider
    QHBoxLayout* opacityLayout = new QHBoxLayout();
    QLabel* opacityLabel = new QLabel(tr("Default opacity:"), tab);
    opacityLabel->setFixedWidth(120);
    m_pinWindowOpacitySlider = new QSlider(Qt::Horizontal, tab);
    m_pinWindowOpacitySlider->setRange(10, 100);
    int currentOpacity = static_cast<int>(pinSettings.loadDefaultOpacity() * 100);
    m_pinWindowOpacitySlider->setValue(currentOpacity);
    m_pinWindowOpacityLabel = new QLabel(QString("%1%").arg(currentOpacity), tab);
    m_pinWindowOpacityLabel->setFixedWidth(40);
    connect(m_pinWindowOpacitySlider, &QSlider::valueChanged, this, [this](int value) {
        m_pinWindowOpacityLabel->setText(QString("%1%").arg(value));
    });
    opacityLayout->addWidget(opacityLabel);
    opacityLayout->addWidget(m_pinWindowOpacitySlider);
    opacityLayout->addWidget(m_pinWindowOpacityLabel);
    layout->addLayout(opacityLayout);

    // Opacity step slider
    QHBoxLayout* opacityStepLayout = new QHBoxLayout();
    QLabel* opacityStepLabel = new QLabel(tr("Opacity step:"), tab);
    opacityStepLabel->setFixedWidth(120);
    m_pinWindowOpacityStepSlider = new QSlider(Qt::Horizontal, tab);
    m_pinWindowOpacityStepSlider->setRange(1, 20);
    int currentOpacityStep = static_cast<int>(pinSettings.loadOpacityStep() * 100);
    m_pinWindowOpacityStepSlider->setValue(currentOpacityStep);
    m_pinWindowOpacityStepLabel = new QLabel(QString("%1%").arg(currentOpacityStep), tab);
    m_pinWindowOpacityStepLabel->setFixedWidth(40);
    connect(m_pinWindowOpacityStepSlider, &QSlider::valueChanged, this, [this](int value) {
        m_pinWindowOpacityStepLabel->setText(QString("%1%").arg(value));
    });
    opacityStepLayout->addWidget(opacityStepLabel);
    opacityStepLayout->addWidget(m_pinWindowOpacityStepSlider);
    opacityStepLayout->addWidget(m_pinWindowOpacityStepLabel);
    layout->addLayout(opacityStepLayout);

    // Zoom step slider
    QHBoxLayout* zoomStepLayout = new QHBoxLayout();
    QLabel* zoomStepLabel = new QLabel(tr("Zoom step:"), tab);
    zoomStepLabel->setFixedWidth(120);
    m_pinWindowZoomStepSlider = new QSlider(Qt::Horizontal, tab);
    m_pinWindowZoomStepSlider->setRange(1, 20);
    int currentZoomStep = static_cast<int>(pinSettings.loadZoomStep() * 100);
    m_pinWindowZoomStepSlider->setValue(currentZoomStep);
    m_pinWindowZoomStepLabel = new QLabel(QString("%1%").arg(currentZoomStep), tab);
    m_pinWindowZoomStepLabel->setFixedWidth(40);
    connect(m_pinWindowZoomStepSlider, &QSlider::valueChanged, this, [this](int value) {
        m_pinWindowZoomStepLabel->setText(QString("%1%").arg(value));
    });
    zoomStepLayout->addWidget(zoomStepLabel);
    zoomStepLayout->addWidget(m_pinWindowZoomStepSlider);
    zoomStepLayout->addWidget(m_pinWindowZoomStepLabel);
    layout->addLayout(zoomStepLayout);

    // Max cache files slider
    QHBoxLayout* cacheFilesLayout = new QHBoxLayout();
    QLabel* cacheFilesLabel = new QLabel(tr("Max cache files:"), tab);
    cacheFilesLabel->setFixedWidth(120);
    m_pinWindowMaxCacheFilesSlider = new QSlider(Qt::Horizontal, tab);
    m_pinWindowMaxCacheFilesSlider->setRange(5, 200);
    int currentMaxCacheFiles = pinSettings.loadMaxCacheFiles();
    m_pinWindowMaxCacheFilesSlider->setValue(currentMaxCacheFiles);
    m_pinWindowMaxCacheFilesLabel = new QLabel(QString::number(currentMaxCacheFiles), tab);
    m_pinWindowMaxCacheFilesLabel->setFixedWidth(40);
    connect(m_pinWindowMaxCacheFilesSlider, &QSlider::valueChanged, this, [this](int value) {
        m_pinWindowMaxCacheFilesLabel->setText(QString::number(value));
    });
    cacheFilesLayout->addWidget(cacheFilesLabel);
    cacheFilesLayout->addWidget(m_pinWindowMaxCacheFilesSlider);
    cacheFilesLayout->addWidget(m_pinWindowMaxCacheFilesLabel);
    layout->addLayout(cacheFilesLayout);

    layout->addStretch();
}

void SettingsDialog::setupWatermarkTab(QWidget* tab)
{
    QVBoxLayout* mainLayout = new QVBoxLayout(tab);
    mainLayout->setContentsMargins(9, 12, 9, 9);  // Increase top margin for checkbox

    // Apply to images checkbox (for screenshots via PinWindow context menu)
    m_watermarkEnabledCheckbox = new QCheckBox(tr("Apply to images"), tab);
    mainLayout->addWidget(m_watermarkEnabledCheckbox);

    // Apply to recordings checkbox
    m_watermarkApplyToRecordingCheckbox = new QCheckBox(tr("Apply to recordings"), tab);
    mainLayout->addWidget(m_watermarkApplyToRecordingCheckbox);

    mainLayout->addSpacing(8);

    // Horizontal layout: left controls, right preview
    QHBoxLayout* contentLayout = new QHBoxLayout();

    // === LEFT SIDE: Controls ===
    QVBoxLayout* controlsLayout = new QVBoxLayout();
    controlsLayout->setSpacing(8);

    // Image path row
    QHBoxLayout* imageLayout = new QHBoxLayout();
    QLabel* imageLabel = new QLabel(tr("Image:"), tab);
    imageLabel->setFixedWidth(60);
    m_watermarkImagePathEdit = new QLineEdit(tab);
    m_watermarkImagePathEdit->setPlaceholderText(tr("Select an image file..."));
    m_watermarkImagePathEdit->setReadOnly(true);
    m_watermarkBrowseBtn = new QPushButton(tr("Browse..."), tab);
    m_watermarkBrowseBtn->setFixedWidth(80);
    connect(m_watermarkBrowseBtn, &QPushButton::clicked, this, [this]() {
        QString filePath = QFileDialog::getOpenFileName(this, tr("Select Watermark Image"),
            QString(), tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.svg);;All Files (*)"));
        if (!filePath.isEmpty()) {
            m_watermarkImagePathEdit->setText(filePath);
            updateWatermarkImagePreview();
        }
        });
    imageLayout->addWidget(imageLabel);
    imageLayout->addWidget(m_watermarkImagePathEdit);
    imageLayout->addWidget(m_watermarkBrowseBtn);
    controlsLayout->addLayout(imageLayout);

    // Image scale row
    QHBoxLayout* scaleLayout = new QHBoxLayout();
    QLabel* scaleLabel = new QLabel(tr("Scale:"), tab);
    scaleLabel->setFixedWidth(60);
    m_watermarkImageScaleSlider = new QSlider(Qt::Horizontal, tab);
    m_watermarkImageScaleSlider->setRange(10, 200);
    m_watermarkImageScaleSlider->setValue(100);
    m_watermarkImageScaleLabel = new QLabel("100%", tab);
    m_watermarkImageScaleLabel->setFixedWidth(40);
    connect(m_watermarkImageScaleSlider, &QSlider::valueChanged, this, [this](int value) {
        m_watermarkImageScaleLabel->setText(QString("%1%").arg(value));
        updateWatermarkImagePreview();
        });
    scaleLayout->addWidget(scaleLabel);
    scaleLayout->addWidget(m_watermarkImageScaleSlider);
    scaleLayout->addWidget(m_watermarkImageScaleLabel);
    controlsLayout->addLayout(scaleLayout);

    // Opacity row
    QHBoxLayout* opacityLayout = new QHBoxLayout();
    QLabel* opacityLabel = new QLabel(tr("Opacity:"), tab);
    opacityLabel->setFixedWidth(60);
    m_watermarkOpacitySlider = new QSlider(Qt::Horizontal, tab);
    m_watermarkOpacitySlider->setRange(10, 100);
    m_watermarkOpacitySlider->setValue(50);
    m_watermarkOpacityLabel = new QLabel("50%", tab);
    m_watermarkOpacityLabel->setFixedWidth(40);
    connect(m_watermarkOpacitySlider, &QSlider::valueChanged, this, [this](int value) {
        m_watermarkOpacityLabel->setText(QString("%1%").arg(value));
        });
    opacityLayout->addWidget(opacityLabel);
    opacityLayout->addWidget(m_watermarkOpacitySlider);
    opacityLayout->addWidget(m_watermarkOpacityLabel);
    controlsLayout->addLayout(opacityLayout);

    // Margin row
    QHBoxLayout* marginLayout = new QHBoxLayout();
    QLabel* marginLabel = new QLabel(tr("Margin:"), tab);
    marginLabel->setFixedWidth(60);
    m_watermarkMarginSlider = new QSlider(Qt::Horizontal, tab);
    m_watermarkMarginSlider->setRange(0, 100);
    m_watermarkMarginSlider->setValue(12);
    m_watermarkMarginLabel = new QLabel("12 px", tab);
    m_watermarkMarginLabel->setFixedWidth(40);
    connect(m_watermarkMarginSlider, &QSlider::valueChanged, this, [this](int value) {
        m_watermarkMarginLabel->setText(QString("%1 px").arg(value));
        });
    marginLayout->addWidget(marginLabel);
    marginLayout->addWidget(m_watermarkMarginSlider);
    marginLayout->addWidget(m_watermarkMarginLabel);
    controlsLayout->addLayout(marginLayout);

    // Position row
    QHBoxLayout* positionLayout = new QHBoxLayout();
    QLabel* positionLabel = new QLabel(tr("Position:"), tab);
    positionLabel->setFixedWidth(60);
    m_watermarkPositionCombo = new QComboBox(tab);
    m_watermarkPositionCombo->addItem(tr("Top-Left"), static_cast<int>(WatermarkRenderer::TopLeft));
    m_watermarkPositionCombo->addItem(tr("Top-Right"), static_cast<int>(WatermarkRenderer::TopRight));
    m_watermarkPositionCombo->addItem(tr("Bottom-Left"), static_cast<int>(WatermarkRenderer::BottomLeft));
    m_watermarkPositionCombo->addItem(tr("Bottom-Right"), static_cast<int>(WatermarkRenderer::BottomRight));
    m_watermarkPositionCombo->setCurrentIndex(3); // Default to Bottom-Right
    positionLayout->addWidget(positionLabel);
    positionLayout->addWidget(m_watermarkPositionCombo);
    positionLayout->addStretch();
    controlsLayout->addLayout(positionLayout);

    controlsLayout->addStretch();
    contentLayout->addLayout(controlsLayout, 1);

    // === RIGHT SIDE: Preview ===
    QVBoxLayout* previewLayout = new QVBoxLayout();
    previewLayout->setAlignment(Qt::AlignTop);

    m_watermarkImagePreview = new QLabel(tab);
    m_watermarkImagePreview->setFixedSize(120, 120);
    m_watermarkImagePreview->setAlignment(Qt::AlignCenter);
    m_watermarkImagePreview->setStyleSheet(
        QStringLiteral("QLabel { border: 1px solid %1; background-color: %2; border-radius: 4px; }")
        .arg(SnapTray::SettingsTheme::borderColor(), SnapTray::SettingsTheme::panelBackground()));
    m_watermarkImagePreview->setText(tr("No image"));

    m_watermarkImageSizeLabel = new QLabel(tab);
    m_watermarkImageSizeLabel->setAlignment(Qt::AlignCenter);
    m_watermarkImageSizeLabel->setFixedWidth(120);

    previewLayout->addWidget(m_watermarkImagePreview);
    previewLayout->addWidget(m_watermarkImageSizeLabel);
    previewLayout->addStretch();

    contentLayout->addLayout(previewLayout);

    mainLayout->addLayout(contentLayout);
    mainLayout->addStretch();

    // Load current settings
    WatermarkRenderer::Settings settings = WatermarkSettingsManager::instance().load();
    m_watermarkEnabledCheckbox->setChecked(settings.enabled);
    m_watermarkApplyToRecordingCheckbox->setChecked(settings.applyToRecording);
    m_watermarkImagePathEdit->setText(settings.imagePath);
    m_watermarkImageScaleSlider->setValue(settings.imageScale);
    m_watermarkImageScaleLabel->setText(QString("%1%").arg(settings.imageScale));
    m_watermarkOpacitySlider->setValue(static_cast<int>(settings.opacity * 100));
    m_watermarkOpacityLabel->setText(QString("%1%").arg(static_cast<int>(settings.opacity * 100)));
    m_watermarkMarginSlider->setValue(settings.margin);
    m_watermarkMarginLabel->setText(QString("%1 px").arg(settings.margin));
    int posIndex = m_watermarkPositionCombo->findData(static_cast<int>(settings.position));
    if (posIndex >= 0) {
        m_watermarkPositionCombo->setCurrentIndex(posIndex);
    }

    // Update image preview if image path exists
    updateWatermarkImagePreview();
}

void SettingsDialog::setupRecordingTab(QWidget* tab)
{
    QVBoxLayout* layout = new QVBoxLayout(tab);

    // Frame Rate row
    QHBoxLayout* fpsLayout = new QHBoxLayout();
    QLabel* fpsLabel = new QLabel(tr("Frame Rate:"), tab);
    fpsLabel->setFixedWidth(120);
    m_recordingFrameRateCombo = new QComboBox(tab);
    m_recordingFrameRateCombo->addItem(tr("10 FPS"), 10);
    m_recordingFrameRateCombo->addItem(tr("15 FPS"), 15);
    m_recordingFrameRateCombo->addItem(tr("24 FPS"), 24);
    m_recordingFrameRateCombo->addItem(tr("30 FPS"), 30);
    m_recordingFrameRateCombo->setCurrentIndex(3);  // Default 30 FPS
    fpsLayout->addWidget(fpsLabel);
    fpsLayout->addWidget(m_recordingFrameRateCombo);
    fpsLayout->addStretch();
    layout->addLayout(fpsLayout);

    // Output Format row
    QHBoxLayout* formatLayout = new QHBoxLayout();
    QLabel* formatLabel = new QLabel(tr("Output Format:"), tab);
    formatLabel->setFixedWidth(120);
    m_recordingOutputFormatCombo = new QComboBox(tab);
    m_recordingOutputFormatCombo->setMinimumWidth(120);
    m_recordingOutputFormatCombo->addItem(tr("MP4 (H.264)"), 0);
    m_recordingOutputFormatCombo->addItem(tr("GIF"), 1);
    m_recordingOutputFormatCombo->addItem(tr("WebP"), 2);
    formatLayout->addWidget(formatLabel);
    formatLayout->addWidget(m_recordingOutputFormatCombo);
    formatLayout->addStretch();
    layout->addLayout(formatLayout);

    // ========== MP4 Settings (visible when MP4 selected) ==========
    m_mp4SettingsWidget = new QWidget(tab);
    QVBoxLayout* mp4Layout = new QVBoxLayout(m_mp4SettingsWidget);
    mp4Layout->setContentsMargins(0, 8, 0, 0);

    // Quality slider
    QHBoxLayout* qualityLayout = new QHBoxLayout();
    QLabel* qualityLabel = new QLabel(tr("Quality:"), m_mp4SettingsWidget);
    qualityLabel->setFixedWidth(120);
    m_recordingQualitySlider = new QSlider(Qt::Horizontal, m_mp4SettingsWidget);
    m_recordingQualitySlider->setRange(0, 100);
    m_recordingQualitySlider->setValue(55);  // Default quality
    m_recordingQualityLabel = new QLabel("55", m_mp4SettingsWidget);
    m_recordingQualityLabel->setFixedWidth(40);
    connect(m_recordingQualitySlider, &QSlider::valueChanged, this, [this](int value) {
        m_recordingQualityLabel->setText(QString::number(value));
        });
    qualityLayout->addWidget(qualityLabel);
    qualityLayout->addWidget(m_recordingQualitySlider);
    qualityLayout->addWidget(m_recordingQualityLabel);
    mp4Layout->addLayout(qualityLayout);

    layout->addWidget(m_mp4SettingsWidget);

    // ========== GIF Settings (visible when GIF selected) ==========
    m_gifSettingsWidget = new QWidget(tab);
    QVBoxLayout* gifLayout = new QVBoxLayout(m_gifSettingsWidget);
    gifLayout->setContentsMargins(0, 8, 0, 0);

    // Info label
    m_gifInfoLabel = new QLabel(
        tr("GIF format creates larger files than MP4.\n"
        "Best for short clips and sharing on web.\n"
        "Audio is not supported for GIF recordings."),
        m_gifSettingsWidget);
    m_gifInfoLabel->setWordWrap(true);
    m_gifInfoLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_gifInfoLabel->setStyleSheet(
        QStringLiteral("QLabel { background-color: %1; color: %2; "
            "padding: 10px; border-radius: 4px; }")
        .arg(SnapTray::SettingsTheme::infoPanelBg(), SnapTray::SettingsTheme::infoPanelText()));
    gifLayout->addWidget(m_gifInfoLabel);

    layout->addWidget(m_gifSettingsWidget);

    // ========== Audio Settings Section ==========
    layout->addSpacing(16);
    QLabel* audioSectionLabel = new QLabel(tr("Audio"), tab);
    audioSectionLabel->setStyleSheet("font-weight: bold; font-size: 12px;");
    layout->addWidget(audioSectionLabel);

    // Enable audio checkbox
    m_audioEnabledCheckbox = new QCheckBox(tr("Record audio"), tab);
    layout->addWidget(m_audioEnabledCheckbox);

    // Audio source dropdown
    QHBoxLayout* audioSourceLayout = new QHBoxLayout();
    QLabel* audioSourceLabel = new QLabel(tr("Source:"), tab);
    audioSourceLabel->setFixedWidth(120);
    m_audioSourceCombo = new QComboBox(tab);
    m_audioSourceCombo->addItem(tr("Microphone"), 0);
    m_audioSourceCombo->addItem(tr("System Audio"), 1);
    m_audioSourceCombo->addItem(tr("Both (Mixed)"), 2);
    audioSourceLayout->addWidget(audioSourceLabel);
    audioSourceLayout->addWidget(m_audioSourceCombo);
    audioSourceLayout->addStretch();
    layout->addLayout(audioSourceLayout);

    // Audio device dropdown
    QHBoxLayout* audioDeviceLayout = new QHBoxLayout();
    QLabel* audioDeviceLabel = new QLabel(tr("Device:"), tab);
    audioDeviceLabel->setFixedWidth(120);
    m_audioDeviceCombo = new QComboBox(tab);
    m_audioDeviceCombo->addItem(tr("Default"), QString());
    audioDeviceLayout->addWidget(audioDeviceLabel);
    audioDeviceLayout->addWidget(m_audioDeviceCombo);
    audioDeviceLayout->addStretch();
    layout->addLayout(audioDeviceLayout);

    // System audio warning label
    m_systemAudioWarningLabel = new QLabel(tab);
    m_systemAudioWarningLabel->setWordWrap(true);
    m_systemAudioWarningLabel->setMinimumHeight(50);
    m_systemAudioWarningLabel->setStyleSheet(
        QStringLiteral("QLabel { background-color: %1; color: %2; "
            "padding: 8px; border-radius: 4px; font-size: 11px; }")
        .arg(SnapTray::SettingsTheme::warningPanelBg(), SnapTray::SettingsTheme::warningPanelText()));
    m_systemAudioWarningLabel->hide();
    layout->addWidget(m_systemAudioWarningLabel);

    // Connect audio UI signals
    connect(m_audioEnabledCheckbox, &QCheckBox::toggled, this, [this](bool checked) {
        m_audioSourceCombo->setEnabled(checked);
        m_audioDeviceCombo->setEnabled(checked);
        if (checked) {
            onAudioSourceChanged(m_audioSourceCombo->currentIndex());
        }
        else {
            m_systemAudioWarningLabel->hide();
        }
        });
    connect(m_audioSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &SettingsDialog::onAudioSourceChanged);

    // ========== Preview option ==========
    layout->addSpacing(16);
    m_recordingShowPreviewCheckbox = new QCheckBox(tr("Show preview after recording"), tab);
    layout->addWidget(m_recordingShowPreviewCheckbox);

    // ========== Countdown settings ==========
    layout->addSpacing(16);
    QLabel* countdownHeader = new QLabel(tr("Countdown"), tab);
    QFont headerFont = countdownHeader->font();
    headerFont.setBold(true);
    countdownHeader->setFont(headerFont);
    layout->addWidget(countdownHeader);

    m_countdownEnabledCheckbox = new QCheckBox(tr("Show countdown before recording"), tab);
    m_countdownEnabledCheckbox->setToolTip(tr("Display a 3-2-1 countdown before recording starts"));
    layout->addWidget(m_countdownEnabledCheckbox);

    QHBoxLayout* countdownSecondsLayout = new QHBoxLayout();
    countdownSecondsLayout->setContentsMargins(20, 0, 0, 0);  // Indent under checkbox
    QLabel* countdownSecondsLabel = new QLabel(tr("Countdown duration:"), tab);
    m_countdownSecondsCombo = new QComboBox(tab);
    m_countdownSecondsCombo->addItem(tr("1 second"), 1);
    m_countdownSecondsCombo->addItem(tr("2 seconds"), 2);
    m_countdownSecondsCombo->addItem(tr("3 seconds"), 3);
    m_countdownSecondsCombo->addItem(tr("4 seconds"), 4);
    m_countdownSecondsCombo->addItem(tr("5 seconds"), 5);
    m_countdownSecondsCombo->setCurrentIndex(2);  // Default: 3 seconds
    countdownSecondsLayout->addWidget(countdownSecondsLabel);
    countdownSecondsLayout->addWidget(m_countdownSecondsCombo);
    countdownSecondsLayout->addStretch();
    layout->addLayout(countdownSecondsLayout);

    // Connect countdown enabled to seconds combo
    connect(m_countdownEnabledCheckbox, &QCheckBox::toggled, m_countdownSecondsCombo, &QWidget::setEnabled);

    layout->addStretch();

    // ========== Connect format change to show/hide widgets ==========
    connect(m_recordingOutputFormatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &SettingsDialog::onOutputFormatChanged);

    // ========== Load settings ==========
    auto settings = SnapTray::getSettings();

    int fps = settings.value("recording/framerate", 30).toInt();
    int fpsIndex = m_recordingFrameRateCombo->findData(fps);
    if (fpsIndex >= 0) {
        m_recordingFrameRateCombo->setCurrentIndex(fpsIndex);
    }

    int outputFormat = settings.value("recording/outputFormat", 0).toInt();
    m_recordingOutputFormatCombo->setCurrentIndex(outputFormat);

    // Load quality (use saved quality or convert from CRF for backward compatibility)
    int quality = settings.value("recording/quality", -1).toInt();
    if (quality < 0) {
        // No quality setting yet, convert from CRF
        int crf = settings.value("recording/crf", 23).toInt();
        quality = 100 - (crf * 100 / 51);
    }
    m_recordingQualitySlider->setValue(quality);
    m_recordingQualityLabel->setText(QString::number(quality));

    m_recordingShowPreviewCheckbox->setChecked(
        settings.value("recording/showPreview", true).toBool());

    // Load countdown settings
    bool countdownEnabled = settings.value("recording/countdownEnabled", true).toBool();
    m_countdownEnabledCheckbox->setChecked(countdownEnabled);
    int countdownSeconds = settings.value("recording/countdownSeconds", 3).toInt();
    int countdownIndex = m_countdownSecondsCombo->findData(countdownSeconds);
    if (countdownIndex >= 0) {
        m_countdownSecondsCombo->setCurrentIndex(countdownIndex);
    }
    m_countdownSecondsCombo->setEnabled(countdownEnabled);

    // Load audio settings
    bool audioEnabled = settings.value("recording/audioEnabled", false).toBool();
    m_audioEnabledCheckbox->setChecked(audioEnabled);
    int audioSource = settings.value("recording/audioSource", 0).toInt();
    m_audioSourceCombo->setCurrentIndex(audioSource);
    QString audioDevice = settings.value("recording/audioDevice").toString();
    int deviceIndex = m_audioDeviceCombo->findData(audioDevice);
    if (deviceIndex >= 0) {
        m_audioDeviceCombo->setCurrentIndex(deviceIndex);
    }
    // Set initial enabled state for audio controls
    m_audioSourceCombo->setEnabled(audioEnabled);
    m_audioDeviceCombo->setEnabled(audioEnabled);
    if (audioEnabled) {
        onAudioSourceChanged(audioSource);
    }

    // Lazy load audio devices - only populate when audio is enabled
    // This avoids expensive CoreAudio enumeration on dialog open
    connect(m_audioEnabledCheckbox, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked && m_audioDeviceCombo->count() == 0) {
            populateAudioDevices();
        }
        });

    // If audio was already enabled, populate devices after dialog is shown
    if (audioEnabled) {
        QTimer::singleShot(0, this, [this]() {
            if (m_audioDeviceCombo->count() == 0) {
                populateAudioDevices();
            }
            });
    }

    onOutputFormatChanged(outputFormat);  // Initial visibility
}

void SettingsDialog::onOutputFormatChanged(int index)
{
    bool isMp4 = (index == 0);
    bool isGif = (index == 1);
    bool isWebP = (index == 2);

    m_mp4SettingsWidget->setVisible(isMp4);
    m_gifSettingsWidget->setVisible(isGif || isWebP);

    // Update info label text for the selected format
    if (isWebP) {
        m_gifInfoLabel->setText(
            tr("WebP format creates smaller files than GIF with better quality.\n"
            "Best for short clips and sharing on web.\n"
            "Audio is not supported for WebP recordings."));
    } else {
        m_gifInfoLabel->setText(
            tr("GIF format creates larger files than MP4.\n"
            "Best for short clips and sharing on web.\n"
            "Audio is not supported for GIF recordings."));
    }

    // Audio is only supported for MP4
    bool supportsAudio = isMp4;
    m_audioEnabledCheckbox->setEnabled(supportsAudio);
    m_audioSourceCombo->setEnabled(supportsAudio && m_audioEnabledCheckbox->isChecked());
    m_audioDeviceCombo->setEnabled(supportsAudio && m_audioEnabledCheckbox->isChecked());
    if (!supportsAudio) {
        m_systemAudioWarningLabel->hide();
    }
}

void SettingsDialog::onSave()
{
    // Apply start-on-login setting immediately
    bool desiredStartOnLogin = m_startOnLoginCheckbox->isChecked();
    bool currentState = AutoLaunchManager::isEnabled();
    if (desiredStartOnLogin != currentState) {
        bool success = AutoLaunchManager::setEnabled(desiredStartOnLogin);
        if (!success) {
            qDebug() << "Failed to update auto-launch setting";
        }
        emit startOnLoginChanged(desiredStartOnLogin);
    }

    // Save watermark settings
    WatermarkRenderer::Settings watermarkSettings;
    watermarkSettings.enabled = m_watermarkEnabledCheckbox->isChecked();
    watermarkSettings.applyToRecording = m_watermarkApplyToRecordingCheckbox->isChecked();
    watermarkSettings.imagePath = m_watermarkImagePathEdit->text();
    watermarkSettings.opacity = m_watermarkOpacitySlider->value() / 100.0;
    watermarkSettings.position = static_cast<WatermarkRenderer::Position>(
        m_watermarkPositionCombo->currentData().toInt());
    watermarkSettings.imageScale = m_watermarkImageScaleSlider->value();
    watermarkSettings.margin = m_watermarkMarginSlider->value();
    WatermarkSettingsManager::instance().save(watermarkSettings);

    // Save recording settings
    auto recordingSettings = SnapTray::getSettings();
    recordingSettings.setValue("recording/framerate",
        m_recordingFrameRateCombo->currentData().toInt());
    recordingSettings.setValue("recording/outputFormat",
        m_recordingOutputFormatCombo->currentIndex());
    recordingSettings.setValue("recording/quality",
        m_recordingQualitySlider->value());
    // Also save CRF for backward compatibility with FFmpeg fallback
    int quality = m_recordingQualitySlider->value();
    int crf = 51 - (quality * 51 / 100);
    recordingSettings.setValue("recording/crf", crf);
    recordingSettings.setValue("recording/showPreview",
        m_recordingShowPreviewCheckbox->isChecked());

    // Save countdown settings
    recordingSettings.setValue("recording/countdownEnabled",
        m_countdownEnabledCheckbox->isChecked());
    recordingSettings.setValue("recording/countdownSeconds",
        m_countdownSecondsCombo->currentData().toInt());

    // Save audio settings
    recordingSettings.setValue("recording/audioEnabled",
        m_audioEnabledCheckbox->isChecked());
    recordingSettings.setValue("recording/audioSource",
        m_audioSourceCombo->currentData().toInt());
    recordingSettings.setValue("recording/audioDevice",
        m_audioDeviceCombo->currentData().toString());

    // Save auto-blur settings
    AutoBlurSettingsManager::Options blurOptions;
    blurOptions.blurIntensity = m_blurIntensitySlider->value();
    blurOptions.blurType = m_blurTypeCombo->currentIndex() == 1
        ? AutoBlurSettingsManager::BlurType::Gaussian
        : AutoBlurSettingsManager::BlurType::Pixelate;
    AutoBlurSettingsManager::instance().save(blurOptions);

    // Save Pin Window settings
    auto& pinSettings = PinWindowSettingsManager::instance();
    pinSettings.saveDefaultOpacity(m_pinWindowOpacitySlider->value() / 100.0);
    pinSettings.saveOpacityStep(m_pinWindowOpacityStepSlider->value() / 100.0);
    pinSettings.saveZoomStep(m_pinWindowZoomStepSlider->value() / 100.0);
    pinSettings.saveMaxCacheFiles(m_pinWindowMaxCacheFilesSlider->value());

    // Save file settings
    auto& fileSettings = FileSettingsManager::instance();
    fileSettings.saveScreenshotPath(m_screenshotPathEdit->text());
    fileSettings.saveRecordingPath(m_recordingPathEdit->text());
    fileSettings.saveFilenameTemplate(m_filenameTemplateEdit->text());
    fileSettings.saveAutoSaveScreenshots(m_autoSaveScreenshotsCheckbox->isChecked());
    fileSettings.saveAutoSaveRecordings(m_autoSaveRecordingsCheckbox->isChecked());

    // Save OCR language settings (only if OCR tab was visited)
    if (m_ocrSelectedList) {
        QStringList ocrLanguages;
        for (int i = 0; i < m_ocrSelectedList->count(); ++i) {
            ocrLanguages << m_ocrSelectedList->item(i)->data(Qt::UserRole).toString();
        }
        auto& ocrSettings = OCRSettingsManager::instance();
        ocrSettings.setLanguages(ocrLanguages);

        // Save OCR behavior setting
        if (m_ocrShowEditorRadio && m_ocrShowEditorRadio->isChecked()) {
            ocrSettings.setBehavior(OCRBehavior::ShowEditor);
        }
        else {
            ocrSettings.setBehavior(OCRBehavior::DirectCopy);
        }

        ocrSettings.save();
        emit ocrLanguagesChanged(ocrLanguages);
    }

    // Save toolbar style setting
    ToolbarStyleType newStyle = static_cast<ToolbarStyleType>(
        m_toolbarStyleCombo->currentData().toInt());
    ToolbarStyleConfig::saveStyle(newStyle);
    emit toolbarStyleChanged(newStyle);

    // Save update settings (only if Direct Download mode)
    if (m_autoCheckUpdatesCheckbox) {
        auto& updateSettings = UpdateSettingsManager::instance();
        updateSettings.setAutoCheckEnabled(m_autoCheckUpdatesCheckbox->isChecked());
        if (m_checkFrequencyCombo) {
            updateSettings.setCheckIntervalHours(m_checkFrequencyCombo->currentData().toInt());
        }
    }

    // Save language setting
    if (m_languageCombo) {
        const QString selectedLanguage = m_languageCombo->currentData().toString();
        const QString currentLanguage = LanguageManager::instance().loadLanguage();
        if (selectedLanguage != currentLanguage) {
            LanguageManager::instance().saveLanguage(selectedLanguage);
            QMessageBox::information(
                this,
                tr("Restart Required"),
                tr("The language change will take effect after restarting the application."));
        }
    }

    // Close dialog (non-modal mode)
    accept();
}

void SettingsDialog::onAccepted()
{
    // Start-on-login is now applied immediately in onSave()
    // This slot is kept for potential future use
}

void SettingsDialog::onCancel()
{
    reject();
}

void SettingsDialog::updateWatermarkImagePreview()
{
    QString imagePath = m_watermarkImagePathEdit->text();

    if (imagePath.isEmpty()) {
        m_watermarkImagePreview->clear();
        m_watermarkImagePreview->setText(tr("No image"));
        m_watermarkImageSizeLabel->clear();
        m_watermarkOriginalSize = QSize();
        return;
    }

    QPixmap pixmap(imagePath);
    if (pixmap.isNull()) {
        m_watermarkImagePreview->clear();
        m_watermarkImagePreview->setText(tr("Invalid image"));
        m_watermarkImageSizeLabel->clear();
        m_watermarkOriginalSize = QSize();
        return;
    }

    // Store original size
    m_watermarkOriginalSize = pixmap.size();

    // Scale for preview display
    QPixmap scaled = pixmap.scaled(
        m_watermarkImagePreview->size(),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation);
    m_watermarkImagePreview->setPixmap(scaled);

    // Calculate and display scaled dimensions
    int scale = m_watermarkImageScaleSlider->value();
    int scaledWidth = m_watermarkOriginalSize.width() * scale / 100;
    int scaledHeight = m_watermarkOriginalSize.height() * scale / 100;
    m_watermarkImageSizeLabel->setText(
        tr("Size: %1 × %2 px").arg(scaledWidth).arg(scaledHeight));
}

void SettingsDialog::populateAudioDevices()
{
    // Save current selection
    QString currentDevice = m_audioDeviceCombo->currentData().toString();

    m_audioDeviceCombo->clear();
    m_audioDeviceCombo->addItem(tr("Default"), QString());

    // Enumerate available audio input devices
    std::unique_ptr<IAudioCaptureEngine> engine(
        IAudioCaptureEngine::createBestEngine(nullptr));
    if (engine && engine->isAvailable()) {
        auto devices = engine->availableInputDevices();
        for (const auto& device : devices) {
            if (!device.id.isEmpty()) {
                m_audioDeviceCombo->addItem(device.name, device.id);
            }
        }
    }

    // Restore selection if possible
    int index = m_audioDeviceCombo->findData(currentDevice);
    if (index >= 0) {
        m_audioDeviceCombo->setCurrentIndex(index);
    }
}

void SettingsDialog::onAudioSourceChanged(int index)
{
    if (!m_audioEnabledCheckbox->isChecked()) {
        m_systemAudioWarningLabel->hide();
        return;
    }

    // Show/hide warning based on audio source selection
    bool needsSystemAudio = (index == 1 || index == 2);  // System Audio or Both
    bool needsMicrophone = (index == 0 || index == 2);   // Microphone or Both

    // Check microphone permission if needed (macOS)
    if (needsMicrophone) {
        auto permission = IAudioCaptureEngine::checkMicrophonePermission();
        if (permission == IAudioCaptureEngine::MicrophonePermission::NotDetermined) {
            // Request permission
            IAudioCaptureEngine::requestMicrophonePermission([this](bool granted) {
                if (!granted) {
                    m_systemAudioWarningLabel->setText(
                        tr("Microphone access denied. Please enable in System Settings > "
                        "Privacy & Security > Microphone."));
                    m_systemAudioWarningLabel->show();
                }
                });
        }
        else if (permission == IAudioCaptureEngine::MicrophonePermission::Denied ||
            permission == IAudioCaptureEngine::MicrophonePermission::Restricted) {
            m_systemAudioWarningLabel->setText(
                tr("Microphone access denied. Please enable in System Settings > "
                "Privacy & Security > Microphone."));
            m_systemAudioWarningLabel->show();
        }
        else {
            // Permission granted, hide warning if not showing system audio warning
            if (!needsSystemAudio) {
                m_systemAudioWarningLabel->hide();
            }
        }
    }

    if (needsSystemAudio) {
#ifdef Q_OS_MAC
        // macOS system audio requires ScreenCaptureKit (macOS 13+) or virtual audio device
        // We'll check availability at runtime when audio engine is created
        m_systemAudioWarningLabel->setText(
            tr("System audio capture requires macOS 13 (Ventura) or later, "
            "or a virtual audio device like BlackHole."));
        m_systemAudioWarningLabel->show();
#else
        // Windows supports system audio via WASAPI loopback
        m_systemAudioWarningLabel->hide();
#endif
    }
    else if (!needsMicrophone) {
        m_systemAudioWarningLabel->hide();
    }

    // Device combo is only relevant for microphone
    bool showDeviceCombo = (index == 0 || index == 2);  // Microphone or Both
    m_audioDeviceCombo->setVisible(showDeviceCombo);
}

void SettingsDialog::setupOcrTab(QWidget* tab)
{
    QVBoxLayout* mainLayout = new QVBoxLayout(tab);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // Loading widget (shown initially)
    m_ocrLoadingWidget = new QWidget(tab);
    QVBoxLayout* loadingLayout = new QVBoxLayout(m_ocrLoadingWidget);
    loadingLayout->addStretch();
    m_ocrLoadingLabel = new QLabel(tr("Loading available languages..."), m_ocrLoadingWidget);
    m_ocrLoadingLabel->setAlignment(Qt::AlignCenter);
    loadingLayout->addWidget(m_ocrLoadingLabel);
    loadingLayout->addStretch();
    mainLayout->addWidget(m_ocrLoadingWidget);

    // Content widget (hidden initially, populated when tab is selected)
    m_ocrContentWidget = new QWidget(tab);
    m_ocrContentWidget->hide();
    mainLayout->addWidget(m_ocrContentWidget);

    m_ocrTabInitialized = false;
}

void SettingsDialog::onTabChanged(int index)
{
    if (index == m_ocrTabIndex && !m_ocrTabInitialized) {
        // Use QTimer::singleShot to allow the tab UI to render first
        QTimer::singleShot(0, this, &SettingsDialog::loadOcrLanguages);
    }
}

void SettingsDialog::loadOcrLanguages()
{
    if (m_ocrTabInitialized) return;
    m_ocrTabInitialized = true;

    // Setup the content widget layout
    QVBoxLayout* contentLayout = new QVBoxLayout(m_ocrContentWidget);

    // Info label
    m_ocrInfoLabel = new QLabel(
        tr("Select and order the languages for OCR recognition.\n"
            "English is always included and cannot be removed. Drag to reorder selected languages."), m_ocrContentWidget);
    m_ocrInfoLabel->setWordWrap(true);
    contentLayout->addWidget(m_ocrInfoLabel);

    contentLayout->addSpacing(12);

    // Horizontal layout for the two lists
    QHBoxLayout* listsLayout = new QHBoxLayout();

    // Left: Available languages
    QVBoxLayout* availableLayout = new QVBoxLayout();
    QLabel* availableLabel = new QLabel(tr("Available Languages"), m_ocrContentWidget);
    availableLabel->setStyleSheet("font-weight: bold;");
    availableLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    availableLabel->setMinimumWidth(0);
    availableLayout->addWidget(availableLabel);

    m_ocrAvailableList = new QListWidget(m_ocrContentWidget);
    m_ocrAvailableList->setFrameShape(QFrame::Box);
    m_ocrAvailableList->setFrameShadow(QFrame::Plain);
    m_ocrAvailableList->setLineWidth(1);
    m_ocrAvailableList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_ocrAvailableList->setSortingEnabled(true);
    m_ocrAvailableList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    availableLayout->addWidget(m_ocrAvailableList);
    listsLayout->addLayout(availableLayout, 1);

    // Middle: Add/Remove buttons
    QVBoxLayout* btnLayout = new QVBoxLayout();
    btnLayout->addStretch();
    m_ocrAddBtn = new QPushButton(QString::fromUtf8("\u2192"), m_ocrContentWidget);  // →
    m_ocrAddBtn->setFixedWidth(40);
    m_ocrAddBtn->setToolTip(tr("Add selected languages"));
    m_ocrRemoveBtn = new QPushButton(QString::fromUtf8("\u2190"), m_ocrContentWidget);  // ←
    m_ocrRemoveBtn->setFixedWidth(40);
    m_ocrRemoveBtn->setToolTip(tr("Remove selected languages"));
    btnLayout->addWidget(m_ocrAddBtn);
    btnLayout->addSpacing(8);
    btnLayout->addWidget(m_ocrRemoveBtn);
    btnLayout->addStretch();
    listsLayout->addLayout(btnLayout, 0);

    // Right: Selected languages (with drag-drop reorder)
    QVBoxLayout* selectedLayout = new QVBoxLayout();
    QLabel* selectedLabel = new QLabel(tr("Selected Languages"), m_ocrContentWidget);
    selectedLabel->setStyleSheet("font-weight: bold;");
    selectedLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    selectedLabel->setMinimumWidth(0);
    selectedLayout->addWidget(selectedLabel);

    m_ocrSelectedList = new QListWidget(m_ocrContentWidget);
    m_ocrSelectedList->setFrameShape(QFrame::Box);
    m_ocrSelectedList->setFrameShadow(QFrame::Plain);
    m_ocrSelectedList->setLineWidth(1);
    m_ocrSelectedList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_ocrSelectedList->setDragDropMode(QAbstractItemView::InternalMove);
    m_ocrSelectedList->setDefaultDropAction(Qt::MoveAction);
    m_ocrSelectedList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    selectedLayout->addWidget(m_ocrSelectedList);
    listsLayout->addLayout(selectedLayout, 1);

    contentLayout->addLayout(listsLayout);

    // Populate available languages (this is the slow call)
    auto availableLanguages = OCRManager::availableLanguages();
    auto& ocrSettings = OCRSettingsManager::instance();
    QStringList selectedCodes = ocrSettings.languages();
    if (!selectedCodes.contains("en-US")) {
        selectedCodes.append("en-US");
    }

    for (const auto& lang : availableLanguages) {
        // Skip en-US in available list (always required)
        if (lang.code == "en-US") {
            continue;
        }

        // Skip if already selected
        if (selectedCodes.contains(lang.code)) {
            continue;
        }

        QString displayText = QString("%1 (%2)").arg(lang.nativeName, lang.englishName);
        QListWidgetItem* item = new QListWidgetItem(displayText);
        item->setData(Qt::UserRole, lang.code);
        m_ocrAvailableList->addItem(item);
    }

    // Populate selected languages
    for (const QString& code : selectedCodes) {
        QString displayText = code;
        if (code == "en-US") {
            displayText = QStringLiteral("English (en-US)");
        }
        else {
            for (const auto& lang : availableLanguages) {
                if (lang.code == code) {
                    displayText = QString("%1 (%2)").arg(lang.nativeName, lang.englishName);
                    break;
                }
            }
        }

        QListWidgetItem* item = new QListWidgetItem(displayText);
        item->setData(Qt::UserRole, code);
        if (code == "en-US") {
            item->setToolTip(tr("English is always included and cannot be removed"));
        }
        m_ocrSelectedList->addItem(item);
    }

    // Connect signals
    connect(m_ocrAddBtn, &QPushButton::clicked, this, &SettingsDialog::onAddOcrLanguage);
    connect(m_ocrRemoveBtn, &QPushButton::clicked, this, &SettingsDialog::onRemoveOcrLanguage);
    // Double-click to add/remove
    connect(m_ocrAvailableList, &QListWidget::itemDoubleClicked,
        this, &SettingsDialog::onAddOcrLanguage);
    connect(m_ocrSelectedList, &QListWidget::itemDoubleClicked,
        this, [this](QListWidgetItem* item) {
            if (item->data(Qt::UserRole).toString() != "en-US") {
                onRemoveOcrLanguage();
            }
        });

    // === OCR Behavior Section ===
    contentLayout->addSpacing(16);
    QGroupBox* behaviorGroup = new QGroupBox(tr("After OCR Recognition"), m_ocrContentWidget);
    QVBoxLayout* behaviorLayout = new QVBoxLayout(behaviorGroup);

    m_ocrDirectCopyRadio = new QRadioButton(
        tr("Copy text directly to clipboard"), behaviorGroup);
    m_ocrShowEditorRadio = new QRadioButton(
        tr("Show editor to review and edit text"), behaviorGroup);

    // Set tooltips for clarity
    m_ocrDirectCopyRadio->setToolTip(
        tr("Recognized text is immediately copied to clipboard"));
    m_ocrShowEditorRadio->setToolTip(
        tr("Opens a dialog where you can review and edit the text before copying"));

    behaviorLayout->addWidget(m_ocrDirectCopyRadio);
    behaviorLayout->addWidget(m_ocrShowEditorRadio);

    // Load current setting
    if (OCRSettingsManager::instance().behavior() == OCRBehavior::ShowEditor) {
        m_ocrShowEditorRadio->setChecked(true);
    }
    else {
        m_ocrDirectCopyRadio->setChecked(true);
    }

    contentLayout->addWidget(behaviorGroup);

    contentLayout->addStretch();

    // Show warning if no languages available
    if (availableLanguages.isEmpty()) {
        m_ocrInfoLabel->setText(
            tr("No OCR languages available.\n\n"
                "macOS: OCR requires macOS 10.15 or later.\n"
                "Windows: Install language packs in Settings > Time & Language > Language."));
        m_ocrAddBtn->setEnabled(false);
        m_ocrRemoveBtn->setEnabled(false);
    }

    // Switch from loading to content
    m_ocrLoadingWidget->hide();
    m_ocrContentWidget->show();
}

void SettingsDialog::onAddOcrLanguage()
{
    QList<QListWidgetItem*> selected = m_ocrAvailableList->selectedItems();
    for (QListWidgetItem* item : selected) {
        QString code = item->data(Qt::UserRole).toString();
        QString text = item->text();

        // Add to selected list
        QListWidgetItem* newItem = new QListWidgetItem(text);
        newItem->setData(Qt::UserRole, code);
        m_ocrSelectedList->addItem(newItem);

        // Remove from available list
        delete m_ocrAvailableList->takeItem(m_ocrAvailableList->row(item));
    }
}

void SettingsDialog::onRemoveOcrLanguage()
{
    QList<QListWidgetItem*> selected = m_ocrSelectedList->selectedItems();
    for (QListWidgetItem* item : selected) {
        QString code = item->data(Qt::UserRole).toString();

        // Cannot remove en-US
        if (code == "en-US") {
            continue;
        }

        QString text = item->text();

        // Add back to available list
        QListWidgetItem* newItem = new QListWidgetItem(text);
        newItem->setData(Qt::UserRole, code);
        m_ocrAvailableList->addItem(newItem);

        // Remove from selected list
        delete m_ocrSelectedList->takeItem(m_ocrSelectedList->row(item));
    }
}

void SettingsDialog::setupFilesTab(QWidget* tab)
{
    QVBoxLayout* layout = new QVBoxLayout(tab);

    auto& fileSettings = FileSettingsManager::instance();

    // ========== Save Locations Section ==========
    QLabel* locationsLabel = new QLabel(tr("Save Locations"), tab);
    locationsLabel->setStyleSheet("font-weight: bold; font-size: 12px;");
    layout->addWidget(locationsLabel);

    // Screenshot path row
    QHBoxLayout* screenshotLayout = new QHBoxLayout();
    QLabel* screenshotLabel = new QLabel(tr("Screenshots:"), tab);
    screenshotLabel->setFixedWidth(90);
    m_screenshotPathEdit = new QLineEdit(tab);
    m_screenshotPathEdit->setReadOnly(true);
    m_screenshotPathEdit->setText(fileSettings.loadScreenshotPath());
    m_screenshotPathBrowseBtn = new QPushButton(tr("Browse..."), tab);
    m_screenshotPathBrowseBtn->setFixedWidth(80);
    connect(m_screenshotPathBrowseBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, tr("Select Screenshot Folder"),
            m_screenshotPathEdit->text());
        if (!dir.isEmpty()) {
            m_screenshotPathEdit->setText(dir);
        }
        });
    screenshotLayout->addWidget(screenshotLabel);
    screenshotLayout->addWidget(m_screenshotPathEdit);
    screenshotLayout->addWidget(m_screenshotPathBrowseBtn);
    layout->addLayout(screenshotLayout);

    // Recording path row
    QHBoxLayout* recordingLayout = new QHBoxLayout();
    QLabel* recordingLabel = new QLabel(tr("Recordings:"), tab);
    recordingLabel->setFixedWidth(90);
    m_recordingPathEdit = new QLineEdit(tab);
    m_recordingPathEdit->setReadOnly(true);
    m_recordingPathEdit->setText(fileSettings.loadRecordingPath());
    m_recordingPathBrowseBtn = new QPushButton(tr("Browse..."), tab);
    m_recordingPathBrowseBtn->setFixedWidth(80);
    connect(m_recordingPathBrowseBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, tr("Select Recording Folder"),
            m_recordingPathEdit->text());
        if (!dir.isEmpty()) {
            m_recordingPathEdit->setText(dir);
        }
        });
    recordingLayout->addWidget(recordingLabel);
    recordingLayout->addWidget(m_recordingPathEdit);
    recordingLayout->addWidget(m_recordingPathBrowseBtn);
    layout->addLayout(recordingLayout);

    // ========== Filename Format Section ==========
    layout->addSpacing(16);
    QLabel* formatLabel = new QLabel(tr("Filename Format"), tab);
    formatLabel->setStyleSheet("font-weight: bold; font-size: 12px;");
    layout->addWidget(formatLabel);

    QHBoxLayout* templateLayout = new QHBoxLayout();
    QLabel* templateLabel = new QLabel(tr("Template:"), tab);
    templateLabel->setFixedWidth(90);
    m_filenameTemplateEdit = new QLineEdit(tab);
    m_filenameTemplateEdit->setPlaceholderText("{prefix}_{type}_{yyyyMMdd_HHmmss}_{w}x{h}_{monitor}_{windowTitle}.{ext}");
    m_filenameTemplateEdit->setText(fileSettings.loadFilenameTemplate());
    connect(m_filenameTemplateEdit, &QLineEdit::textChanged, this, [this]() {
        updateFilenamePreview();
        });
    templateLayout->addWidget(templateLabel);
    templateLayout->addWidget(m_filenameTemplateEdit);
    layout->addLayout(templateLayout);

    QLabel* tokensLabel = new QLabel(
        tr("Tokens: {prefix} {type} {w} {h} {monitor} {windowTitle} {appName} {regionIndex} {ext} {#}\n"
        "Date tokens: {yyyyMMdd_HHmmss}, {yyyy-MM-dd_HH-mm-ss}, or {date}"),
        tab);
    tokensLabel->setStyleSheet(
        QStringLiteral("color: %1; font-size: 11px;")
        .arg(SnapTray::SettingsTheme::secondaryText()));
    tokensLabel->setWordWrap(true);
    layout->addWidget(tokensLabel);

    // Preview label
    m_filenamePreviewLabel = new QLabel(tab);
    m_filenamePreviewLabel->setWordWrap(true);
    m_filenamePreviewLabel->setStyleSheet(
        QStringLiteral("color: %1; font-size: 11px; padding: 8px 0;")
        .arg(SnapTray::SettingsTheme::secondaryText()));
    layout->addWidget(m_filenamePreviewLabel);

    updateFilenamePreview();

    // ========== Save Behavior Section ==========
    layout->addSpacing(16);
    QLabel* behaviorLabel = new QLabel(tr("Save Behavior"), tab);
    behaviorLabel->setStyleSheet("font-weight: bold; font-size: 12px;");
    layout->addWidget(behaviorLabel);

    m_autoSaveScreenshotsCheckbox = new QCheckBox(tr("Auto-save screenshots (save directly without dialog)"), tab);
    m_autoSaveScreenshotsCheckbox->setChecked(fileSettings.loadAutoSaveScreenshots());
    layout->addWidget(m_autoSaveScreenshotsCheckbox);

    m_autoSaveRecordingsCheckbox = new QCheckBox(tr("Auto-save recordings (save directly without dialog)"), tab);
    m_autoSaveRecordingsCheckbox->setChecked(fileSettings.loadAutoSaveRecordings());
    layout->addWidget(m_autoSaveRecordingsCheckbox);

    layout->addStretch();
}

void SettingsDialog::updateFilenamePreview()
{
    if (!m_filenameTemplateEdit || !m_filenamePreviewLabel) {
        return;
    }

    auto& fileSettings = FileSettingsManager::instance();
    FilenameTemplateEngine::Context context;
    context.type = QStringLiteral("Screenshot");
    context.prefix = fileSettings.loadFilenamePrefix();
    context.width = 1920;
    context.height = 1080;
    context.monitor = QStringLiteral("0");
    context.windowTitle = QStringLiteral("unknown");
    context.appName = QStringLiteral("unknown");
    context.regionIndex = 1;
    context.ext = QStringLiteral("png");
    context.dateFormat = fileSettings.loadDateFormat();

    const FilenameTemplateEngine::Result result =
        FilenameTemplateEngine::renderFilename(m_filenameTemplateEdit->text(), context);

    QString previewText = tr("Preview: %1").arg(result.filename);
    if (result.usedFallback) {
        previewText += tr("\nInvalid template, fallback applied: %1").arg(result.error);
    }
    m_filenamePreviewLabel->setText(previewText);
}

void SettingsDialog::setupUpdatesTab(QWidget* tab)
{
    QVBoxLayout* layout = new QVBoxLayout(tab);

    // ========== Version Information Section ==========
    QLabel* versionInfoLabel = new QLabel(tr("Version Information"), tab);
    versionInfoLabel->setStyleSheet("font-weight: bold; font-size: 12px;");
    layout->addWidget(versionInfoLabel);

    // Current version row
    QHBoxLayout* currentVersionLayout = new QHBoxLayout();
    QLabel* currentVersionTextLabel = new QLabel(tr("Current Version"), tab);
    currentVersionTextLabel->setFixedWidth(150);
    m_currentVersionLabel = new QLabel(UpdateChecker::currentVersion(), tab);
    currentVersionLayout->addWidget(currentVersionTextLabel);
    currentVersionLayout->addWidget(m_currentVersionLabel);
    currentVersionLayout->addStretch();
    layout->addLayout(currentVersionLayout);

    layout->addSpacing(16);
    setupUpdatesTabForDirectDownload(layout, tab);
}

void SettingsDialog::setupUpdatesTabForDirectDownload(QVBoxLayout* layout, QWidget* tab)
{
    // ========== Automatic Updates Section ==========
    QLabel* autoUpdateLabel = new QLabel(tr("Automatic Updates"), tab);
    autoUpdateLabel->setStyleSheet("font-weight: bold; font-size: 12px;");
    layout->addWidget(autoUpdateLabel);

    auto& updateSettings = UpdateSettingsManager::instance();

    // Auto-check checkbox
    m_autoCheckUpdatesCheckbox = new QCheckBox(tr("Check for updates automatically"), tab);
    m_autoCheckUpdatesCheckbox->setChecked(updateSettings.isAutoCheckEnabled());
    layout->addWidget(m_autoCheckUpdatesCheckbox);

    // Check frequency row (indented)
    QHBoxLayout* frequencyLayout = new QHBoxLayout();
    frequencyLayout->setContentsMargins(20, 0, 0, 0);
    QLabel* frequencyLabel = new QLabel(tr("Check frequency"), tab);
    frequencyLabel->setFixedWidth(130);
    m_checkFrequencyCombo = new QComboBox(tab);
    m_checkFrequencyCombo->setMinimumWidth(150);
    m_checkFrequencyCombo->addItem(tr("Every day"), 24);
    m_checkFrequencyCombo->addItem(tr("Every 3 days"), 72);
    m_checkFrequencyCombo->addItem(tr("Every week"), 168);
    m_checkFrequencyCombo->addItem(tr("Every 2 weeks"), 336);
    m_checkFrequencyCombo->addItem(tr("Every month"), 720);

    int currentInterval = updateSettings.checkIntervalHours();
    int freqIndex = m_checkFrequencyCombo->findData(currentInterval);
    if (freqIndex >= 0) {
        m_checkFrequencyCombo->setCurrentIndex(freqIndex);
    }

    frequencyLayout->addWidget(frequencyLabel);
    frequencyLayout->addWidget(m_checkFrequencyCombo);
    frequencyLayout->addStretch();
    layout->addLayout(frequencyLayout);

    // Connect auto-check to enable/disable frequency combo
    connect(m_autoCheckUpdatesCheckbox, &QCheckBox::toggled,
            m_checkFrequencyCombo, &QWidget::setEnabled);
    m_checkFrequencyCombo->setEnabled(m_autoCheckUpdatesCheckbox->isChecked());

    layout->addSpacing(16);

    // Separator line
    QFrame* line = new QFrame(tab);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);

    layout->addSpacing(8);

    // Last checked label
    QDateTime lastCheck = updateSettings.lastCheckTime();
    QString lastCheckText;
    if (lastCheck.isValid()) {
        lastCheckText = tr("Last checked: %1")
                            .arg(lastCheck.toString("MMMM d, yyyy 'at' h:mm AP"));
    } else {
        lastCheckText = tr("Last checked: Never");
    }
    m_lastCheckedLabel = new QLabel(lastCheckText, tab);
    m_lastCheckedLabel->setStyleSheet("font-size: 12px; color: gray;");
    m_lastCheckedLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_lastCheckedLabel);

    layout->addSpacing(12);

    // Check Now button (centered)
    QHBoxLayout* checkNowLayout = new QHBoxLayout();
    checkNowLayout->addStretch();
    m_checkNowButton = new QPushButton(tr("Check Now"), tab);
    m_checkNowButton->setMinimumWidth(120);
    connect(m_checkNowButton, &QPushButton::clicked, this, &SettingsDialog::onCheckForUpdates);
    checkNowLayout->addWidget(m_checkNowButton);
    checkNowLayout->addStretch();
    layout->addLayout(checkNowLayout);

    layout->addStretch();

    // Create update checker for manual checks
    m_updateChecker = new UpdateChecker(this);
    connect(m_updateChecker, &UpdateChecker::updateAvailable,
            this, &SettingsDialog::onUpdateAvailable);
    connect(m_updateChecker, &UpdateChecker::noUpdateAvailable,
            this, &SettingsDialog::onNoUpdateAvailable);
    connect(m_updateChecker, &UpdateChecker::checkFailed,
            this, &SettingsDialog::onUpdateCheckFailed);
    connect(m_updateChecker, &UpdateChecker::checkStarted, this, [this]() {
        updateCheckNowButtonState(true);
    });
}

void SettingsDialog::updateCheckNowButtonState(bool checking)
{
    if (m_checkNowButton) {
        m_checkNowButton->setEnabled(!checking);
        if (checking) {
            m_checkNowButton->setText(tr("Checking..."));
        } else {
            m_checkNowButton->setText(tr("Check Now"));
        }
    }
}

void SettingsDialog::onCheckForUpdates()
{
    if (m_updateChecker) {
        m_updateChecker->checkForUpdates(false);  // Non-silent check
    }
}

void SettingsDialog::onUpdateAvailable(const ReleaseInfo& release)
{
    updateCheckNowButtonState(false);

    // Update last checked label
    m_lastCheckedLabel->setText(
        tr("Last checked: %1")
            .arg(QDateTime::currentDateTime().toString("MMMM d, yyyy 'at' h:mm AP")));

    // Show update dialog
    UpdateDialog* dialog = new UpdateDialog(release, this);
    dialog->exec();
}

void SettingsDialog::onNoUpdateAvailable()
{
    updateCheckNowButtonState(false);

    // Update last checked label
    m_lastCheckedLabel->setText(
        tr("Last checked: %1")
            .arg(QDateTime::currentDateTime().toString("MMMM d, yyyy 'at' h:mm AP")));

    // Show up-to-date dialog
    UpdateDialog* dialog = UpdateDialog::createUpToDateDialog(this);
    dialog->exec();
}

void SettingsDialog::onUpdateCheckFailed(const QString& error)
{
    updateCheckNowButtonState(false);

    // Show error dialog
    UpdateDialog* dialog = UpdateDialog::createErrorDialog(error, this);
    connect(dialog, &UpdateDialog::retryRequested, this, &SettingsDialog::onCheckForUpdates);
    dialog->exec();
}

void SettingsDialog::setupAboutTab(QWidget* tab)
{
    QVBoxLayout* layout = new QVBoxLayout(tab);
    layout->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    layout->setSpacing(8);
    layout->setContentsMargins(40, 30, 40, 30);

    // App icon
    QLabel* iconLabel = new QLabel(tab);
    QPixmap iconPixmap(":/icons/icons/snaptray.png");
    iconLabel->setPixmap(iconPixmap.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    iconLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(iconLabel);

    layout->addSpacing(12);

    // App name
    QLabel* nameLabel = new QLabel(SNAPTRAY_APP_NAME, tab);
    nameLabel->setStyleSheet("font-size: 18px; font-weight: bold;");
    nameLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(nameLabel);

    // Version
    QLabel* versionLabel = new QLabel(tr("Version %1").arg(SNAPTRAY_VERSION), tab);
    versionLabel->setStyleSheet(
        QStringLiteral("font-size: 12px; color: %1;").arg(SnapTray::SettingsTheme::secondaryText()));
    versionLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(versionLabel);

    layout->addSpacing(20);

    // Copyright
    QLabel* copyrightLabel = new QLabel(tr("Copyright 2024-2025 Victor Fu"), tab);
    copyrightLabel->setStyleSheet("font-size: 11px;");
    copyrightLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(copyrightLabel);

    layout->addSpacing(12);

    // Author
    QLabel* authorLabel = new QLabel(tr("Author: Victor Fu"), tab);
    authorLabel->setStyleSheet("font-size: 11px;");
    authorLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(authorLabel);

    // Website link
    QLabel* websiteLabel = new QLabel(
        QStringLiteral("<a href=\"https://victorfu.github.io/snap-tray/\" style=\"color: %1;\">https://victorfu.github.io/snap-tray/</a>")
        .arg(SnapTray::SettingsTheme::linkColor()), tab);
    websiteLabel->setStyleSheet("font-size: 11px;");
    websiteLabel->setAlignment(Qt::AlignCenter);
    websiteLabel->setOpenExternalLinks(true);
    layout->addWidget(websiteLabel);

    layout->addStretch();
}

void SettingsDialog::updateCLIStatus()
{
    bool installed = PlatformFeatures::instance().isCLIInstalled();
    if (installed) {
        m_cliStatusLabel->setText(tr("'snaptray' command is available in terminal"));
        m_cliInstallButton->setText(tr("Uninstall CLI"));
    }
    else {
        m_cliStatusLabel->setText(tr("'snaptray' command is not installed"));
        m_cliInstallButton->setText(tr("Install CLI"));
    }
}

#ifdef Q_OS_MAC
void SettingsDialog::updatePermissionStatus()
{
    // Screen Recording permission
    bool hasScreenRecording = PlatformFeatures::hasScreenRecordingPermission();
    if (hasScreenRecording) {
        m_screenRecordingStatusLabel->setText(tr("Granted"));
        m_screenRecordingStatusLabel->setStyleSheet(
            QStringLiteral("color: %1;").arg(SnapTray::SettingsTheme::successColor()));
    }
    else {
        m_screenRecordingStatusLabel->setText(tr("Not Granted"));
        m_screenRecordingStatusLabel->setStyleSheet(
            QStringLiteral("color: %1;").arg(SnapTray::SettingsTheme::errorColor()));
    }

    // Accessibility permission
    bool hasAccessibility = PlatformFeatures::hasAccessibilityPermission();
    if (hasAccessibility) {
        m_accessibilityStatusLabel->setText(tr("Granted"));
        m_accessibilityStatusLabel->setStyleSheet(
            QStringLiteral("color: %1;").arg(SnapTray::SettingsTheme::successColor()));
    }
    else {
        m_accessibilityStatusLabel->setText(tr("Not Granted"));
        m_accessibilityStatusLabel->setStyleSheet(
            QStringLiteral("color: %1;").arg(SnapTray::SettingsTheme::errorColor()));
    }
}
#endif

void SettingsDialog::onCLIButtonClicked()
{
    bool installed = PlatformFeatures::instance().isCLIInstalled();
    bool success = installed
        ? PlatformFeatures::instance().uninstallCLI()
        : PlatformFeatures::instance().installCLI();

    if (success) {
        updateCLIStatus();
    }
}
