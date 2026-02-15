#include "beautify/BeautifyPanel.h"
#include "beautify/BeautifyRenderer.h"
#include "GlassRenderer.h"
#include "ToolbarStyle.h"
#include "utils/CoordinateHelper.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSlider>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QPainter>
#include <QColorDialog>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTimer>
#include <QShowEvent>

BeautifyPanel::BeautifyPanel(QWidget* parent)
    : QWidget(parent)
{
    setFixedWidth(PANEL_WIDTH);
    setAttribute(Qt::WA_TranslucentBackground);

    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(50);
    connect(m_previewTimer, &QTimer::timeout, this, &BeautifyPanel::updatePreview);

    setupUI();
}

void BeautifyPanel::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);

    // Preview area
    m_previewLabel = new QLabel(this);
    m_previewLabel->setFixedHeight(PREVIEW_HEIGHT);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setStyleSheet("background: transparent;");
    mainLayout->addWidget(m_previewLabel);

    // Presets row
    auto* presetLayout = new QHBoxLayout();
    presetLayout->setSpacing(4);
    const auto presets = beautifyPresets();
    const bool isDarkPresetStyle = (ToolbarStyleConfig::loadStyle() == ToolbarStyleType::Dark);
    for (int i = 0; i < presets.size(); ++i) {
        auto* btn = new QPushButton(this);
        btn->setFixedSize(24, 24);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setToolTip(presets[i].name);

        // Style as a colored circle — no border by default, subtle border on hover
        QColor c = presets[i].gradientStart;
        const bool isDarkStyle = isDarkPresetStyle;
        QString borderHover = isDarkStyle ? QStringLiteral("rgba(255,255,255,120)")
                                          : QStringLiteral("rgba(0,0,0,60)");
        btn->setStyleSheet(
            QString("QPushButton { background: %1; border: none; "
                    "border-radius: 12px; } "
                    "QPushButton:hover { border: 2px solid %2; }")
                .arg(c.name(), borderHover));

        connect(btn, &QPushButton::clicked, this, [this, i]() { loadPreset(i); });
        presetLayout->addWidget(btn);
        m_presetButtons.append(btn);
    }
    presetLayout->addStretch();
    mainLayout->addLayout(presetLayout);

    // Background type
    auto* bgLayout = new QHBoxLayout();
    bgLayout->addWidget(new QLabel("Background:", this));
    m_backgroundTypeCombo = new QComboBox(this);
    m_backgroundTypeCombo->addItem("Solid");
    m_backgroundTypeCombo->addItem("Linear Gradient");
    m_backgroundTypeCombo->addItem("Radial Gradient");
    bgLayout->addWidget(m_backgroundTypeCombo);
    mainLayout->addLayout(bgLayout);

    // Gradient colors
    auto* colorLayout = new QHBoxLayout();
    colorLayout->addWidget(new QLabel("Colors:", this));
    m_gradientStartBtn = new QPushButton(this);
    m_gradientStartBtn->setFixedSize(28, 28);
    m_gradientStartBtn->setCursor(Qt::PointingHandCursor);
    colorLayout->addWidget(m_gradientStartBtn);
    m_gradientEndBtn = new QPushButton(this);
    m_gradientEndBtn->setFixedSize(28, 28);
    m_gradientEndBtn->setCursor(Qt::PointingHandCursor);
    colorLayout->addWidget(m_gradientEndBtn);
    colorLayout->addStretch();
    mainLayout->addLayout(colorLayout);

    // Padding slider
    auto* paddingLayout = new QHBoxLayout();
    paddingLayout->addWidget(new QLabel("Padding:", this));
    m_paddingSlider = new QSlider(Qt::Horizontal, this);
    m_paddingSlider->setRange(16, 200);
    paddingLayout->addWidget(m_paddingSlider);
    m_paddingLabel = new QLabel("64px", this);
    m_paddingLabel->setFixedWidth(40);
    paddingLayout->addWidget(m_paddingLabel);
    mainLayout->addLayout(paddingLayout);

    // Corner radius slider
    auto* cornerLayout = new QHBoxLayout();
    cornerLayout->addWidget(new QLabel("Corners:", this));
    m_cornerRadiusSlider = new QSlider(Qt::Horizontal, this);
    m_cornerRadiusSlider->setRange(0, 40);
    cornerLayout->addWidget(m_cornerRadiusSlider);
    m_cornerRadiusLabel = new QLabel("12px", this);
    m_cornerRadiusLabel->setFixedWidth(40);
    cornerLayout->addWidget(m_cornerRadiusLabel);
    mainLayout->addLayout(cornerLayout);

    // Aspect ratio
    auto* ratioLayout = new QHBoxLayout();
    ratioLayout->addWidget(new QLabel("Ratio:", this));
    m_aspectRatioCombo = new QComboBox(this);
    m_aspectRatioCombo->addItem("Auto");
    m_aspectRatioCombo->addItem("1:1");
    m_aspectRatioCombo->addItem("16:9");
    m_aspectRatioCombo->addItem("4:3");
    m_aspectRatioCombo->addItem("9:16");
    m_aspectRatioCombo->addItem("2:1");
    ratioLayout->addWidget(m_aspectRatioCombo);
    mainLayout->addLayout(ratioLayout);

    // Shadow section
    m_shadowCheckbox = new QCheckBox("Shadow", this);
    mainLayout->addWidget(m_shadowCheckbox);

    auto* shadowBlurLayout = new QHBoxLayout();
    shadowBlurLayout->addWidget(new QLabel("  Blur:", this));
    m_shadowBlurSlider = new QSlider(Qt::Horizontal, this);
    m_shadowBlurSlider->setRange(0, 100);
    shadowBlurLayout->addWidget(m_shadowBlurSlider);
    m_shadowBlurLabel = new QLabel("40px", this);
    m_shadowBlurLabel->setFixedWidth(40);
    shadowBlurLayout->addWidget(m_shadowBlurLabel);
    mainLayout->addLayout(shadowBlurLayout);

    // Action buttons
    auto* actionLayout = new QHBoxLayout();
    m_copyBtn = new QPushButton("Copy", this);
    m_saveBtn = new QPushButton("Save", this);
    m_closeBtn = new QPushButton("Close", this);
    actionLayout->setSpacing(8);
    actionLayout->addWidget(m_copyBtn);
    actionLayout->addWidget(m_saveBtn);
    actionLayout->addWidget(m_closeBtn);
    mainLayout->addLayout(actionLayout);

    // Connect signals
    connect(m_backgroundTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                m_settings.backgroundType = static_cast<BeautifyBackgroundType>(index);
                bool isSolid = (m_settings.backgroundType == BeautifyBackgroundType::Solid);
                m_gradientEndBtn->setVisible(!isSolid);
                onSettingChanged();
            });

    connect(m_gradientStartBtn, &QPushButton::clicked, this, [this]() {
        if (m_settings.backgroundType == BeautifyBackgroundType::Solid) {
            pickColor(m_settings.backgroundColor);
        } else {
            pickColor(m_settings.gradientStartColor);
        }
    });

    connect(m_gradientEndBtn, &QPushButton::clicked, this, [this]() {
        pickColor(m_settings.gradientEndColor);
    });

    connect(m_paddingSlider, &QSlider::valueChanged, this, [this](int value) {
        m_settings.padding = value;
        m_paddingLabel->setText(QString("%1px").arg(value));
        onSettingChanged();
    });

    connect(m_cornerRadiusSlider, &QSlider::valueChanged, this, [this](int value) {
        m_settings.cornerRadius = value;
        m_cornerRadiusLabel->setText(QString("%1px").arg(value));
        onSettingChanged();
    });

    connect(m_aspectRatioCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                m_settings.aspectRatio = static_cast<BeautifyAspectRatio>(index);
                onSettingChanged();
            });

    connect(m_shadowCheckbox, &QCheckBox::toggled, this, [this](bool checked) {
        m_settings.shadowEnabled = checked;
        m_shadowBlurSlider->setEnabled(checked);
        onSettingChanged();
    });

    connect(m_shadowBlurSlider, &QSlider::valueChanged, this, [this](int value) {
        m_settings.shadowBlur = value;
        m_shadowBlurLabel->setText(QString("%1px").arg(value));
        onSettingChanged();
    });

    connect(m_copyBtn, &QPushButton::clicked, this, [this]() {
        emit copyRequested(m_settings);
    });

    connect(m_saveBtn, &QPushButton::clicked, this, [this]() {
        emit saveRequested(m_settings);
    });

    connect(m_closeBtn, &QPushButton::clicked, this, [this]() {
        emit closeRequested();
    });

    // Apply theme-aware styles to all controls
    applyThemeStyles();

    // Apply initial settings to controls
    setSettings(m_settings);
}

void BeautifyPanel::applyThemeStyles()
{
    // Use ToolbarStyleConfig colors to match the glass panel background.
    // SettingsTheme uses system palette which may not match the glass panel appearance.
    const auto style = ToolbarStyleConfig::loadStyle();
    const auto& config = ToolbarStyleConfig::getStyle(style);
    const bool isDarkStyle = (style == ToolbarStyleType::Dark);
    const QString textColor = config.textColor.name();

    // Labels: use toolbar text color for readability on glass
    const QString labelStyle = QStringLiteral(
        "QLabel { font-size: 13px; color: %1; }").arg(textColor);
    const auto labels = findChildren<QLabel*>();
    for (auto* label : labels) {
        if (label != m_previewLabel) {
            label->setStyleSheet(labelStyle);
        }
    }

    // QComboBox: styled to match glass panel
    const QString comboBg = config.buttonInactiveColor.name();
    const QString comboBorder = config.dropdownBorder.name();
    const QString comboHover = config.buttonHoverColor.name();
    const QString dropdownBg = config.dropdownBackground.name(QColor::HexArgb);
    const QString selectionBg = config.buttonActiveColor.name();
    const QString comboStyle = QStringLiteral(
        "QComboBox {"
        "  background-color: %1;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  border-radius: 4px;"
        "  padding: 4px 8px;"
        "  font-size: 13px;"
        "}"
        "QComboBox:hover {"
        "  background-color: %4;"
        "}"
        "QComboBox::drop-down {"
        "  border: none;"
        "}"
        "QComboBox QAbstractItemView {"
        "  background-color: %5;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  selection-background-color: %6;"
        "  selection-color: white;"
        "}")
        .arg(comboBg, textColor, comboBorder, comboHover, dropdownBg, selectionBg);
    m_backgroundTypeCombo->setStyleSheet(comboStyle);
    m_aspectRatioCombo->setStyleSheet(comboStyle);

    // QSlider: visible groove and handle on glass
    const QString grooveColor = config.separatorColor.name();
    const QString handleColor = isDarkStyle ? QStringLiteral("#FFFFFF") : QStringLiteral("#333333");
    const QString sliderStyle = QStringLiteral(
        "QSlider::groove:horizontal {"
        "  background: %1;"
        "  height: 4px;"
        "  border-radius: 2px;"
        "}"
        "QSlider::sub-page:horizontal {"
        "  background: #3B82F6;"
        "  height: 4px;"
        "  border-radius: 2px;"
        "}"
        "QSlider::handle:horizontal {"
        "  background: %2;"
        "  width: 14px;"
        "  height: 14px;"
        "  margin: -5px 0;"
        "  border-radius: 7px;"
        "}")
        .arg(grooveColor, handleColor);
    m_paddingSlider->setStyleSheet(sliderStyle);
    m_cornerRadiusSlider->setStyleSheet(sliderStyle);
    m_shadowBlurSlider->setStyleSheet(sliderStyle);

    // QCheckBox: text and indicator styled for glass panel
    const QString indicatorBg = config.buttonInactiveColor.name();
    const QString indicatorBorder = config.separatorColor.name();
    const QString checkboxStyle = QStringLiteral(
        "QCheckBox { font-size: 13px; color: %1; }"
        "QCheckBox::indicator {"
        "  width: 14px; height: 14px;"
        "  border: 1px solid %2;"
        "  border-radius: 3px;"
        "  background: %3;"
        "}"
        "QCheckBox::indicator:checked {"
        "  background: #3B82F6;"
        "  border: 1px solid #3B82F6;"
        "}")
        .arg(textColor, indicatorBorder, indicatorBg);
    m_shadowCheckbox->setStyleSheet(checkboxStyle);

    // Action buttons: styled to match glass panel
    const QString btnBg = config.buttonInactiveColor.name();
    const QString btnHover = config.buttonHoverColor.name();
    const QString btnPressed = isDarkStyle ? QStringLiteral("#383838") : QStringLiteral("#C0C0C0");
    const QString actionBtnStyle = QStringLiteral(
        "QPushButton {"
        "  background-color: %1;"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 6px 16px;"
        "  font-size: 13px;"
        "  color: %2;"
        "}"
        "QPushButton:hover {"
        "  background-color: %3;"
        "}"
        "QPushButton:pressed {"
        "  background-color: %4;"
        "}")
        .arg(btnBg, textColor, btnHover, btnPressed);
    m_copyBtn->setStyleSheet(actionBtnStyle);
    m_saveBtn->setStyleSheet(actionBtnStyle);
    m_closeBtn->setStyleSheet(actionBtnStyle);
}

void BeautifyPanel::setSourcePixmap(const QPixmap& pixmap)
{
    m_sourcePixmap = pixmap;
    updatePreview();
}

void BeautifyPanel::setSettings(const BeautifySettings& settings)
{
    m_settings = settings;

    // Update controls without triggering signals
    m_backgroundTypeCombo->blockSignals(true);
    m_backgroundTypeCombo->setCurrentIndex(static_cast<int>(settings.backgroundType));
    m_backgroundTypeCombo->blockSignals(false);

    m_paddingSlider->blockSignals(true);
    m_paddingSlider->setValue(settings.padding);
    m_paddingLabel->setText(QString("%1px").arg(settings.padding));
    m_paddingSlider->blockSignals(false);

    m_cornerRadiusSlider->blockSignals(true);
    m_cornerRadiusSlider->setValue(settings.cornerRadius);
    m_cornerRadiusLabel->setText(QString("%1px").arg(settings.cornerRadius));
    m_cornerRadiusSlider->blockSignals(false);

    m_aspectRatioCombo->blockSignals(true);
    m_aspectRatioCombo->setCurrentIndex(static_cast<int>(settings.aspectRatio));
    m_aspectRatioCombo->blockSignals(false);

    m_shadowCheckbox->blockSignals(true);
    m_shadowCheckbox->setChecked(settings.shadowEnabled);
    m_shadowCheckbox->blockSignals(false);

    m_shadowBlurSlider->blockSignals(true);
    m_shadowBlurSlider->setValue(settings.shadowBlur);
    m_shadowBlurLabel->setText(QString("%1px").arg(settings.shadowBlur));
    m_shadowBlurSlider->setEnabled(settings.shadowEnabled);
    m_shadowBlurSlider->blockSignals(false);

    // Update color buttons with toolbar-style-aware borders
    const bool isDark = (ToolbarStyleConfig::loadStyle() == ToolbarStyleType::Dark);
    auto updateColorBtn = [isDark](QPushButton* btn, const QColor& color) {
        QString borderNormal = isDark ? QStringLiteral("rgba(255,255,255,60)")
                                      : QStringLiteral("rgba(0,0,0,20)");
        QString borderHover = isDark ? QStringLiteral("rgba(255,255,255,150)")
                                     : QStringLiteral("rgba(0,0,0,60)");
        btn->setStyleSheet(
            QString("QPushButton { background: %1; border: 1px solid %2; "
                    "border-radius: 4px; } "
                    "QPushButton:hover { border: 1px solid %3; }")
                .arg(color.name(), borderNormal, borderHover));
    };
    if (settings.backgroundType == BeautifyBackgroundType::Solid) {
        updateColorBtn(m_gradientStartBtn, settings.backgroundColor);
    } else {
        updateColorBtn(m_gradientStartBtn, settings.gradientStartColor);
    }
    updateColorBtn(m_gradientEndBtn, settings.gradientEndColor);
    m_gradientEndBtn->setVisible(settings.backgroundType != BeautifyBackgroundType::Solid);

    updatePreview();
}

void BeautifyPanel::updatePreview()
{
    if (m_sourcePixmap.isNull() || !m_previewLabel) return;

    const QSize previewSize = m_previewLabel->size();
    const qreal dpr = devicePixelRatioF();
    QPixmap preview(CoordinateHelper::toPhysical(previewSize, dpr));
    preview.setDevicePixelRatio(dpr);
    preview.fill(Qt::transparent);

    QPainter painter(&preview);
    BeautifyRenderer::render(painter, QRect(QPoint(0, 0), previewSize),
                             m_sourcePixmap, m_settings);
    painter.end();

    m_previewLabel->setPixmap(preview);
}

void BeautifyPanel::loadPreset(int index)
{
    auto presets = beautifyPresets();
    if (index < 0 || index >= presets.size()) return;

    const auto& preset = presets[index];
    m_settings.backgroundType = BeautifyBackgroundType::LinearGradient;
    m_settings.gradientStartColor = preset.gradientStart;
    m_settings.gradientEndColor = preset.gradientEnd;
    m_settings.gradientAngle = preset.gradientAngle;

    setSettings(m_settings);
    emit settingsChanged(m_settings);
}

void BeautifyPanel::onSettingChanged()
{
    m_previewTimer->start();
    emit settingsChanged(m_settings);
}

void BeautifyPanel::pickColor(QColor& target)
{
    QColor color = QColorDialog::getColor(target, this, "Select Color",
                                           QColorDialog::ShowAlphaChannel);
    if (color.isValid()) {
        target = color;
        setSettings(m_settings);
        emit settingsChanged(m_settings);
    }
}

void BeautifyPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // Re-render preview now that layout has resolved the label's actual size
    updatePreview();
}

void BeautifyPanel::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    auto config = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
    GlassRenderer::drawGlassPanel(painter, rect(), config);
}

void BeautifyPanel::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && event->position().y() < PREVIEW_HEIGHT + 12) {
        m_isDragging = true;
        m_dragStartPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    } else {
        QWidget::mousePressEvent(event);
    }
}

void BeautifyPanel::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isDragging) {
        move(event->globalPosition().toPoint() - m_dragStartPos);
        event->accept();
    } else {
        QWidget::mouseMoveEvent(event);
    }
}

void BeautifyPanel::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_isDragging = false;
        event->accept();
    } else {
        QWidget::mouseReleaseEvent(event);
    }
}

void BeautifyPanel::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        emit closeRequested();
        event->accept();
    } else {
        QWidget::keyPressEvent(event);
    }
}
