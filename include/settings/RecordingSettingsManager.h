#pragma once

#include <QJsonObject>
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

    // Show the screen and audio setup before every recording, even with one screen.
    bool showSetupBeforeRecording() const;
    void setShowSetupBeforeRecording(bool value);

    // Last pointer effects chosen in the preview (CursorEffectSettings JSON);
    // empty = defaults.
    QJsonObject pointerEffects() const;
    void setPointerEffects(const QJsonObject& value);

    // Hand captured surfaces straight to the hardware video encoder when they
    // fit (macOS ScreenCaptureKit + AVFoundation;
    // Windows D3D11 textures + Media Foundation after a probe encode, #155). Off forces the CPU image
    // copy for every frame; kept as a setting for A/B comparison.
    bool nativeFrameEncoding() const;
    void setNativeFrameEncoding(bool value);

    // Windows only, no UI yet. Record through Windows Graphics Capture instead
    // of Desktop Duplication (experimental until compared on real hardware,
    // #152); Desktop Duplication remains the fallback.
    bool windowsGraphicsCapture() const;
    void setWindowsGraphicsCapture(bool value);

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
    static constexpr bool kDefaultShowSetupBeforeRecording = false;
    static constexpr bool kDefaultNativeFrameEncoding = true;
    static constexpr bool kDefaultWindowsGraphicsCapture = false;

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
    static constexpr const char* kKeyShowSetupBeforeRecording = "recording/showSetupBeforeRecording";
    static constexpr const char* kKeyPointerEffects = "recording/pointerEffects";
    static constexpr const char* kKeyNativeFrameEncoding = "recording/nativeFrameEncoding";
    static constexpr const char* kKeyWindowsGraphicsCapture = "recording/windowsGraphicsCapture";
};
