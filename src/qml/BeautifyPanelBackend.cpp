#include "qml/BeautifyPanelBackend.h"

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

// The parts of a look the panel shows; a saved style matches when they agree.
bool sameLook(const BeautifySettings& a, const BeautifySettings& b)
{
    if (a.backgroundType != b.backgroundType || a.padding != b.padding
        || a.cornerRadius != b.cornerRadius || a.shadowEnabled != b.shadowEnabled
        || a.aspectRatio != b.aspectRatio) {
        return false;
    }
    if (a.shadowEnabled && a.shadowBlur != b.shadowBlur) {
        return false;
    }
    if (a.backgroundType == BeautifyBackgroundType::Solid) {
        return a.backgroundColor == b.backgroundColor;
    }
    return a.gradientStartColor == b.gradientStartColor && a.gradientEndColor == b.gradientEndColor;
}

// Every live backend, so a saved-style change in one panel reaches the
// others (each pin window keeps its own Beautify panel).
QList<BeautifyPanelBackend*>& liveBackends()
{
    static QList<BeautifyPanelBackend*> backends;
    return backends;
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
    m_customStyles = BeautifySettingsManager::instance().loadCustomStyles();
    setSettings(BeautifySettingsManager::instance().loadSettings());
    liveBackends().append(this);
}

BeautifyPanelBackend::~BeautifyPanelBackend()
{
    liveBackends().removeOne(this);
}

void BeautifyPanelBackend::reloadCustomStyles()
{
    m_customStyles = BeautifySettingsManager::instance().loadCustomStyles();
    emit customStylesChanged();
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
    for (const auto& style : m_customStyles) {
        const BeautifySettings& look = style.look;
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
    for (int i = 0; i < m_customStyles.size(); ++i) {
        if (sameLook(m_customStyles.at(i).look, m_settings)) {
            return i;
        }
    }
    return -1;
}

bool BeautifyPanelBackend::customStylesFull() const
{
    return m_customStyles.size() >= BeautifySettingsManager::kMaxCustomStyles;
}

int BeautifyPanelBackend::maxCustomStyles() const
{
    return BeautifySettingsManager::kMaxCustomStyles;
}

QString BeautifyPanelBackend::suggestedCustomStyleName() const
{
    for (int n = 1;; ++n) {
        const QString name = QCoreApplication::translate("BeautifyPanel", "My style %1").arg(n);
        const bool taken = std::any_of(m_customStyles.cbegin(), m_customStyles.cend(),
                                       [&name](const BeautifyCustomStyle& s) {
                                           return s.name.compare(name, Qt::CaseInsensitive) == 0;
                                       });
        if (!taken) {
            return name;
        }
    }
}

bool BeautifyPanelBackend::saveCustomStyle(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    QVector<BeautifyCustomStyle> styles = m_customStyles;
    auto existing = std::find_if(styles.begin(), styles.end(), [&trimmed](const BeautifyCustomStyle& s) {
        return s.name.compare(trimmed, Qt::CaseInsensitive) == 0;
    });
    if (existing != styles.end()) {
        existing->name = trimmed;
        existing->look = m_settings;
    } else if (styles.size() < BeautifySettingsManager::kMaxCustomStyles) {
        styles.append({trimmed, m_settings});
    } else {
        return false;
    }
    setCustomStyles(styles);
    return true;
}

void BeautifyPanelBackend::applyCustomStyle(int index)
{
    if (index < 0 || index >= m_customStyles.size()) {
        return;
    }
    setSettings(m_customStyles.at(index).look);
}

void BeautifyPanelBackend::deleteCustomStyle(int index)
{
    if (index < 0 || index >= m_customStyles.size()) {
        return;
    }
    QVector<BeautifyCustomStyle> styles = m_customStyles;
    styles.removeAt(index);
    setCustomStyles(styles);
}

bool BeautifyPanelBackend::customStyleNameTaken(const QString& name, int exceptIndex) const
{
    const QString trimmed = name.trimmed();
    for (int i = 0; i < m_customStyles.size(); ++i) {
        if (i != exceptIndex && m_customStyles.at(i).name.compare(trimmed, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

bool BeautifyPanelBackend::renameCustomStyle(int index, const QString& name)
{
    const QString trimmed = name.trimmed();
    if (index < 0 || index >= m_customStyles.size() || trimmed.isEmpty()
        || customStyleNameTaken(trimmed, index)) {
        return false;
    }
    if (m_customStyles.at(index).name == trimmed) {
        return true;
    }
    QVector<BeautifyCustomStyle> styles = m_customStyles;
    styles[index].name = trimmed;
    setCustomStyles(styles);
    return true;
}

void BeautifyPanelBackend::setCustomStyles(const QVector<BeautifyCustomStyle>& styles)
{
    m_customStyles = styles;
    BeautifySettingsManager::instance().saveCustomStyles(m_customStyles);
    emit customStylesChanged();
    // Other panels would otherwise write their stale list back later.
    const QList<BeautifyPanelBackend*> backends = liveBackends();
    for (BeautifyPanelBackend* other : backends) {
        if (other != this) {
            other->reloadCustomStyles();
        }
    }
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
