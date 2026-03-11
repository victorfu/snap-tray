#include "qml/BeautifyPanelBackend.h"

#include "settings/BeautifySettingsManager.h"

#include <QCoreApplication>
#include <QColorDialog>
#include <QTimer>
#include <QVariantMap>

namespace SnapTray {

namespace {

QString beautifyText(const char* sourceText)
{
    return QCoreApplication::translate("BeautifyPanel", sourceText);
}

} // namespace

BeautifyPanelBackend::BeautifyPanelBackend(QObject* parent)
    : QObject(parent)
    , m_previewTimer(new QTimer(this))
{
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(40);
    connect(m_previewTimer, &QTimer::timeout, this, [this]() {
        ++m_previewRevision;
        emit previewRevisionChanged();
    });

    rebuildPresetModel();
    setSettings(BeautifySettingsManager::instance().loadSettings());
}

void BeautifyPanelBackend::setSourcePixmap(const QPixmap& pixmap)
{
    m_sourcePixmap = pixmap;
    schedulePreviewUpdate();
}

void BeautifyPanelBackend::setSettings(const BeautifySettings& settings)
{
    m_settings = settings;
    emit settingsStateChanged();
    schedulePreviewUpdate();
}

int BeautifyPanelBackend::backgroundType() const
{
    return static_cast<int>(m_settings.backgroundType);
}

void BeautifyPanelBackend::setBackgroundType(int type)
{
    const auto bounded = qBound(0, type, 2);
    const auto newType = static_cast<BeautifyBackgroundType>(bounded);
    if (m_settings.backgroundType == newType) {
        return;
    }

    m_settings.backgroundType = newType;
    commitSettingsChange();
}

QColor BeautifyPanelBackend::primaryColor() const
{
    return m_settings.backgroundType == BeautifyBackgroundType::Solid
        ? m_settings.backgroundColor
        : m_settings.gradientStartColor;
}

QColor BeautifyPanelBackend::secondaryColor() const
{
    return m_settings.gradientEndColor;
}

bool BeautifyPanelBackend::secondaryColorVisible() const
{
    return m_settings.backgroundType != BeautifyBackgroundType::Solid;
}

void BeautifyPanelBackend::setPadding(int padding)
{
    padding = qBound(16, padding, 200);
    if (m_settings.padding == padding) {
        return;
    }

    m_settings.padding = padding;
    commitSettingsChange();
}

void BeautifyPanelBackend::setCornerRadius(int radius)
{
    radius = qBound(0, radius, 40);
    if (m_settings.cornerRadius == radius) {
        return;
    }

    m_settings.cornerRadius = radius;
    commitSettingsChange();
}

int BeautifyPanelBackend::aspectRatio() const
{
    return static_cast<int>(m_settings.aspectRatio);
}

void BeautifyPanelBackend::setAspectRatio(int ratio)
{
    const auto bounded = qBound(0, ratio, 5);
    const auto newRatio = static_cast<BeautifyAspectRatio>(bounded);
    if (m_settings.aspectRatio == newRatio) {
        return;
    }

    m_settings.aspectRatio = newRatio;
    commitSettingsChange();
}

void BeautifyPanelBackend::setShadowEnabled(bool enabled)
{
    if (m_settings.shadowEnabled == enabled) {
        return;
    }

    m_settings.shadowEnabled = enabled;
    commitSettingsChange();
}

void BeautifyPanelBackend::setShadowBlur(int blur)
{
    blur = qBound(0, blur, 100);
    if (m_settings.shadowBlur == blur) {
        return;
    }

    m_settings.shadowBlur = blur;
    commitSettingsChange();
}

QVariantList BeautifyPanelBackend::backgroundTypeModel() const
{
    return {
        QVariantMap{{QStringLiteral("text"), beautifyText("Solid")}, {QStringLiteral("value"), 0}},
        QVariantMap{{QStringLiteral("text"), beautifyText("Linear Gradient")}, {QStringLiteral("value"), 1}},
        QVariantMap{{QStringLiteral("text"), beautifyText("Radial Gradient")}, {QStringLiteral("value"), 2}},
    };
}

QVariantList BeautifyPanelBackend::aspectRatioModel() const
{
    return {
        QVariantMap{{QStringLiteral("text"), beautifyText("Auto")}, {QStringLiteral("value"), 0}},
        QVariantMap{{QStringLiteral("text"), QStringLiteral("1:1")}, {QStringLiteral("value"), 1}},
        QVariantMap{{QStringLiteral("text"), QStringLiteral("16:9")}, {QStringLiteral("value"), 2}},
        QVariantMap{{QStringLiteral("text"), QStringLiteral("4:3")}, {QStringLiteral("value"), 3}},
        QVariantMap{{QStringLiteral("text"), QStringLiteral("9:16")}, {QStringLiteral("value"), 4}},
        QVariantMap{{QStringLiteral("text"), QStringLiteral("2:1")}, {QStringLiteral("value"), 5}},
    };
}

void BeautifyPanelBackend::applyPreset(int index)
{
    const auto presets = beautifyPresets();
    if (index < 0 || index >= presets.size()) {
        return;
    }

    const auto& preset = presets.at(index);
    m_settings.backgroundType = BeautifyBackgroundType::LinearGradient;
    m_settings.gradientStartColor = preset.gradientStart;
    m_settings.gradientEndColor = preset.gradientEnd;
    m_settings.gradientAngle = preset.gradientAngle;
    commitSettingsChange();
}

void BeautifyPanelBackend::pickPrimaryColor()
{
    const QColor initial = primaryColor();
    const QColor selected = QColorDialog::getColor(
        initial,
        nullptr,
        beautifyText("Select Color"),
        QColorDialog::ShowAlphaChannel);
    if (!selected.isValid()) {
        return;
    }

    if (m_settings.backgroundType == BeautifyBackgroundType::Solid) {
        m_settings.backgroundColor = selected;
    } else {
        m_settings.gradientStartColor = selected;
    }
    commitSettingsChange();
}

void BeautifyPanelBackend::pickSecondaryColor()
{
    const QColor selected = QColorDialog::getColor(
        m_settings.gradientEndColor,
        nullptr,
        beautifyText("Select Color"),
        QColorDialog::ShowAlphaChannel);
    if (!selected.isValid()) {
        return;
    }

    m_settings.gradientEndColor = selected;
    commitSettingsChange();
}

void BeautifyPanelBackend::requestCopy()
{
    BeautifySettingsManager::instance().saveSettings(m_settings);
    emit copyRequested(m_settings);
}

void BeautifyPanelBackend::requestSave()
{
    BeautifySettingsManager::instance().saveSettings(m_settings);
    emit saveRequested(m_settings);
}

void BeautifyPanelBackend::requestClose()
{
    emit closeRequested();
}

void BeautifyPanelBackend::rebuildPresetModel()
{
    m_presetModel.clear();
    const auto presets = beautifyPresets();
    m_presetModel.reserve(presets.size());
    for (const auto& preset : presets) {
        QVariantMap entry;
        entry[QStringLiteral("name")] = beautifyText(qPrintable(preset.name));
        entry[QStringLiteral("startColor")] = preset.gradientStart;
        entry[QStringLiteral("endColor")] = preset.gradientEnd;
        m_presetModel.append(entry);
    }
    emit presetModelChanged();
}

void BeautifyPanelBackend::schedulePreviewUpdate()
{
    m_previewTimer->start();
}

void BeautifyPanelBackend::commitSettingsChange()
{
    emit settingsStateChanged();
    schedulePreviewUpdate();
}

} // namespace SnapTray
