#ifndef GUI_COMMAND_H
#define GUI_COMMAND_H

#include "cli/CLICommand.h"

#include <QList>
#include <QRect>
#include <QString>

#include <optional>

namespace SnapTray {
namespace CLI {

/**
 * @brief Open region capture GUI (same as pressing F2)
 *
 * Without destination options this only opens the GUI and returns. With a
 * destination (--clipboard, --output/--path, --raw, --pin) or --print-geometry
 * the CLI waits until the user completes or cancels the selection.
 */
class GuiCommand : public CLICommand
{
public:
    // A screen as seen by the CLI process, in global logical coordinates.
    struct ScreenInfo {
        QString name;
        QRect geometry;
    };

    // IPC option keys shared with MainApplication.
    static constexpr const char* kOptionDelay = "delay";
    static constexpr const char* kOptionWaitResult = "waitResult";
    static constexpr const char* kOptionAcceptOnSelect = "acceptOnSelect";
    static constexpr const char* kOptionPin = "pin";
    static constexpr const char* kOptionWantImage = "wantImage";
    static constexpr const char* kOptionInitialRegion = "initialRegion";

    // Keys of IPCResponse::result for a completed capture.
    static constexpr const char* kResultX = "x";
    static constexpr const char* kResultY = "y";
    static constexpr const char* kResultWidth = "width";
    static constexpr const char* kResultHeight = "height";
    static constexpr const char* kResultScreen = "screen";
    static constexpr const char* kResultDevicePixelRatio = "devicePixelRatio";

    QString name() const override;
    QString description() const override;
    void setupOptions(QCommandLineParser& parser) override;
    CLIResult execute(const QCommandLineParser& parser) override;
    bool requiresGUI(const QCommandLineParser&) const override { return true; }
    bool requiresResponse(const QCommandLineParser& parser) const override;
    int responseTimeoutMs(const QCommandLineParser& parser) const override;
    QJsonObject buildIPCMessage(const QCommandLineParser& parser) const override;
    CLIResult handleResponse(const QCommandLineParser& parser,
                             const IPCResponse& response) const override;

    // Screens used to resolve --region/--last-region; tests may replace them.
    void setScreensForTesting(const QList<ScreenInfo>& screens);

    // "WIDTHxHEIGHT+X+Y", or the JSON object form.
    static QString formatGeometry(const QRect& rect, int screenIndex, bool asJson);

    // Global rect for --last-region, or nullopt (with a reason) when the
    // stored region no longer fits the current screen layout.
    static std::optional<QRect> resolveLastRegion(
        const QRect& storedRect, const QString& storedScreenName,
        const QList<ScreenInfo>& screens, QString* reason);

    // Global rect for --region "x,y,w,h" on screen index `screenIndex`
    // (same rules as the `region` command), or nullopt with an error.
    static std::optional<QRect> resolveRegionOption(
        const QString& value, const QString& screenValue,
        const QList<ScreenInfo>& screens, QString* error);

private:
    QList<ScreenInfo> currentScreens() const;
    bool hasImageDestination(const QCommandLineParser& parser) const;

    std::optional<QList<ScreenInfo>> m_screensOverride;
    // Resolved by execute() (pre-flight) and sent by buildIPCMessage().
    std::optional<QRect> m_initialRegion;
};

} // namespace CLI
} // namespace SnapTray

#endif // GUI_COMMAND_H
