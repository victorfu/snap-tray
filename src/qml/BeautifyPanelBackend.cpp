#include "qml/BeautifyPanelBackend.h"

#include "beautify/FrameStyleLibrary.h"
#include "settings/BeautifySettingsManager.h"

#include <QCoreApplication>
#include <QList>
#include <QColorDialog>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>
#include <iterator>

namespace SnapTray {

namespace {

QString beautifyText(const char* sourceText)
{
    return QCoreApplication::translate("BeautifyPanel", sourceText);
}

// A whole look for the screenshot inset (#227): padding, corners and shadow
// together. Applying one leaves the background and ratio as they are.
struct BeautifyStylePreset {
    const char* name; // translated in the "BeautifyPanel" context
    int padding;
    int cornerRadius;
    bool shadowEnabled;
    int shadowBlur;

    bool matches(const BeautifySettings& s) const
    {
        return s.padding == padding && s.cornerRadius == cornerRadius
            && s.shadowEnabled == shadowEnabled
            && (!shadowEnabled || s.shadowBlur == shadowBlur);
    }
};

// "Standard" is the BeautifySettings default look.
constexpr BeautifyStylePreset kStylePresets[] = {
    {QT_TRANSLATE_NOOP("BeautifyPanel", "Compact"), 32, 8, true, 24},
    {QT_TRANSLATE_NOOP("BeautifyPanel", "Standard"), 64, 12, true, 40},
    {QT_TRANSLATE_NOOP("BeautifyPanel", "Spacious"), 128, 16, true, 60},
    {QT_TRANSLATE_NOOP("BeautifyPanel", "Flat"), 64, 0, false, 40},
};
constexpr int kStyleCount = int(std::size(kStylePresets));

// The panel's control ranges, in logical pixels (as BeautifySettingsManager
// clamps the stored values).
constexpr int kMinPadding = 16;
constexpr int kMaxPadding = 200;
constexpr int kMaxCornerRadius = 40;
constexpr int kMaxShadowBlur = 100;
constexpr int kMaxShadowOffset = 50;

// The parts of a look the panel shows; a saved style matches when they agree.
bool sameLook(const BeautifySettings& a, const BeautifySettings& b)
{
    if (a.backgroundType != b.backgroundType || a.padding != b.padding
        || a.cornerRadius != b.cornerRadius || a.shadowEnabled != b.shadowEnabled
        || a.borderEnabled != b.borderEnabled || a.aspectRatio != b.aspectRatio) {
        return false;
    }
    if (a.shadowEnabled && a.shadowBlur != b.shadowBlur) {
        return false;
    }
    if (a.borderEnabled && (a.borderWidth != b.borderWidth || a.borderColor != b.borderColor)) {
        return false;
    }
    if (a.backgroundType == BeautifyBackgroundType::Solid) {
        return a.backgroundColor == b.backgroundColor;
    }
    return a.gradientStartColor == b.gradientStartColor && a.gradientEndColor == b.gradientEndColor;
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
    connect(this, &BeautifyPanelBackend::settingsStateChanged,
            this, &BeautifyPanelBackend::customStyleStateChanged);
    connect(this, &BeautifyPanelBackend::customStylesChanged,
            this, &BeautifyPanelBackend::customStyleStateChanged);
    // One list for every pin window's panel and the recording editor (#269).
    connect(&FrameStyleLibrary::instance(), &FrameStyleLibrary::stylesChanged,
            this, &BeautifyPanelBackend::customStylesChanged);
    setSettings(BeautifySettingsManager::instance().loadSettings());
}

BeautifyPanelBackend::~BeautifyPanelBackend() = default;

void BeautifyPanelBackend::setSourcePixmap(const QPixmap& pixmap)
{
    m_sourcePixmap = pixmap;
    schedulePreviewUpdate();
}

BeautifySettings BeautifyPanelBackend::settingsFor(const FrameStyle& style) const
{
    // Screenshot Beautify works in fixed pixels, like its built-in styles and
    // sliders, so a saved style always means the same pixels here whatever
    // the screenshot's size. Only the recording frame scales a style with the
    // picture (#269). The reference size keeps styles saved before #269 exact.
    BeautifySettings s = style.toBeautify(FrameStyle::kReferenceShortSide);
    s.padding = std::clamp(s.padding, kMinPadding, kMaxPadding);
    s.cornerRadius = std::clamp(s.cornerRadius, 0, kMaxCornerRadius);
    s.shadowBlur = std::clamp(s.shadowBlur, 0, kMaxShadowBlur);
    s.shadowOffsetX = std::clamp(s.shadowOffsetX, -kMaxShadowOffset, kMaxShadowOffset);
    s.shadowOffsetY = std::clamp(s.shadowOffsetY, -kMaxShadowOffset, kMaxShadowOffset);
    s.borderWidth = std::clamp(s.borderWidth, BeautifySettingsManager::kMinBorderWidth,
                               BeautifySettingsManager::kMaxBorderWidth);
    return s;
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
    padding = qBound(kMinPadding, padding, kMaxPadding);
    if (m_settings.padding == padding) {
        return;
    }

    m_settings.padding = padding;
    commitSettingsChange();
}

void BeautifyPanelBackend::setCornerRadius(int radius)
{
    radius = qBound(0, radius, kMaxCornerRadius);
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
    blur = qBound(0, blur, kMaxShadowBlur);
    if (m_settings.shadowBlur == blur) {
        return;
    }

    m_settings.shadowBlur = blur;
    commitSettingsChange();
}

void BeautifyPanelBackend::setBorderEnabled(bool enabled)
{
    if (m_settings.borderEnabled == enabled) {
        return;
    }

    m_settings.borderEnabled = enabled;
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

QVariantList BeautifyPanelBackend::styleModel() const
{
    QVariantList model;
    for (const auto& style : kStylePresets) {
        model.append(QVariantMap{
            {QStringLiteral("name"), beautifyText(style.name)},
            {QStringLiteral("padding"), style.padding},
            {QStringLiteral("cornerRadius"), style.cornerRadius},
            {QStringLiteral("shadowEnabled"), style.shadowEnabled},
            {QStringLiteral("shadowBlur"), style.shadowBlur},
        });
    }
    return model;
}

int BeautifyPanelBackend::currentStyle() const
{
    for (int i = 0; i < kStyleCount; ++i) {
        if (kStylePresets[i].matches(m_settings)) {
            return i;
        }
    }
    return -1;
}

int BeautifyPanelBackend::currentPreset() const
{
    if (m_settings.backgroundType != BeautifyBackgroundType::LinearGradient) {
        return -1;
    }
    const auto presets = beautifyPresets();
    for (int i = 0; i < presets.size(); ++i) {
        const auto& preset = presets.at(i);
        if (preset.gradientStart == m_settings.gradientStartColor
            && preset.gradientEnd == m_settings.gradientEndColor) {
            return i;
        }
    }
    return -1;
}

void BeautifyPanelBackend::applyStyle(int index)
{
    if (index < 0 || index >= kStyleCount) {
        return;
    }

    const auto& style = kStylePresets[index];
    if (style.matches(m_settings)) {
        return;
    }
    m_settings.padding = style.padding;
    m_settings.cornerRadius = style.cornerRadius;
    m_settings.shadowEnabled = style.shadowEnabled;
    m_settings.shadowBlur = style.shadowBlur;
    commitSettingsChange();
}

QVariantList BeautifyPanelBackend::customStyleModel() const
{
    QVariantList model;
    for (const auto& style : FrameStyleLibrary::instance().styles()) {
        const BeautifySettings look = settingsFor(style.style);
        const bool solid = look.backgroundType == BeautifyBackgroundType::Solid;
        model.append(QVariantMap{
            {QStringLiteral("name"), style.name},
            {QStringLiteral("padding"), look.padding},
            {QStringLiteral("cornerRadius"), look.cornerRadius},
            {QStringLiteral("shadowEnabled"), look.shadowEnabled},
            {QStringLiteral("shadowBlur"), look.shadowBlur},
            {QStringLiteral("startColor"), solid ? look.backgroundColor : look.gradientStartColor},
            {QStringLiteral("endColor"), solid ? look.backgroundColor : look.gradientEndColor},
        });
    }
    return model;
}

int BeautifyPanelBackend::currentCustomStyle() const
{
    const auto& styles = FrameStyleLibrary::instance().styles();
    for (int i = 0; i < styles.size(); ++i) {
        if (sameLook(settingsFor(styles.at(i).style), m_settings)) {
            return i;
        }
    }
    return -1;
}

bool BeautifyPanelBackend::customStylesFull() const
{
    return FrameStyleLibrary::instance().isFull();
}

int BeautifyPanelBackend::maxCustomStyles() const
{
    return FrameStyleLibrary::maxStyles();
}

QString BeautifyPanelBackend::suggestedCustomStyleName() const
{
    return FrameStyleLibrary::instance().suggestedName();
}

bool BeautifyPanelBackend::saveCustomStyle(const QString& name)
{
    return FrameStyleLibrary::instance().save(name, FrameStyle::fromBeautify(m_settings, FrameStyle::kReferenceShortSide));
}

void BeautifyPanelBackend::applyCustomStyle(int index)
{
    const auto& styles = FrameStyleLibrary::instance().styles();
    if (index < 0 || index >= styles.size()) {
        return;
    }
    setSettings(settingsFor(styles.at(index).style));
}

void BeautifyPanelBackend::deleteCustomStyle(int index)
{
    FrameStyleLibrary::instance().remove(index);
}

bool BeautifyPanelBackend::customStyleNameTaken(const QString& name, int exceptIndex) const
{
    return FrameStyleLibrary::instance().nameTaken(name, exceptIndex);
}

bool BeautifyPanelBackend::renameCustomStyle(int index, const QString& name)
{
    return FrameStyleLibrary::instance().rename(index, name);
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
