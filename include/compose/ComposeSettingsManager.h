#ifndef COMPOSESETTINGSMANAGER_H
#define COMPOSESETTINGSMANAGER_H

#include <QString>

class ComposeSettingsManager
{
public:
    static ComposeSettingsManager& instance();

    QString defaultTemplate() const;
    void setDefaultTemplate(const QString& templateId);

    QString defaultCopyFormat() const;
    void setDefaultCopyFormat(const QString& format);

    bool autoIncludeMetadata() const;
    void setAutoIncludeMetadata(bool enabled);

    int screenshotMaxWidth() const;
    void setScreenshotMaxWidth(int width);

    static constexpr int kMinScreenshotWidth = 240;
    static constexpr int kMaxScreenshotWidth = 1600;

private:
    ComposeSettingsManager() = default;
    ~ComposeSettingsManager() = default;
    ComposeSettingsManager(const ComposeSettingsManager&) = delete;
    ComposeSettingsManager& operator=(const ComposeSettingsManager&) = delete;

    static constexpr const char* kSettingsKeyDefaultTemplate = "compose/defaultTemplate";
    static constexpr const char* kSettingsKeyDefaultCopyFormat = "compose/defaultCopyFormat";
    static constexpr const char* kSettingsKeyAutoIncludeMetadata = "compose/autoIncludeMetadata";
    static constexpr const char* kSettingsKeyScreenshotMaxWidth = "compose/screenshotMaxWidth";
};

#endif // COMPOSESETTINGSMANAGER_H
