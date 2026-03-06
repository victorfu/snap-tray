#pragma once

#include <QString>

class RecordingSettingsManager
{
public:
    static RecordingSettingsManager& instance();

    int frameRate() const;
    void setFrameRate(int value);

    int outputFormat() const;
    void setOutputFormat(int value);

    int quality() const;
    void setQuality(int value);

    bool showPreview() const;
    void setShowPreview(bool value);

    bool audioEnabled() const;
    void setAudioEnabled(bool value);

    int audioSource() const;
    void setAudioSource(int value);

    QString audioDevice() const;
    void setAudioDevice(const QString& value);

    bool countdownEnabled() const;
    void setCountdownEnabled(bool value);

    int countdownSeconds() const;
    void setCountdownSeconds(int value);

    // Default values (shared with RecordingManager)
    static constexpr int kDefaultFrameRate = 30;
    static constexpr int kDefaultOutputFormat = 0;
    static constexpr int kDefaultQuality = 55;
    static constexpr bool kDefaultShowPreview = true;
    static constexpr bool kDefaultAudioEnabled = false;
    static constexpr int kDefaultAudioSource = 0;
    static constexpr const char* kDefaultAudioDevice = "";
    static constexpr bool kDefaultCountdownEnabled = true;
    static constexpr int kDefaultCountdownSeconds = 3;

private:
    RecordingSettingsManager() = default;
    ~RecordingSettingsManager() = default;
    RecordingSettingsManager(const RecordingSettingsManager&) = delete;
    RecordingSettingsManager& operator=(const RecordingSettingsManager&) = delete;

    static constexpr const char* kKeyFrameRate = "recording/framerate";
    static constexpr const char* kKeyOutputFormat = "recording/outputFormat";
    static constexpr const char* kKeyQuality = "recording/quality";
    static constexpr const char* kKeyShowPreview = "recording/showPreview";
    static constexpr const char* kKeyAudioEnabled = "recording/audioEnabled";
    static constexpr const char* kKeyAudioSource = "recording/audioSource";
    static constexpr const char* kKeyAudioDevice = "recording/audioDevice";
    static constexpr const char* kKeyCountdownEnabled = "recording/countdownEnabled";
    static constexpr const char* kKeyCountdownSeconds = "recording/countdownSeconds";
};
