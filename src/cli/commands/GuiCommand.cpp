#include "cli/commands/GuiCommand.h"

#include "cli/CaptureOutputHelper.h"
#include "settings/RegionCaptureSettingsManager.h"

#include <QGuiApplication>
#include <QJsonDocument>
#include <QPixmap>
#include <QScreen>
#include <QStringList>

namespace SnapTray {
namespace CLI {

namespace {

constexpr const char* kOptDelay = "delay";
constexpr const char* kOptClipboard = "clipboard";
constexpr const char* kOptOutput = "output";
constexpr const char* kOptPath = "path";
constexpr const char* kOptRaw = "raw";
constexpr const char* kOptPin = "pin";
constexpr const char* kOptAcceptOnSelect = "accept-on-select";
constexpr const char* kOptPrintGeometry = "print-geometry";
constexpr const char* kOptJson = "json";
constexpr const char* kOptLastRegion = "last-region";
constexpr const char* kOptRegion = "region";
constexpr const char* kOptScreen = "screen";

constexpr int kRegionFieldCount = 4;

QJsonObject rectToJson(const QRect& rect)
{
    return {
        {GuiCommand::kResultX, rect.x()},
        {GuiCommand::kResultY, rect.y()},
        {GuiCommand::kResultWidth, rect.width()},
        {GuiCommand::kResultHeight, rect.height()},
    };
}

QRect rectFromJson(const QJsonObject& object)
{
    return QRect(object.value(GuiCommand::kResultX).toInt(),
                 object.value(GuiCommand::kResultY).toInt(),
                 object.value(GuiCommand::kResultWidth).toInt(),
                 object.value(GuiCommand::kResultHeight).toInt());
}

} // namespace

QString GuiCommand::name() const { return "gui"; }

QString GuiCommand::description() const { return "Open region capture GUI"; }

void GuiCommand::setupOptions(QCommandLineParser& parser)
{
    parser.addOption({{"d", kOptDelay}, "Delay in milliseconds before capture", "ms", "0"});
    parser.addOption({{"c", kOptClipboard}, "Copy the capture to the clipboard when done"});
    parser.addOption({{"o", kOptOutput}, "Save the capture to this file when done", "file"});
    parser.addOption({{"p", kOptPath}, "Save the capture into this directory when done", "dir"});
    parser.addOption({kOptRaw, "Write the capture as PNG to stdout when done"});
    parser.addOption({kOptPin, "Pin the capture when done"});
    parser.addOption({{"s", kOptAcceptOnSelect},
                      "Finish as soon as the mouse is released (no toolbar, no annotation)"});
    parser.addOption({{"g", kOptPrintGeometry},
                      "Print the selection as WIDTHxHEIGHT+X+Y (global logical pixels)"});
    parser.addOption({kOptJson, "With --print-geometry, print the selection as JSON"});
    parser.addOption({kOptLastRegion, "Start from the last captured region"});
    parser.addOption({{"r", kOptRegion},
                      "Start from this region in logical pixels of the selected screen "
                      "(x,y,width,height)",
                      "rect"});
    parser.addOption({{"n", kOptScreen}, "Screen number for --region (default: 0)", "num"});
}

bool GuiCommand::hasImageDestination(const QCommandLineParser& parser) const
{
    return parser.isSet(kOptClipboard) || parser.isSet(kOptOutput) || parser.isSet(kOptPath)
        || parser.isSet(kOptRaw);
}

bool GuiCommand::requiresResponse(const QCommandLineParser& parser) const
{
    return hasImageDestination(parser) || parser.isSet(kOptPin)
        || parser.isSet(kOptPrintGeometry);
}

int GuiCommand::responseTimeoutMs(const QCommandLineParser& parser) const
{
    // Interactive selection has no time limit; a closed or crashed main
    // instance still ends the wait because the connection drops.
    return requiresResponse(parser) ? IPCProtocol::kWaitIndefinitely
                                    : IPCProtocol::kResponseTimeout;
}

void GuiCommand::setScreensForTesting(const QList<ScreenInfo>& screens)
{
    m_screensOverride = screens;
}

QList<GuiCommand::ScreenInfo> GuiCommand::currentScreens() const
{
    if (m_screensOverride) {
        return *m_screensOverride;
    }
    QList<ScreenInfo> screens;
    for (QScreen* screen : QGuiApplication::screens()) {
        screens.append({screen->name(), screen->geometry()});
    }
    return screens;
}

QString GuiCommand::formatGeometry(const QRect& rect, int screenIndex, bool asJson)
{
    if (asJson) {
        QJsonObject object = rectToJson(rect);
        object.insert(kResultScreen, screenIndex);
        return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
    }
    return QStringLiteral("%1x%2+%3+%4")
        .arg(rect.width())
        .arg(rect.height())
        .arg(rect.x())
        .arg(rect.y());
}

std::optional<QRect> GuiCommand::resolveLastRegion(const QRect& storedRect,
                                                   const QString& storedScreenName,
                                                   const QList<ScreenInfo>& screens,
                                                   QString* reason)
{
    if (storedRect.isEmpty()) {
        if (reason) *reason = QStringLiteral("No previous region is recorded yet");
        return std::nullopt;
    }

    for (const ScreenInfo& screen : screens) {
        const bool sameScreen = storedScreenName.isEmpty() || screen.name == storedScreenName;
        if (sameScreen && screen.geometry.contains(storedRect)) {
            return storedRect;
        }
    }

    if (reason) {
        *reason = QStringLiteral("The last region (%1) no longer fits the current screen layout")
                      .arg(formatGeometry(storedRect, -1, false));
    }
    return std::nullopt;
}

std::optional<QRect> GuiCommand::resolveRegionOption(const QString& value,
                                                     const QString& screenValue,
                                                     const QList<ScreenInfo>& screens,
                                                     QString* error)
{
    auto fail = [error](const QString& message) -> std::optional<QRect> {
        if (error) *error = message;
        return std::nullopt;
    };

    const QStringList parts = value.split(QLatin1Char(','));
    if (parts.size() != kRegionFieldCount) {
        return fail(QStringLiteral("Invalid region format. Use: x,y,width,height"));
    }

    int fields[kRegionFieldCount] = {};
    for (int i = 0; i < kRegionFieldCount; ++i) {
        bool ok = false;
        fields[i] = parts.at(i).trimmed().toInt(&ok);
        if (!ok) {
            return fail(QStringLiteral("Invalid region value: %1").arg(value));
        }
    }
    const QRect localRegion(fields[0], fields[1], fields[2], fields[3]);
    if (localRegion.width() <= 0 || localRegion.height() <= 0) {
        return fail(QStringLiteral("Invalid region size: %1").arg(value));
    }

    bool screenOk = false;
    const int screenIndex = screenValue.toInt(&screenOk);
    if (!screenOk || screenIndex < 0 || screenIndex >= screens.size()) {
        return fail(QStringLiteral("Invalid screen number: %1 (available: 0-%2)")
                        .arg(screenValue)
                        .arg(screens.size() - 1));
    }

    const QRect screenGeometry = screens.at(screenIndex).geometry;
    const QRect logicalScreenRect(QPoint(0, 0), screenGeometry.size());
    if (!logicalScreenRect.contains(localRegion)) {
        return fail(QStringLiteral("Region (%1) exceeds logical screen bounds (%2x%3)")
                        .arg(value)
                        .arg(screenGeometry.width())
                        .arg(screenGeometry.height()));
    }
    return localRegion.translated(screenGeometry.topLeft());
}

CLIResult GuiCommand::execute(const QCommandLineParser& parser)
{
    m_initialRegion.reset();

    const QString value = parser.value(kOptDelay);
    bool ok = false;
    const int delay = value.toInt(&ok);
    if (!ok || delay < 0) {
        return CLIResult::error(CLIResult::Code::InvalidArguments,
                                QString("Invalid delay value: %1").arg(value));
    }

    auto invalid = [](const QString& message) {
        return CLIResult::error(CLIResult::Code::InvalidArguments, message);
    };

    if (parser.isSet(kOptRaw) && parser.isSet(kOptPrintGeometry)) {
        return invalid("--raw and --print-geometry cannot be used together (both write to stdout)");
    }
    if (parser.isSet(kOptJson) && !parser.isSet(kOptPrintGeometry)) {
        return invalid("--json requires --print-geometry");
    }
    if (parser.isSet(kOptAcceptOnSelect) && !requiresResponse(parser)) {
        return invalid("--accept-on-select requires --clipboard, --output, --path, --raw, "
                       "--pin or --print-geometry");
    }
    if (parser.isSet(kOptRegion) && parser.isSet(kOptLastRegion)) {
        return invalid("--region and --last-region cannot be used together");
    }
    if (parser.isSet(kOptScreen) && !parser.isSet(kOptRegion)) {
        return invalid("--screen requires --region");
    }

    CLIResult result = CLIResult::success("Region capture started");

    if (parser.isSet(kOptRegion)) {
        QString error;
        const QString screenValue =
            parser.isSet(kOptScreen) ? parser.value(kOptScreen) : QStringLiteral("0");
        m_initialRegion =
            resolveRegionOption(parser.value(kOptRegion), screenValue, currentScreens(), &error);
        if (!m_initialRegion) {
            return invalid(error);
        }
    }
    else if (parser.isSet(kOptLastRegion)) {
        const auto stored = RegionCaptureSettingsManager::instance().lastCaptureRegion();
        QString reason;
        m_initialRegion = resolveLastRegion(stored ? stored->globalRect : QRect(),
                                            stored ? stored->screenName : QString(),
                                            currentScreens(), &reason);
        if (!m_initialRegion) {
            result.warnings.append(reason + QStringLiteral("; starting without a selection"));
        }
    }

    // Validate before the CLI handler dispatches IPC.
    return result;
}

QJsonObject GuiCommand::buildIPCMessage(const QCommandLineParser& parser) const
{
    QJsonObject options;

    if (parser.isSet(kOptDelay)) {
        bool ok = false;
        const int delay = parser.value(kOptDelay).toInt(&ok);
        if (ok && delay >= 0) options[kOptionDelay] = delay;
    }

    if (requiresResponse(parser)) {
        options[kOptionWaitResult] = true;
    }
    if (parser.isSet(kOptAcceptOnSelect)) {
        options[kOptionAcceptOnSelect] = true;
    }
    if (parser.isSet(kOptPin)) {
        options[kOptionPin] = true;
    }
    if (hasImageDestination(parser)) {
        options[kOptionWantImage] = true;
    }
    if (m_initialRegion) {
        options[kOptionInitialRegion] = rectToJson(*m_initialRegion);
    }

    return options;
}

CLIResult GuiCommand::handleResponse(const QCommandLineParser& parser,
                                     const IPCResponse& response) const
{
    if (!requiresResponse(parser) || !response.success) {
        return CLICommand::handleResponse(parser, response);
    }

    const QRect selection = rectFromJson(response.result);
    const int screenIndex = response.result.value(kResultScreen).toInt(-1);
    QStringList messages;

    if (hasImageDestination(parser)) {
        QPixmap pixmap;
        if (response.data.isEmpty() || !pixmap.loadFromData(response.data, "PNG")) {
            return CLIResult::error(CLIResult::Code::GeneralError,
                                    "SnapTray returned no image for the capture");
        }
        const qreal dpr = response.result.value(kResultDevicePixelRatio).toDouble(1.0);
        pixmap.setDevicePixelRatio(dpr > 0.0 ? dpr : 1.0);

        CaptureMetadata metadata;
        metadata.sourceScreen = QGuiApplication::screens().value(screenIndex, nullptr);

        if (parser.isSet(kOptClipboard)) {
            CaptureOutputOptions options;
            options.toClipboard = true;
            CLIResult copied = emitCaptureOutput(pixmap, options, metadata);
            if (!copied.isSuccess()) return copied;
            messages.append(copied.message);
        }

        if (parser.isSet(kOptOutput) || parser.isSet(kOptPath)) {
            CaptureOutputOptions options;
            options.outputFile = parser.value(kOptOutput);
            options.savePath = parser.value(kOptPath);
            CLIResult saved = emitCaptureOutput(pixmap, options, metadata);
            if (!saved.isSuccess()) return saved;
            messages.append(saved.message);
        }

        if (parser.isSet(kOptRaw)) {
            CaptureOutputOptions options;
            options.toRaw = true;
            // stdout carries only the PNG bytes.
            return emitCaptureOutput(pixmap, options, metadata);
        }
    }

    if (parser.isSet(kOptPrintGeometry)) {
        // stdout carries only the geometry so scripts can consume it directly.
        return CLIResult::success(
            formatGeometry(selection, screenIndex, parser.isSet(kOptJson)));
    }

    if (parser.isSet(kOptPin)) {
        messages.append(QStringLiteral("Capture pinned"));
    }
    return CLIResult::success(messages.join(QLatin1Char('\n')));
}

} // namespace CLI
} // namespace SnapTray
