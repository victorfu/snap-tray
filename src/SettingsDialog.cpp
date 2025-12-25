#include "SettingsDialog.h"
#include "AutoLaunchManager.h"
#include "WatermarkRenderer.h"
#include "FFmpegEncoder.h"

#include <QKeySequenceEdit>
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
#include <QFileDialog>
#include <QStandardPaths>
#include <QFile>

static const char* SETTINGS_KEY_HOTKEY = "hotkey";
static const char* DEFAULT_HOTKEY = "F2";
static const char* SETTINGS_KEY_SCREEN_CANVAS_HOTKEY = "screenCanvasHotkey";
static const char* DEFAULT_SCREEN_CANVAS_HOTKEY = "Ctrl+F2";

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , m_tabWidget(nullptr)
    , m_startOnLoginCheckbox(nullptr)
    , m_hotkeyEdit(nullptr)
    , m_captureHotkeyStatus(nullptr)
    , m_screenCanvasHotkeyEdit(nullptr)
    , m_screenCanvasHotkeyStatus(nullptr)
    , m_restoreDefaultsBtn(nullptr)
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
    , m_recordingAutoSaveCheckbox(nullptr)
    , m_mp4SettingsWidget(nullptr)
    , m_recordingQualitySlider(nullptr)
    , m_recordingQualityLabel(nullptr)
    , m_gifSettingsWidget(nullptr)
    , m_gifWarningLabel(nullptr)
    , m_ffmpegPathEdit(nullptr)
    , m_ffmpegBrowseBtn(nullptr)
    , m_ffmpegStatusLabel(nullptr)
{
    setWindowTitle("SnapTray Settings");
    setMinimumSize(520, 360);
    setupUi();

    connect(this, &QDialog::accepted, this, &SettingsDialog::onAccepted);
}

SettingsDialog::~SettingsDialog()
{
}

void SettingsDialog::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // Create tab widget
    m_tabWidget = new QTabWidget(this);

    // Tab 1: General
    QWidget *generalTab = new QWidget();
    setupGeneralTab(generalTab);
    m_tabWidget->addTab(generalTab, "General");

    // Tab 2: Hotkeys
    QWidget *hotkeysTab = new QWidget();
    setupHotkeysTab(hotkeysTab);
    m_tabWidget->addTab(hotkeysTab, "Hotkeys");

    // Tab 3: Watermark
    QWidget *watermarkTab = new QWidget();
    setupWatermarkTab(watermarkTab);
    m_tabWidget->addTab(watermarkTab, "Watermark");

    // Tab 4: Recording
    QWidget *recordingTab = new QWidget();
    setupRecordingTab(recordingTab);
    m_tabWidget->addTab(recordingTab, "Recording");

    mainLayout->addWidget(m_tabWidget);

    // Buttons row (outside tabs)
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    QPushButton *saveButton = new QPushButton("Save", this);
    QPushButton *cancelButton = new QPushButton("Cancel", this);

    connect(saveButton, &QPushButton::clicked, this, &SettingsDialog::onSave);
    connect(cancelButton, &QPushButton::clicked, this, &SettingsDialog::onCancel);

    buttonLayout->addWidget(saveButton);
    buttonLayout->addWidget(cancelButton);
    mainLayout->addLayout(buttonLayout);
}

void SettingsDialog::setupGeneralTab(QWidget *tab)
{
    QVBoxLayout *layout = new QVBoxLayout(tab);

    m_startOnLoginCheckbox = new QCheckBox("Start on login", tab);
    m_startOnLoginCheckbox->setChecked(AutoLaunchManager::isEnabled());
    layout->addWidget(m_startOnLoginCheckbox);

    layout->addStretch();
}

void SettingsDialog::setupHotkeysTab(QWidget *tab)
{
    QVBoxLayout *layout = new QVBoxLayout(tab);

    // Region Capture hotkey row
    QHBoxLayout *captureLayout = new QHBoxLayout();
    QLabel *captureLabel = new QLabel("Region Capture:", tab);
    captureLabel->setFixedWidth(120);
    m_hotkeyEdit = new QKeySequenceEdit(tab);
    m_hotkeyEdit->setKeySequence(QKeySequence(loadHotkey()));
    m_captureHotkeyStatus = new QLabel(tab);
    m_captureHotkeyStatus->setFixedSize(24, 24);
    m_captureHotkeyStatus->setAlignment(Qt::AlignCenter);
    captureLayout->addWidget(captureLabel);
    captureLayout->addWidget(m_hotkeyEdit);
    captureLayout->addWidget(m_captureHotkeyStatus);
    layout->addLayout(captureLayout);

    // Screen Canvas hotkey row
    QHBoxLayout *canvasLayout = new QHBoxLayout();
    QLabel *canvasLabel = new QLabel("Screen Canvas:", tab);
    canvasLabel->setFixedWidth(120);
    m_screenCanvasHotkeyEdit = new QKeySequenceEdit(tab);
    m_screenCanvasHotkeyEdit->setKeySequence(QKeySequence(loadScreenCanvasHotkey()));
    m_screenCanvasHotkeyStatus = new QLabel(tab);
    m_screenCanvasHotkeyStatus->setFixedSize(24, 24);
    m_screenCanvasHotkeyStatus->setAlignment(Qt::AlignCenter);
    canvasLayout->addWidget(canvasLabel);
    canvasLayout->addWidget(m_screenCanvasHotkeyEdit);
    canvasLayout->addWidget(m_screenCanvasHotkeyStatus);
    layout->addLayout(canvasLayout);

    layout->addStretch();

    // Restore Defaults button
    QHBoxLayout *defaultsLayout = new QHBoxLayout();
    defaultsLayout->addStretch();
    m_restoreDefaultsBtn = new QPushButton("Restore Defaults", tab);
    connect(m_restoreDefaultsBtn, &QPushButton::clicked,
            this, &SettingsDialog::onRestoreDefaults);
    defaultsLayout->addWidget(m_restoreDefaultsBtn);
    layout->addLayout(defaultsLayout);
}

void SettingsDialog::setupWatermarkTab(QWidget *tab)
{
    QVBoxLayout *mainLayout = new QVBoxLayout(tab);
    mainLayout->setContentsMargins(9, 12, 9, 9);  // Increase top margin for checkbox

    // Enable watermark checkbox
    m_watermarkEnabledCheckbox = new QCheckBox("Enable watermark", tab);
    mainLayout->addWidget(m_watermarkEnabledCheckbox);

    mainLayout->addSpacing(8);

    // Horizontal layout: left controls, right preview
    QHBoxLayout *contentLayout = new QHBoxLayout();

    // === LEFT SIDE: Controls ===
    QVBoxLayout *controlsLayout = new QVBoxLayout();
    controlsLayout->setSpacing(8);

    // Image path row
    QHBoxLayout *imageLayout = new QHBoxLayout();
    QLabel *imageLabel = new QLabel("Image:", tab);
    imageLabel->setFixedWidth(60);
    m_watermarkImagePathEdit = new QLineEdit(tab);
    m_watermarkImagePathEdit->setPlaceholderText("Select an image file...");
    m_watermarkImagePathEdit->setReadOnly(true);
    m_watermarkBrowseBtn = new QPushButton("Browse...", tab);
    m_watermarkBrowseBtn->setFixedWidth(80);
    connect(m_watermarkBrowseBtn, &QPushButton::clicked, this, [this]() {
        QString filePath = QFileDialog::getOpenFileName(this, "Select Watermark Image",
            QString(), "Images (*.png *.jpg *.jpeg *.bmp *.gif *.svg);;All Files (*)");
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
    QHBoxLayout *scaleLayout = new QHBoxLayout();
    QLabel *scaleLabel = new QLabel("Scale:", tab);
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
    QHBoxLayout *opacityLayout = new QHBoxLayout();
    QLabel *opacityLabel = new QLabel("Opacity:", tab);
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
    QHBoxLayout *marginLayout = new QHBoxLayout();
    QLabel *marginLabel = new QLabel("Margin:", tab);
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
    QHBoxLayout *positionLayout = new QHBoxLayout();
    QLabel *positionLabel = new QLabel("Position:", tab);
    positionLabel->setFixedWidth(60);
    m_watermarkPositionCombo = new QComboBox(tab);
    m_watermarkPositionCombo->addItem("Top-Left", static_cast<int>(WatermarkRenderer::TopLeft));
    m_watermarkPositionCombo->addItem("Top-Right", static_cast<int>(WatermarkRenderer::TopRight));
    m_watermarkPositionCombo->addItem("Bottom-Left", static_cast<int>(WatermarkRenderer::BottomLeft));
    m_watermarkPositionCombo->addItem("Bottom-Right", static_cast<int>(WatermarkRenderer::BottomRight));
    m_watermarkPositionCombo->setCurrentIndex(3); // Default to Bottom-Right
    positionLayout->addWidget(positionLabel);
    positionLayout->addWidget(m_watermarkPositionCombo);
    positionLayout->addStretch();
    controlsLayout->addLayout(positionLayout);

    controlsLayout->addStretch();
    contentLayout->addLayout(controlsLayout, 1);

    // === RIGHT SIDE: Preview ===
    QVBoxLayout *previewLayout = new QVBoxLayout();
    previewLayout->setAlignment(Qt::AlignTop);

    m_watermarkImagePreview = new QLabel(tab);
    m_watermarkImagePreview->setFixedSize(120, 120);
    m_watermarkImagePreview->setAlignment(Qt::AlignCenter);
    m_watermarkImagePreview->setStyleSheet(
        "QLabel { border: 1px solid #ccc; background-color: #f5f5f5; border-radius: 4px; }");
    m_watermarkImagePreview->setText("No image");

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
    WatermarkRenderer::Settings settings = WatermarkRenderer::loadSettings();
    m_watermarkEnabledCheckbox->setChecked(settings.enabled);
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

void SettingsDialog::setupRecordingTab(QWidget *tab)
{
    QVBoxLayout *layout = new QVBoxLayout(tab);

    // Frame Rate row
    QHBoxLayout *fpsLayout = new QHBoxLayout();
    QLabel *fpsLabel = new QLabel("Frame Rate:", tab);
    fpsLabel->setFixedWidth(120);
    m_recordingFrameRateCombo = new QComboBox(tab);
    m_recordingFrameRateCombo->addItem("10 FPS", 10);
    m_recordingFrameRateCombo->addItem("15 FPS", 15);
    m_recordingFrameRateCombo->addItem("24 FPS", 24);
    m_recordingFrameRateCombo->addItem("30 FPS", 30);
    m_recordingFrameRateCombo->setCurrentIndex(3);  // Default 30 FPS
    fpsLayout->addWidget(fpsLabel);
    fpsLayout->addWidget(m_recordingFrameRateCombo);
    fpsLayout->addStretch();
    layout->addLayout(fpsLayout);

    // Output Format row
    QHBoxLayout *formatLayout = new QHBoxLayout();
    QLabel *formatLabel = new QLabel("Output Format:", tab);
    formatLabel->setFixedWidth(120);
    m_recordingOutputFormatCombo = new QComboBox(tab);
    m_recordingOutputFormatCombo->addItem("MP4 (H.264) - No dependencies", 0);
    m_recordingOutputFormatCombo->addItem("GIF (Requires FFmpeg)", 1);
    formatLayout->addWidget(formatLabel);
    formatLayout->addWidget(m_recordingOutputFormatCombo);
    formatLayout->addStretch();
    layout->addLayout(formatLayout);

    // ========== MP4 Settings (visible when MP4 selected) ==========
    m_mp4SettingsWidget = new QWidget(tab);
    QVBoxLayout *mp4Layout = new QVBoxLayout(m_mp4SettingsWidget);
    mp4Layout->setContentsMargins(0, 8, 0, 0);

    // Quality slider
    QHBoxLayout *qualityLayout = new QHBoxLayout();
    QLabel *qualityLabel = new QLabel("Quality:", m_mp4SettingsWidget);
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

    // Quality hint
    QLabel *qualityHint = new QLabel("Higher = better quality, larger file", m_mp4SettingsWidget);
    qualityHint->setStyleSheet("color: gray; font-size: 11px;");
    qualityHint->setContentsMargins(120, 0, 0, 0);
    mp4Layout->addWidget(qualityHint);

    layout->addWidget(m_mp4SettingsWidget);

    // ========== GIF Settings (visible when GIF selected) ==========
    m_gifSettingsWidget = new QWidget(tab);
    QVBoxLayout *gifLayout = new QVBoxLayout(m_gifSettingsWidget);
    gifLayout->setContentsMargins(0, 8, 0, 0);

    // Warning label
    m_gifWarningLabel = new QLabel(m_gifSettingsWidget);
    m_gifWarningLabel->setWordWrap(true);
    gifLayout->addWidget(m_gifWarningLabel);

    // FFmpeg path row
    QHBoxLayout *ffmpegPathLayout = new QHBoxLayout();
    QLabel *ffmpegPathLabel = new QLabel("FFmpeg Path:", m_gifSettingsWidget);
    ffmpegPathLabel->setFixedWidth(120);
    m_ffmpegPathEdit = new QLineEdit(m_gifSettingsWidget);
    m_ffmpegPathEdit->setPlaceholderText("Leave empty for auto-detection");
    m_ffmpegBrowseBtn = new QPushButton("Browse...", m_gifSettingsWidget);
    m_ffmpegBrowseBtn->setFixedWidth(80);
    ffmpegPathLayout->addWidget(ffmpegPathLabel);
    ffmpegPathLayout->addWidget(m_ffmpegPathEdit);
    ffmpegPathLayout->addWidget(m_ffmpegBrowseBtn);
    gifLayout->addLayout(ffmpegPathLayout);

    // FFmpeg status
    m_ffmpegStatusLabel = new QLabel(m_gifSettingsWidget);
    gifLayout->addWidget(m_ffmpegStatusLabel);

    // Connect signals AFTER all UI elements are created
    connect(m_ffmpegBrowseBtn, &QPushButton::clicked, this, [this]() {
        QString filter = "FFmpeg Executable (ffmpeg.exe ffmpeg);;All Files (*)";
#ifdef Q_OS_WIN
        filter = "FFmpeg Executable (ffmpeg.exe);;All Files (*)";
#endif
        QString path = QFileDialog::getOpenFileName(this, "Select FFmpeg Executable", QString(), filter);
        if (!path.isEmpty()) {
            m_ffmpegPathEdit->setText(path);
            updateFFmpegStatus();
        }
    });
    connect(m_ffmpegPathEdit, &QLineEdit::textChanged, this, &SettingsDialog::updateFFmpegStatus);

    layout->addWidget(m_gifSettingsWidget);

    // ========== Audio Settings Section ==========
    layout->addSpacing(16);
    QLabel *audioSectionLabel = new QLabel("Audio", tab);
    audioSectionLabel->setStyleSheet("font-weight: bold; font-size: 12px;");
    layout->addWidget(audioSectionLabel);

    // Enable audio checkbox
    m_audioEnabledCheckbox = new QCheckBox("Record audio", tab);
    layout->addWidget(m_audioEnabledCheckbox);

    // Audio source dropdown
    QHBoxLayout *audioSourceLayout = new QHBoxLayout();
    QLabel *audioSourceLabel = new QLabel("Source:", tab);
    audioSourceLabel->setFixedWidth(120);
    m_audioSourceCombo = new QComboBox(tab);
    m_audioSourceCombo->addItem("Microphone", 0);
    m_audioSourceCombo->addItem("System Audio", 1);
    m_audioSourceCombo->addItem("Both (Mixed)", 2);
    audioSourceLayout->addWidget(audioSourceLabel);
    audioSourceLayout->addWidget(m_audioSourceCombo);
    audioSourceLayout->addStretch();
    layout->addLayout(audioSourceLayout);

    // Audio device dropdown
    QHBoxLayout *audioDeviceLayout = new QHBoxLayout();
    QLabel *audioDeviceLabel = new QLabel("Device:", tab);
    audioDeviceLabel->setFixedWidth(120);
    m_audioDeviceCombo = new QComboBox(tab);
    m_audioDeviceCombo->addItem("Default", QString());
    audioDeviceLayout->addWidget(audioDeviceLabel);
    audioDeviceLayout->addWidget(m_audioDeviceCombo);
    audioDeviceLayout->addStretch();
    layout->addLayout(audioDeviceLayout);

    // System audio warning label
    m_systemAudioWarningLabel = new QLabel(tab);
    m_systemAudioWarningLabel->setWordWrap(true);
    m_systemAudioWarningLabel->setStyleSheet(
        "QLabel { background-color: #fff3cd; color: #856404; "
        "padding: 8px; border-radius: 4px; font-size: 11px; }");
    m_systemAudioWarningLabel->hide();
    layout->addWidget(m_systemAudioWarningLabel);

    // Connect audio UI signals
    connect(m_audioEnabledCheckbox, &QCheckBox::toggled, this, [this](bool checked) {
        m_audioSourceCombo->setEnabled(checked);
        m_audioDeviceCombo->setEnabled(checked);
        if (checked) {
            onAudioSourceChanged(m_audioSourceCombo->currentIndex());
        } else {
            m_systemAudioWarningLabel->hide();
        }
    });
    connect(m_audioSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::onAudioSourceChanged);

    // ========== Auto-save option ==========
    layout->addSpacing(16);
    m_recordingAutoSaveCheckbox = new QCheckBox("Auto-save recordings (no save dialog)", tab);
    layout->addWidget(m_recordingAutoSaveCheckbox);

    layout->addStretch();

    // ========== Connect format change to show/hide widgets ==========
    connect(m_recordingOutputFormatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::onOutputFormatChanged);

    // ========== Load settings ==========
    QSettings settings("Victor Fu", "SnapTray");

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

    m_recordingAutoSaveCheckbox->setChecked(settings.value("recording/autoSave", false).toBool());

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

    // Populate audio devices (will be updated when audio engine is available)
    populateAudioDevices();

    // Load FFmpeg path: use saved path, or auto-detect if not saved
    QString savedFfmpegPath = settings.value("recording/ffmpegPath").toString();
    if (savedFfmpegPath.isEmpty()) {
        // Auto-detect and display (but don't save yet)
        QString autoDetected = FFmpegEncoder::ffmpegPath();
        if (autoDetected != "ffmpeg" && QFile::exists(autoDetected)) {
            m_ffmpegPathEdit->setText(autoDetected);
        }
    } else {
        m_ffmpegPathEdit->setText(savedFfmpegPath);
    }

    updateFFmpegStatus();
    onOutputFormatChanged(outputFormat);  // Initial visibility
}

void SettingsDialog::onOutputFormatChanged(int index)
{
    bool isGif = (index == 1);

    m_mp4SettingsWidget->setVisible(!isGif);
    m_gifSettingsWidget->setVisible(isGif);

    if (isGif) {
        updateFFmpegStatus();
    }
}

void SettingsDialog::updateHotkeyStatus(QLabel *statusLabel, bool isRegistered)
{
    if (isRegistered) {
        statusLabel->setText("✓");
        statusLabel->setStyleSheet("color: green; font-weight: bold; font-size: 16px;");
    } else {
        statusLabel->setText("✗");
        statusLabel->setStyleSheet("color: red; font-weight: bold; font-size: 16px;");
    }
}

void SettingsDialog::updateCaptureHotkeyStatus(bool isRegistered)
{
    updateHotkeyStatus(m_captureHotkeyStatus, isRegistered);
}

void SettingsDialog::onRestoreDefaults()
{
    m_hotkeyEdit->setKeySequence(QKeySequence(DEFAULT_HOTKEY));
    m_screenCanvasHotkeyEdit->setKeySequence(QKeySequence(DEFAULT_SCREEN_CANVAS_HOTKEY));
}

QString SettingsDialog::defaultHotkey()
{
    return QString(DEFAULT_HOTKEY);
}

QString SettingsDialog::loadHotkey()
{
    QSettings settings("Victor Fu", "SnapTray");
    return settings.value(SETTINGS_KEY_HOTKEY, DEFAULT_HOTKEY).toString();
}

void SettingsDialog::saveHotkey(const QString &keySequence)
{
    QSettings settings("Victor Fu", "SnapTray");
    settings.setValue(SETTINGS_KEY_HOTKEY, keySequence);
    qDebug() << "Hotkey saved:" << keySequence;
}

QString SettingsDialog::defaultScreenCanvasHotkey()
{
    return QString(DEFAULT_SCREEN_CANVAS_HOTKEY);
}

QString SettingsDialog::loadScreenCanvasHotkey()
{
    QSettings settings("Victor Fu", "SnapTray");
    return settings.value(SETTINGS_KEY_SCREEN_CANVAS_HOTKEY, DEFAULT_SCREEN_CANVAS_HOTKEY).toString();
}

void SettingsDialog::updateScreenCanvasHotkeyStatus(bool isRegistered)
{
    updateHotkeyStatus(m_screenCanvasHotkeyStatus, isRegistered);
}

void SettingsDialog::onSave()
{
    QString newHotkey = m_hotkeyEdit->keySequence().toString();
    QString newScreenCanvasHotkey = m_screenCanvasHotkeyEdit->keySequence().toString();

    // Validate hotkeys are not empty
    if (newHotkey.isEmpty()) {
        QMessageBox::warning(this, "Invalid Hotkey",
            "Capture hotkey cannot be empty. Please set a valid key combination.");
        return;
    }

    if (newScreenCanvasHotkey.isEmpty()) {
        QMessageBox::warning(this, "Invalid Hotkey",
            "Screen Canvas hotkey cannot be empty. Please set a valid key combination.");
        return;
    }

    // Apply start-on-login setting immediately (independent of hotkey registration)
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
    watermarkSettings.imagePath = m_watermarkImagePathEdit->text();
    watermarkSettings.opacity = m_watermarkOpacitySlider->value() / 100.0;
    watermarkSettings.position = static_cast<WatermarkRenderer::Position>(
        m_watermarkPositionCombo->currentData().toInt());
    watermarkSettings.imageScale = m_watermarkImageScaleSlider->value();
    watermarkSettings.margin = m_watermarkMarginSlider->value();
    WatermarkRenderer::saveSettings(watermarkSettings);

    // Save recording settings
    QSettings recordingSettings("Victor Fu", "SnapTray");
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
    recordingSettings.setValue("recording/autoSave",
        m_recordingAutoSaveCheckbox->isChecked());
    recordingSettings.setValue("recording/ffmpegPath",
        m_ffmpegPathEdit->text());

    // Save audio settings
    recordingSettings.setValue("recording/audioEnabled",
        m_audioEnabledCheckbox->isChecked());
    recordingSettings.setValue("recording/audioSource",
        m_audioSourceCombo->currentData().toInt());
    recordingSettings.setValue("recording/audioDevice",
        m_audioDeviceCombo->currentData().toString());

    // Request hotkey change (MainApplication handles registration)
    emit hotkeyChangeRequested(newHotkey);
    emit screenCanvasHotkeyChangeRequested(newScreenCanvasHotkey);

    // Close dialog (non-modal mode)
    accept();
}

void SettingsDialog::showHotkeyError(const QString &message)
{
    QMessageBox::warning(this, "Hotkey Registration Failed", message);
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
        m_watermarkImagePreview->setText("No image");
        m_watermarkImageSizeLabel->clear();
        m_watermarkOriginalSize = QSize();
        return;
    }

    QPixmap pixmap(imagePath);
    if (pixmap.isNull()) {
        m_watermarkImagePreview->clear();
        m_watermarkImagePreview->setText("Invalid image");
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
        QString("Size: %1 × %2 px").arg(scaledWidth).arg(scaledHeight));
}

void SettingsDialog::updateFFmpegStatus()
{
    QString customPath = m_ffmpegPathEdit->text().trimmed();

    // Validate FFmpeg without saving to settings (only save on Save button click)
    bool available = false;

    if (!customPath.isEmpty() && QFile::exists(customPath)) {
        // Validate if it's a working FFmpeg executable
        QProcess probe;
        probe.start(customPath, {"-version"});
        available = probe.waitForFinished(3000) && probe.exitCode() == 0;
    } else if (customPath.isEmpty()) {
        // When path is empty, check if system has FFmpeg available
        available = FFmpegEncoder::isFFmpegAvailable();
    }

    if (available) {
        m_ffmpegStatusLabel->setText("FFmpeg: Detected");
        m_ffmpegStatusLabel->setStyleSheet("color: green;");
        m_gifWarningLabel->setText("GIF format is enabled. Files will be larger than MP4 with lower quality.");
        m_gifWarningLabel->setStyleSheet(
            "QLabel { background-color: #d4edda; color: #155724; "
            "padding: 8px; border-radius: 4px; }");
    } else {
        m_ffmpegStatusLabel->setText("FFmpeg: Not found");
        m_ffmpegStatusLabel->setStyleSheet("color: red;");
        m_gifWarningLabel->setText(
            "FFmpeg is required for GIF recording.\n"
            "Please install FFmpeg and ensure it's in your PATH, or specify the path below.");
        m_gifWarningLabel->setStyleSheet(
            "QLabel { background-color: #fff3cd; color: #856404; "
            "padding: 8px; border-radius: 4px; }");
    }
}

void SettingsDialog::populateAudioDevices()
{
    // Save current selection
    QString currentDevice = m_audioDeviceCombo->currentData().toString();

    m_audioDeviceCombo->clear();
    m_audioDeviceCombo->addItem("Default", QString());

    // Audio devices will be populated when IAudioCaptureEngine is available
    // For now, just show the default option
    // TODO: Once IAudioCaptureEngine is implemented, enumerate devices here

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

    if (needsSystemAudio) {
#ifdef Q_OS_MAC
        // macOS system audio requires ScreenCaptureKit (macOS 13+) or virtual audio device
        // We'll check availability at runtime when audio engine is created
        m_systemAudioWarningLabel->setText(
            "System audio capture requires macOS 13 (Ventura) or later, "
            "or a virtual audio device like BlackHole.");
        m_systemAudioWarningLabel->show();
#else
        // Windows supports system audio via WASAPI loopback
        m_systemAudioWarningLabel->hide();
#endif
    } else {
        m_systemAudioWarningLabel->hide();
    }

    // Device combo is only relevant for microphone
    bool showDeviceCombo = (index == 0 || index == 2);  // Microphone or Both
    m_audioDeviceCombo->setVisible(showDeviceCombo);
}
