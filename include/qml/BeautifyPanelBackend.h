#pragma once

#include "beautify/BeautifySettings.h"

#include <QObject>
#include <QPixmap>
#include <QVariantList>

class QTimer;

namespace SnapTray {

class BeautifyPanelBackend : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int backgroundType READ backgroundType WRITE setBackgroundType NOTIFY settingsStateChanged)
    Q_PROPERTY(QColor primaryColor READ primaryColor NOTIFY settingsStateChanged)
    Q_PROPERTY(QColor secondaryColor READ secondaryColor NOTIFY settingsStateChanged)
    Q_PROPERTY(bool secondaryColorVisible READ secondaryColorVisible NOTIFY settingsStateChanged)
    Q_PROPERTY(int padding READ padding WRITE setPadding NOTIFY settingsStateChanged)
    Q_PROPERTY(int cornerRadius READ cornerRadius WRITE setCornerRadius NOTIFY settingsStateChanged)
    Q_PROPERTY(int aspectRatio READ aspectRatio WRITE setAspectRatio NOTIFY settingsStateChanged)
    Q_PROPERTY(bool shadowEnabled READ shadowEnabled WRITE setShadowEnabled NOTIFY settingsStateChanged)
    Q_PROPERTY(int shadowBlur READ shadowBlur WRITE setShadowBlur NOTIFY settingsStateChanged)
    Q_PROPERTY(QVariantList presetModel READ presetModel NOTIFY presetModelChanged)
    Q_PROPERTY(QVariantList styleModel READ styleModel CONSTANT)
    // Index of the style/background preset matching the current settings, or -1.
    Q_PROPERTY(int currentStyle READ currentStyle NOTIFY settingsStateChanged)
    Q_PROPERTY(int currentPreset READ currentPreset NOTIFY settingsStateChanged)
    // Saved styles (#227): the whole look, kept by BeautifySettingsManager.
    Q_PROPERTY(QVariantList customStyleModel READ customStyleModel NOTIFY customStylesChanged)
    Q_PROPERTY(int currentCustomStyle READ currentCustomStyle NOTIFY customStyleStateChanged)
    Q_PROPERTY(bool customStylesFull READ customStylesFull NOTIFY customStylesChanged)
    Q_PROPERTY(int maxCustomStyles READ maxCustomStyles CONSTANT)
    Q_PROPERTY(QVariantList backgroundTypeModel READ backgroundTypeModel CONSTANT)
    Q_PROPERTY(QVariantList aspectRatioModel READ aspectRatioModel CONSTANT)
    Q_PROPERTY(int previewRevision READ previewRevision NOTIFY previewRevisionChanged)

public:
    explicit BeautifyPanelBackend(QObject* parent = nullptr);
    ~BeautifyPanelBackend() override;

    void setSourcePixmap(const QPixmap& pixmap);
    const QPixmap& sourcePixmap() const { return m_sourcePixmap; }

    BeautifySettings settings() const { return m_settings; }
    void setSettings(const BeautifySettings& settings);

    int backgroundType() const;
    void setBackgroundType(int type);

    QColor primaryColor() const;
    QColor secondaryColor() const;
    bool secondaryColorVisible() const;

    int padding() const { return m_settings.padding; }
    void setPadding(int padding);

    int cornerRadius() const { return m_settings.cornerRadius; }
    void setCornerRadius(int radius);

    int aspectRatio() const;
    void setAspectRatio(int ratio);

    bool shadowEnabled() const { return m_settings.shadowEnabled; }
    void setShadowEnabled(bool enabled);

    int shadowBlur() const { return m_settings.shadowBlur; }
    void setShadowBlur(int blur);

    QVariantList presetModel() const { return m_presetModel; }
    QVariantList styleModel() const;
    int currentStyle() const;
    int currentPreset() const;
    QVariantList customStyleModel() const;
    int currentCustomStyle() const;
    bool customStylesFull() const;
    int maxCustomStyles() const;
    QVariantList backgroundTypeModel() const;
    QVariantList aspectRatioModel() const;
    int previewRevision() const { return m_previewRevision; }

    Q_INVOKABLE void applyPreset(int index);
    Q_INVOKABLE void applyStyle(int index);
    Q_INVOKABLE QString suggestedCustomStyleName() const;
    // Saves the current look under name (trimmed). An existing name is
    // updated; otherwise a new style is added while there is room.
    Q_INVOKABLE bool saveCustomStyle(const QString& name);
    Q_INVOKABLE void applyCustomStyle(int index);
    Q_INVOKABLE void deleteCustomStyle(int index);
    // Renames a saved style; refused for an empty name or one another saved
    // style already uses (any case).
    Q_INVOKABLE bool renameCustomStyle(int index, const QString& name);
    Q_INVOKABLE bool customStyleNameTaken(const QString& name, int exceptIndex = -1) const;
    Q_INVOKABLE void pickPrimaryColor();
    Q_INVOKABLE void pickSecondaryColor();
    Q_INVOKABLE void requestCopy();
    Q_INVOKABLE void requestSave();
    Q_INVOKABLE void requestClose();

signals:
    void settingsStateChanged();
    void presetModelChanged();
    void customStylesChanged();
    void customStyleStateChanged();
    void previewRevisionChanged();
    void copyRequested(const BeautifySettings& settings);
    void saveRequested(const BeautifySettings& settings);
    void closeRequested();

private:
    void rebuildPresetModel();
    void schedulePreviewUpdate();
    void commitSettingsChange();

    void setCustomStyles(const QVector<BeautifyCustomStyle>& styles);
    void reloadCustomStyles();

    BeautifySettings m_settings;
    QVector<BeautifyCustomStyle> m_customStyles;
    QPixmap m_sourcePixmap;
    QVariantList m_presetModel;
    QTimer* m_previewTimer = nullptr;
    int m_previewRevision = 0;
};

} // namespace SnapTray
