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
    , m_watermarkTypeCombo(nullptr)
    , m_watermarkTextLabel(nullptr)
    , m_watermarkTextEdit(nullptr)
    , m_watermarkImageLabel(nullptr)
    , m_watermarkImagePathEdit(nullptr)
    , m_watermarkBrowseBtn(nullptr)
    , m_watermarkScaleRowLabel(nullptr)
    , m_watermarkImageScaleSlider(nullptr)
    , m_watermarkImageScaleLabel(nullptr)
    , m_watermarkOpacitySlider(nullptr)
    , m_watermarkOpacityLabel(nullptr)
    , m_watermarkPositionCombo(nullptr)
    , m_recordingFrameRateCombo(nullptr)
    , m_recordingOutputFormatCombo(nullptr)
    , m_recordingAutoSaveCheckbox(nullptr)
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

    // Enable watermark checkbox
    m_watermarkEnabledCheckbox = new QCheckBox("Enable watermark", tab);
    mainLayout->addWidget(m_watermarkEnabledCheckbox);

    mainLayout->addSpacing(8);

    // Horizontal layout: left controls, right preview
    QHBoxLayout *contentLayout = new QHBoxLayout();

    // === LEFT SIDE: Controls ===
    QVBoxLayout *controlsLayout = new QVBoxLayout();
    controlsLayout->setSpacing(8);

    // Type row
    QHBoxLayout *typeLayout = new QHBoxLayout();
    QLabel *typeLabel = new QLabel("Type:", tab);
    typeLabel->setFixedWidth(60);
    m_watermarkTypeCombo = new QComboBox(tab);
    m_watermarkTypeCombo->addItem("Text", static_cast<int>(WatermarkRenderer::Text));
    m_watermarkTypeCombo->addItem("Image", static_cast<int>(WatermarkRenderer::Image));
    connect(m_watermarkTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::updateWatermarkTypeVisibility);
    typeLayout->addWidget(typeLabel);
    typeLayout->addWidget(m_watermarkTypeCombo);
    typeLayout->addStretch();
    controlsLayout->addLayout(typeLayout);

    // Text input row
    QHBoxLayout *textLayout = new QHBoxLayout();
    m_watermarkTextLabel = new QLabel("Text:", tab);
    m_watermarkTextLabel->setFixedWidth(60);
    m_watermarkTextEdit = new QLineEdit(tab);
    m_watermarkTextEdit->setPlaceholderText("Enter watermark text...");
    textLayout->addWidget(m_watermarkTextLabel);
    textLayout->addWidget(m_watermarkTextEdit);
    controlsLayout->addLayout(textLayout);

    // Image path row
    QHBoxLayout *imageLayout = new QHBoxLayout();
    m_watermarkImageLabel = new QLabel("Image:", tab);
    m_watermarkImageLabel->setFixedWidth(60);
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
    imageLayout->addWidget(m_watermarkImageLabel);
    imageLayout->addWidget(m_watermarkImagePathEdit);
    imageLayout->addWidget(m_watermarkBrowseBtn);
    controlsLayout->addLayout(imageLayout);

    // Image scale row
    QHBoxLayout *scaleLayout = new QHBoxLayout();
    m_watermarkScaleRowLabel = new QLabel("Scale:", tab);
    m_watermarkScaleRowLabel->setFixedWidth(60);
    m_watermarkImageScaleSlider = new QSlider(Qt::Horizontal, tab);
    m_watermarkImageScaleSlider->setRange(10, 200);
    m_watermarkImageScaleSlider->setValue(100);
    m_watermarkImageScaleLabel = new QLabel("100%", tab);
    m_watermarkImageScaleLabel->setFixedWidth(40);
    connect(m_watermarkImageScaleSlider, &QSlider::valueChanged, this, [this](int value) {
        m_watermarkImageScaleLabel->setText(QString("%1%").arg(value));
        updateWatermarkImagePreview();
    });
    scaleLayout->addWidget(m_watermarkScaleRowLabel);
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

    // === RIGHT SIDE: Preview (only for Image type) ===
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
    int typeIndex = m_watermarkTypeCombo->findData(static_cast<int>(settings.type));
    if (typeIndex >= 0) {
        m_watermarkTypeCombo->setCurrentIndex(typeIndex);
    }
    m_watermarkTextEdit->setText(settings.text);
    m_watermarkImagePathEdit->setText(settings.imagePath);
    m_watermarkImageScaleSlider->setValue(settings.imageScale);
    m_watermarkImageScaleLabel->setText(QString("%1%").arg(settings.imageScale));
    m_watermarkOpacitySlider->setValue(static_cast<int>(settings.opacity * 100));
    m_watermarkOpacityLabel->setText(QString("%1%").arg(static_cast<int>(settings.opacity * 100)));
    int posIndex = m_watermarkPositionCombo->findData(static_cast<int>(settings.position));
    if (posIndex >= 0) {
        m_watermarkPositionCombo->setCurrentIndex(posIndex);
    }

    // Update visibility based on current type
    updateWatermarkTypeVisibility(m_watermarkTypeCombo->currentIndex());

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
    m_recordingOutputFormatCombo->addItem("MP4 (H.264 Video)", 0);
    m_recordingOutputFormatCombo->addItem("GIF (Animated Image)", 1);
    formatLayout->addWidget(formatLabel);
    formatLayout->addWidget(m_recordingOutputFormatCombo);
    formatLayout->addStretch();
    layout->addLayout(formatLayout);

    // Auto-save option
    m_recordingAutoSaveCheckbox = new QCheckBox("Auto-save recordings (no save dialog)", tab);
    layout->addWidget(m_recordingAutoSaveCheckbox);

    layout->addSpacing(16);

    // FFmpeg path row
    QHBoxLayout *ffmpegPathLayout = new QHBoxLayout();
    QLabel *ffmpegPathLabel = new QLabel("FFmpeg Path:", tab);
    ffmpegPathLabel->setFixedWidth(120);
    m_ffmpegPathEdit = new QLineEdit(tab);
    m_ffmpegPathEdit->setPlaceholderText("Leave empty for auto-detection");
    m_ffmpegBrowseBtn = new QPushButton("Browse...", tab);
    m_ffmpegBrowseBtn->setFixedWidth(80);
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
    ffmpegPathLayout->addWidget(ffmpegPathLabel);
    ffmpegPathLayout->addWidget(m_ffmpegPathEdit);
    ffmpegPathLayout->addWidget(m_ffmpegBrowseBtn);
    layout->addLayout(ffmpegPathLayout);

    // FFmpeg status
    m_ffmpegStatusLabel = new QLabel(tab);
    layout->addWidget(m_ffmpegStatusLabel);

    layout->addStretch();

    // Load settings
    QSettings settings("Victor Fu", "SnapTray");
    int fps = settings.value("recording/framerate", 30).toInt();
    int fpsIndex = m_recordingFrameRateCombo->findData(fps);
    if (fpsIndex >= 0) {
        m_recordingFrameRateCombo->setCurrentIndex(fpsIndex);
    }
    int outputFormat = settings.value("recording/outputFormat", 0).toInt();
    m_recordingOutputFormatCombo->setCurrentIndex(outputFormat);
    m_recordingAutoSaveCheckbox->setChecked(settings.value("recording/autoSave", false).toBool());

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
}

void SettingsDialog::updateWatermarkTypeVisibility(int index)
{
    Q_UNUSED(index);
    bool isText = (m_watermarkTypeCombo->currentData().toInt() == static_cast<int>(WatermarkRenderer::Text));

    // Show/hide text-specific controls (label + input)
    m_watermarkTextLabel->setVisible(isText);
    m_watermarkTextEdit->setVisible(isText);

    // Show/hide image-specific controls (label + path + browse + preview + scale row)
    m_watermarkImageLabel->setVisible(!isText);
    m_watermarkImagePathEdit->setVisible(!isText);
    m_watermarkBrowseBtn->setVisible(!isText);
    m_watermarkImagePreview->setVisible(!isText);
    m_watermarkImageSizeLabel->setVisible(!isText);
    m_watermarkScaleRowLabel->setVisible(!isText);
    m_watermarkImageScaleSlider->setVisible(!isText);
    m_watermarkImageScaleLabel->setVisible(!isText);
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
    watermarkSettings.type = static_cast<WatermarkRenderer::Type>(
        m_watermarkTypeCombo->currentData().toInt());
    watermarkSettings.text = m_watermarkTextEdit->text();
    watermarkSettings.imagePath = m_watermarkImagePathEdit->text();
    watermarkSettings.opacity = m_watermarkOpacitySlider->value() / 100.0;
    watermarkSettings.position = static_cast<WatermarkRenderer::Position>(
        m_watermarkPositionCombo->currentData().toInt());
    watermarkSettings.imageScale = m_watermarkImageScaleSlider->value();
    WatermarkRenderer::saveSettings(watermarkSettings);

    // Save recording settings
    QSettings recordingSettings("Victor Fu", "SnapTray");
    recordingSettings.setValue("recording/framerate",
        m_recordingFrameRateCombo->currentData().toInt());
    recordingSettings.setValue("recording/outputFormat",
        m_recordingOutputFormatCombo->currentIndex());
    recordingSettings.setValue("recording/autoSave",
        m_recordingAutoSaveCheckbox->isChecked());
    recordingSettings.setValue("recording/ffmpegPath",
        m_ffmpegPathEdit->text());

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

    // Temporarily save the custom path so FFmpegEncoder can use it
    QSettings settings("Victor Fu", "SnapTray");
    settings.setValue("recording/ffmpegPath", customPath);

    if (FFmpegEncoder::isFFmpegAvailable()) {
        m_ffmpegStatusLabel->setText("FFmpeg: Detected");
        m_ffmpegStatusLabel->setStyleSheet("color: green;");
    } else {
        m_ffmpegStatusLabel->setText("FFmpeg: Not found");
        m_ffmpegStatusLabel->setStyleSheet("color: red;");
    }
}
