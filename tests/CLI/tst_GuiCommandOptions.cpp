#include <QtTest>

#include <QBuffer>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "cli/CLIHandler.h"
#include "cli/commands/GuiCommand.h"
#include "settings/RegionCaptureSettingsManager.h"
#include "settings/Settings.h"

using SnapTray::CLI::CLIResult;
using SnapTray::CLI::GuiCommand;
using SnapTray::CLI::IPCResponse;

namespace {

constexpr const char* kLastRegionKey = "regionCapture/lastRegion";
constexpr const char* kLastRegionScreenKey = "regionCapture/lastRegionScreen";

const QList<GuiCommand::ScreenInfo> kTwoScreens = {
    {QStringLiteral("Left"), QRect(0, 0, 1920, 1080)},
    {QStringLiteral("Right"), QRect(1920, 0, 1280, 1024)},
};

struct ParsedGui {
    GuiCommand command;
    QCommandLineParser parser;
};

std::unique_ptr<ParsedGui> parseGui(const QStringList& args)
{
    auto parsed = std::make_unique<ParsedGui>();
    parsed->command.setupOptions(parsed->parser);
    parsed->command.setScreensForTesting(kTwoScreens);
    QStringList full = {QStringLiteral("snaptray")};
    full += args;
    if (!parsed->parser.parse(full)) {
        qWarning() << parsed->parser.errorText();
        return nullptr;
    }
    return parsed;
}

QByteArray pngBytes(const QSize& size)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(Qt::magenta);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

IPCResponse completedResponse(const QRect& rect, int screen, const QByteArray& data = {})
{
    IPCResponse response;
    response.success = true;
    response.code = 0;
    response.result = {
        {GuiCommand::kResultX, rect.x()},
        {GuiCommand::kResultY, rect.y()},
        {GuiCommand::kResultWidth, rect.width()},
        {GuiCommand::kResultHeight, rect.height()},
        {GuiCommand::kResultScreen, screen},
        {GuiCommand::kResultDevicePixelRatio, 1.0},
    };
    response.data = data;
    return response;
}

void clearLastRegion()
{
    auto settings = SnapTray::getSettings();
    settings.remove(kLastRegionKey);
    settings.remove(kLastRegionScreenKey);
    settings.sync();
}

} // namespace

class tst_GuiCommandOptions : public QObject
{
    Q_OBJECT

private slots:
    void init() { clearLastRegion(); }
    void cleanup() { clearLastRegion(); }

    void noNewOptions_keepsFireAndForget();
    void destinations_waitForResult_data();
    void destinations_waitForResult();
    void invalidCombinations_returnInvalidArguments_data();
    void invalidCombinations_returnInvalidArguments();
    void multipleDestinations_areAllowed();
    void acceptOnSelect_isSentWithDestination();
    void region_isConvertedToGlobalOnSelectedScreen();
    void region_rejectsInvalidValues_data();
    void region_rejectsInvalidValues();
    void lastRegion_usesStoredRegion();
    void lastRegion_fallsBackWithWarningWhenLayoutChanged();
    void lastRegion_fallsBackWithWarningWhenNothingStored();
    void resolveLastRegion_rules();
    void formatGeometry_textAndJson();
    void handleResponse_cancelledReturnsCode5();
    void handleResponse_lostConnectionReturnsGeneralError();
    void handleResponse_printGeometry();
    void handleResponse_printGeometryJson();
    void handleResponse_savesToOutputFile();
    void handleResponse_saveFailureReturnsFileError();
    void handleResponse_rawWritesPngData();
    void handleResponse_preservesTransparentCorners_data();
    void handleResponse_preservesTransparentCorners();
    void handleResponse_missingImageIsError();
    void handleResponse_pinOnly();
    void lastCaptureRegionSetting_roundTrip();
    void ipcResponse_roundTripsCodeResultAndData();
};

void tst_GuiCommandOptions::noNewOptions_keepsFireAndForget()
{
    auto gui = parseGui({QStringLiteral("-d"), QStringLiteral("500")});
    QVERIFY(gui);
    QVERIFY(gui->command.execute(gui->parser).isSuccess());
    QVERIFY(!gui->command.requiresResponse(gui->parser));
    QCOMPARE(gui->command.responseTimeoutMs(gui->parser),
             SnapTray::CLI::IPCProtocol::kResponseTimeout);

    // Only the delay is sent, exactly as before #272.
    const QJsonObject message = gui->command.buildIPCMessage(gui->parser);
    QCOMPARE(message, QJsonObject({{GuiCommand::kOptionDelay, 500}}));

    auto plain = parseGui({});
    QVERIFY(plain);
    QVERIFY(plain->command.execute(plain->parser).isSuccess());
    QVERIFY(plain->command.buildIPCMessage(plain->parser).isEmpty());
}

void tst_GuiCommandOptions::destinations_waitForResult_data()
{
    QTest::addColumn<QStringList>("args");
    QTest::addColumn<bool>("wantImage");
    QTest::addColumn<bool>("pin");

    QTest::newRow("clipboard") << QStringList{"-c"} << true << false;
    QTest::newRow("clipboard-long") << QStringList{"--clipboard"} << true << false;
    QTest::newRow("output") << QStringList{"-o", "shot.png"} << true << false;
    QTest::newRow("path") << QStringList{"-p", "dir"} << true << false;
    QTest::newRow("raw") << QStringList{"--raw"} << true << false;
    QTest::newRow("pin") << QStringList{"--pin"} << false << true;
    QTest::newRow("print-geometry") << QStringList{"-g"} << false << false;
    QTest::newRow("print-geometry-json") << QStringList{"--print-geometry", "--json"} << false
                                         << false;
}

void tst_GuiCommandOptions::destinations_waitForResult()
{
    QFETCH(QStringList, args);
    QFETCH(bool, wantImage);
    QFETCH(bool, pin);

    auto gui = parseGui(args);
    QVERIFY(gui);
    const CLIResult result = gui->command.execute(gui->parser);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QVERIFY(gui->command.requiresResponse(gui->parser));
    // Interactive selection may take longer than the 30 s IPC default.
    QCOMPARE(gui->command.responseTimeoutMs(gui->parser),
             SnapTray::CLI::IPCProtocol::kWaitIndefinitely);

    const QJsonObject message = gui->command.buildIPCMessage(gui->parser);
    QVERIFY(message.value(GuiCommand::kOptionWaitResult).toBool());
    QCOMPARE(message.value(GuiCommand::kOptionWantImage).toBool(), wantImage);
    QCOMPARE(message.value(GuiCommand::kOptionPin).toBool(), pin);
    QVERIFY(!message.contains(GuiCommand::kOptionAcceptOnSelect));
    QVERIFY(!message.contains(GuiCommand::kOptionInitialRegion));
}

void tst_GuiCommandOptions::invalidCombinations_returnInvalidArguments_data()
{
    QTest::addColumn<QStringList>("args");
    QTest::addColumn<QString>("expectedMessage");

    QTest::newRow("raw+geometry") << QStringList{"--raw", "-g"} << "--raw and --print-geometry";
    QTest::newRow("accept-on-select alone") << QStringList{"-s"} << "--accept-on-select requires";
    QTest::newRow("accept-on-select with region only")
        << QStringList{"-s", "-r", "0,0,10,10"} << "--accept-on-select requires";
    QTest::newRow("json without geometry") << QStringList{"-c", "--json"} << "--json requires";
    QTest::newRow("region+last-region")
        << QStringList{"-r", "0,0,10,10", "--last-region"} << "cannot be used together";
    QTest::newRow("screen without region") << QStringList{"-n", "1"} << "--screen requires";
    QTest::newRow("negative delay") << QStringList{"-d", "-5", "-c"} << "Invalid delay";
}

void tst_GuiCommandOptions::invalidCombinations_returnInvalidArguments()
{
    QFETCH(QStringList, args);
    QFETCH(QString, expectedMessage);

    auto gui = parseGui(args);
    QVERIFY(gui);
    const CLIResult result = gui->command.execute(gui->parser);
    QCOMPARE(result.code, CLIResult::Code::InvalidArguments);
    QVERIFY2(result.message.contains(expectedMessage), qPrintable(result.message));
}

void tst_GuiCommandOptions::multipleDestinations_areAllowed()
{
    auto gui = parseGui({"-c", "-o", "shot.png", "--pin", "-g"});
    QVERIFY(gui);
    QVERIFY(gui->command.execute(gui->parser).isSuccess());
    const QJsonObject message = gui->command.buildIPCMessage(gui->parser);
    QVERIFY(message.value(GuiCommand::kOptionWaitResult).toBool());
    QVERIFY(message.value(GuiCommand::kOptionWantImage).toBool());
    QVERIFY(message.value(GuiCommand::kOptionPin).toBool());
}

void tst_GuiCommandOptions::acceptOnSelect_isSentWithDestination()
{
    for (const QStringList& args : {QStringList{"-s", "-c"}, QStringList{"--accept-on-select", "-g"}}) {
        auto gui = parseGui(args);
        QVERIFY(gui);
        QVERIFY(gui->command.execute(gui->parser).isSuccess());
        const QJsonObject message = gui->command.buildIPCMessage(gui->parser);
        QVERIFY(message.value(GuiCommand::kOptionAcceptOnSelect).toBool());
        QVERIFY(message.value(GuiCommand::kOptionWaitResult).toBool());
    }
}

void tst_GuiCommandOptions::region_isConvertedToGlobalOnSelectedScreen()
{
    // Default screen 0: local equals global for a screen at the origin.
    auto primary = parseGui({"-r", "10,20,300,200"});
    QVERIFY(primary);
    QVERIFY(primary->command.execute(primary->parser).isSuccess());
    // An initial selection alone does not make the CLI wait.
    QVERIFY(!primary->command.requiresResponse(primary->parser));
    QCOMPARE(primary->command.buildIPCMessage(primary->parser)
                 .value(GuiCommand::kOptionInitialRegion).toObject(),
             QJsonObject({{"x", 10}, {"y", 20}, {"width", 300}, {"height", 200}}));

    // Screen 1 is offset by its global origin, like the `region` command.
    auto second = parseGui({"-r", "10,20,300,200", "-n", "1", "-c"});
    QVERIFY(second);
    QVERIFY(second->command.execute(second->parser).isSuccess());
    QCOMPARE(second->command.buildIPCMessage(second->parser)
                 .value(GuiCommand::kOptionInitialRegion).toObject(),
             QJsonObject({{"x", 1930}, {"y", 20}, {"width", 300}, {"height", 200}}));
}

void tst_GuiCommandOptions::region_rejectsInvalidValues_data()
{
    QTest::addColumn<QStringList>("args");
    QTest::addColumn<QString>("expectedMessage");

    QTest::newRow("too few fields") << QStringList{"-r", "1,2,3"} << "Invalid region format";
    QTest::newRow("non numeric") << QStringList{"-r", "a,2,3,4"} << "Invalid region value";
    QTest::newRow("zero width") << QStringList{"-r", "0,0,0,10"} << "Invalid region size";
    QTest::newRow("outside screen") << QStringList{"-r", "1900,0,100,100"}
                                    << "exceeds logical screen bounds";
    QTest::newRow("outside second screen") << QStringList{"-r", "0,1000,100,100", "-n", "1"}
                                           << "exceeds logical screen bounds";
    QTest::newRow("bad screen") << QStringList{"-r", "0,0,10,10", "-n", "7"}
                                << "Invalid screen number";
    QTest::newRow("non numeric screen") << QStringList{"-r", "0,0,10,10", "-n", "x"}
                                        << "Invalid screen number";
}

void tst_GuiCommandOptions::region_rejectsInvalidValues()
{
    QFETCH(QStringList, args);
    QFETCH(QString, expectedMessage);

    auto gui = parseGui(args);
    QVERIFY(gui);
    const CLIResult result = gui->command.execute(gui->parser);
    QCOMPARE(result.code, CLIResult::Code::InvalidArguments);
    QVERIFY2(result.message.contains(expectedMessage), qPrintable(result.message));
}

void tst_GuiCommandOptions::lastRegion_usesStoredRegion()
{
    RegionCaptureSettingsManager::instance().setLastCaptureRegion(QRect(2000, 50, 400, 300),
                                                                  QStringLiteral("Right"));
    auto gui = parseGui({"--last-region", "-c"});
    QVERIFY(gui);
    const CLIResult result = gui->command.execute(gui->parser);
    QVERIFY(result.isSuccess());
    QVERIFY(result.warnings.isEmpty());
    QCOMPARE(gui->command.buildIPCMessage(gui->parser)
                 .value(GuiCommand::kOptionInitialRegion).toObject(),
             QJsonObject({{"x", 2000}, {"y", 50}, {"width", 400}, {"height", 300}}));
}

void tst_GuiCommandOptions::lastRegion_fallsBackWithWarningWhenLayoutChanged()
{
    // Recorded on a screen that is gone now.
    RegionCaptureSettingsManager::instance().setLastCaptureRegion(QRect(3300, 0, 100, 100),
                                                                  QStringLiteral("Gone"));
    auto gui = parseGui({"--last-region"});
    QVERIFY(gui);
    const CLIResult result = gui->command.execute(gui->parser);
    QVERIFY(result.isSuccess());
    QCOMPARE(result.warnings.size(), 1);
    QVERIFY2(result.warnings.first().contains("no longer fits"), qPrintable(result.warnings.first()));
    QVERIFY(!gui->command.buildIPCMessage(gui->parser).contains(GuiCommand::kOptionInitialRegion));
}

void tst_GuiCommandOptions::lastRegion_fallsBackWithWarningWhenNothingStored()
{
    auto gui = parseGui({"--last-region", "-g"});
    QVERIFY(gui);
    const CLIResult result = gui->command.execute(gui->parser);
    QVERIFY(result.isSuccess());
    QCOMPARE(result.warnings.size(), 1);
    QVERIFY(!gui->command.buildIPCMessage(gui->parser).contains(GuiCommand::kOptionInitialRegion));
}

void tst_GuiCommandOptions::resolveLastRegion_rules()
{
    QString reason;
    QCOMPARE(GuiCommand::resolveLastRegion(QRect(10, 10, 50, 50), "Left", kTwoScreens, &reason),
             std::optional<QRect>(QRect(10, 10, 50, 50)));
    // A recorded screen name that moved: the rect must lie on that screen.
    QVERIFY(!GuiCommand::resolveLastRegion(QRect(10, 10, 50, 50), "Right", kTwoScreens, &reason));
    // Straddling two screens is not inside any single screen.
    QVERIFY(!GuiCommand::resolveLastRegion(QRect(1900, 10, 50, 50), "", kTwoScreens, &reason));
    // Unknown screen name (older records) falls back to geometry only.
    QCOMPARE(GuiCommand::resolveLastRegion(QRect(2000, 10, 50, 50), "", kTwoScreens, &reason),
             std::optional<QRect>(QRect(2000, 10, 50, 50)));
    QVERIFY(!GuiCommand::resolveLastRegion(QRect(), "Left", kTwoScreens, &reason));
}

void tst_GuiCommandOptions::formatGeometry_textAndJson()
{
    QCOMPARE(GuiCommand::formatGeometry(QRect(-1280, 40, 640, 480), 1, false),
             QStringLiteral("640x480+-1280+40"));
    QCOMPARE(GuiCommand::formatGeometry(QRect(5, 6, 7, 8), 2, false), QStringLiteral("7x8+5+6"));

    const QJsonObject json = QJsonDocument::fromJson(
        GuiCommand::formatGeometry(QRect(5, 6, 7, 8), 2, true).toUtf8()).object();
    QCOMPARE(json, QJsonObject({{"x", 5}, {"y", 6}, {"width", 7}, {"height", 8}, {"screen", 2}}));
}

void tst_GuiCommandOptions::handleResponse_cancelledReturnsCode5()
{
    auto gui = parseGui({"-s", "-c"});
    QVERIFY(gui);
    IPCResponse response;
    response.code = static_cast<int>(CLIResult::Code::Cancelled);
    response.error = QStringLiteral("Capture cancelled");
    const CLIResult result = gui->command.handleResponse(gui->parser, response);
    QCOMPARE(result.code, CLIResult::Code::Cancelled);
    QCOMPARE(static_cast<int>(result.code), 5);
    QVERIFY(result.data.isEmpty());
}

void tst_GuiCommandOptions::handleResponse_lostConnectionReturnsGeneralError()
{
    auto gui = parseGui({"-c"});
    QVERIFY(gui);
    IPCResponse response; // What IPCProtocol returns when the main instance goes away.
    response.error = QStringLiteral("Lost connection to SnapTray");
    const CLIResult result = gui->command.handleResponse(gui->parser, response);
    QCOMPARE(result.code, CLIResult::Code::GeneralError);
    QCOMPARE(result.message, response.error);
}

void tst_GuiCommandOptions::handleResponse_printGeometry()
{
    auto gui = parseGui({"-g"});
    QVERIFY(gui);
    const CLIResult result =
        gui->command.handleResponse(gui->parser, completedResponse(QRect(1930, 20, 300, 200), 1));
    QVERIFY(result.isSuccess());
    QCOMPARE(result.message, QStringLiteral("300x200+1930+20"));
    QVERIFY(result.data.isEmpty());
}

void tst_GuiCommandOptions::handleResponse_printGeometryJson()
{
    auto gui = parseGui({"-g", "--json"});
    QVERIFY(gui);
    const CLIResult result =
        gui->command.handleResponse(gui->parser, completedResponse(QRect(1, 2, 3, 4), 0));
    QVERIFY(result.isSuccess());
    QCOMPARE(QJsonDocument::fromJson(result.message.toUtf8()).object(),
             QJsonObject({{"x", 1}, {"y", 2}, {"width", 3}, {"height", 4}, {"screen", 0}}));
}

void tst_GuiCommandOptions::handleResponse_savesToOutputFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("gui.png"));
    auto gui = parseGui({"-o", path});
    QVERIFY(gui);

    const CLIResult result = gui->command.handleResponse(
        gui->parser, completedResponse(QRect(0, 0, 32, 24), 0, pngBytes(QSize(32, 24))));
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QVERIFY(result.message.contains(path));
    const QImage saved(path);
    QCOMPARE(saved.size(), QSize(32, 24));
}

void tst_GuiCommandOptions::handleResponse_saveFailureReturnsFileError()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // The parent "directory" is a regular file, so the save must fail.
    const QString blocker = dir.filePath(QStringLiteral("blocker"));
    QFile file(blocker);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    auto gui = parseGui({"-o", blocker + QStringLiteral("/gui.png")});
    QVERIFY(gui);

    const CLIResult result = gui->command.handleResponse(
        gui->parser, completedResponse(QRect(0, 0, 8, 8), 0, pngBytes(QSize(8, 8))));
    QCOMPARE(result.code, CLIResult::Code::FileError);
}

void tst_GuiCommandOptions::handleResponse_rawWritesPngData()
{
    auto gui = parseGui({"--raw"});
    QVERIFY(gui);
    const CLIResult result = gui->command.handleResponse(
        gui->parser, completedResponse(QRect(0, 0, 16, 9), 0, pngBytes(QSize(16, 9))));
    QVERIFY(result.isSuccess());
    QVERIFY(result.message.isEmpty());
    QImage image;
    QVERIFY(image.loadFromData(result.data, "PNG"));
    QCOMPARE(image.size(), QSize(16, 9));
}

void tst_GuiCommandOptions::handleResponse_preservesTransparentCorners_data()
{
    QTest::addColumn<bool>("raw");
    QTest::newRow("output-file") << false;
    QTest::newRow("raw") << true;
}

void tst_GuiCommandOptions::handleResponse_preservesTransparentCorners()
{
    // A rounded selection arrives with transparent corners; scripted output
    // must keep them like the selector's own Save does.
    QFETCH(bool, raw);
    QImage rounded(QSize(16, 16), QImage::Format_ARGB32);
    rounded.fill(Qt::magenta);
    rounded.setPixelColor(0, 0, Qt::transparent);
    QByteArray encoded;
    QBuffer buffer(&encoded);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(rounded.save(&buffer, "PNG"));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("rounded.png"));
    auto gui = parseGui(raw ? QStringList{"--raw"} : QStringList{"-o", path});
    QVERIFY(gui);
    const CLIResult result = gui->command.handleResponse(
        gui->parser, completedResponse(QRect(0, 0, 16, 16), 0, encoded));
    QVERIFY2(result.isSuccess(), qPrintable(result.message));

    QImage output;
    if (raw) {
        QVERIFY(output.loadFromData(result.data, "PNG"));
    } else {
        QVERIFY(output.load(path));
    }
    QVERIFY(output.hasAlphaChannel());
    QCOMPARE(qAlpha(output.pixel(0, 0)), 0);
    QCOMPARE(qAlpha(output.pixel(8, 8)), 255);
}

void tst_GuiCommandOptions::handleResponse_missingImageIsError()
{
    auto gui = parseGui({"--raw"});
    QVERIFY(gui);
    const CLIResult result =
        gui->command.handleResponse(gui->parser, completedResponse(QRect(0, 0, 16, 9), 0));
    QCOMPARE(result.code, CLIResult::Code::GeneralError);
}

void tst_GuiCommandOptions::handleResponse_pinOnly()
{
    auto gui = parseGui({"--pin"});
    QVERIFY(gui);
    const CLIResult result =
        gui->command.handleResponse(gui->parser, completedResponse(QRect(0, 0, 16, 9), 0));
    QVERIFY(result.isSuccess());
    QVERIFY(result.data.isEmpty());
}

void tst_GuiCommandOptions::lastCaptureRegionSetting_roundTrip()
{
    auto& settings = RegionCaptureSettingsManager::instance();
    QVERIFY(!settings.lastCaptureRegion().has_value());

    settings.setLastCaptureRegion(QRect(-1280, -20, 640, 360), QStringLiteral("DP-1"));
    const auto stored = settings.lastCaptureRegion();
    QVERIFY(stored.has_value());
    QCOMPARE(stored->globalRect, QRect(-1280, -20, 640, 360));
    QCOMPARE(stored->screenName, QStringLiteral("DP-1"));

    // Empty rects are not recorded; corrupt values read back as nothing.
    settings.setLastCaptureRegion(QRect(), QStringLiteral("DP-2"));
    QCOMPARE(settings.lastCaptureRegion()->screenName, QStringLiteral("DP-1"));
    SnapTray::getSettings().setValue(kLastRegionKey, QStringLiteral("1,2,x,4"));
    QVERIFY(!settings.lastCaptureRegion().has_value());
}

void tst_GuiCommandOptions::ipcResponse_roundTripsCodeResultAndData()
{
    IPCResponse response = completedResponse(QRect(1, 2, 3, 4), 1, QByteArray("\x00\x01\xff png", 8));
    response.code = static_cast<int>(CLIResult::Code::Cancelled);
    const IPCResponse decoded = IPCResponse::fromJson(response.toJson());
    QCOMPARE(decoded.success, response.success);
    QCOMPARE(decoded.code, response.code);
    QCOMPARE(decoded.result, response.result);
    QCOMPARE(decoded.data, response.data);

    // Responses from older builds carry no code.
    IPCResponse legacy;
    legacy.success = true;
    QCOMPARE(IPCResponse::fromJson(legacy.toJson()).code, IPCResponse::kUnsetCode);
}

QTEST_MAIN(tst_GuiCommandOptions)
#include "tst_GuiCommandOptions.moc"
