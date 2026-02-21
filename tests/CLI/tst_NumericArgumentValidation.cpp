#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include "cli/CLIHandler.h"
#include "cli/commands/FullCommand.h"
#include "cli/commands/PinCommand.h"
#include "cli/commands/RecordCommand.h"
#include "cli/commands/RegionCommand.h"
#include "cli/commands/ScreenCommand.h"

using SnapTray::CLI::CLIHandler;
using SnapTray::CLI::CLIResult;
using SnapTray::CLI::FullCommand;
using SnapTray::CLI::PinCommand;
using SnapTray::CLI::RecordCommand;
using SnapTray::CLI::RegionCommand;
using SnapTray::CLI::ScreenCommand;

class tst_NumericArgumentValidation : public QObject
{
    Q_OBJECT

private slots:
    void screenCommand_rejectsNonNumericScreenOption();
    void regionCommand_rejectsNonNumericScreenOption();
    void regionCommand_rejectsNonNumericDelayOption();
    void fullCommand_rejectsNonNumericScreenOption();
    void fullCommand_rejectsNonNumericDelayOption();
    void recordCommand_rejectsNonNumericScreenOption();
    void recordCommand_rejectsUnknownAction();
    void pinCommand_rejectsNonNumericPosition();
    void pinCommand_rejectsNonImageFile();
    void recordCommand_buildIPCMessage_skipsInvalidScreenValue();
    void pinCommand_buildIPCMessage_skipsInvalidPositionValues();
    void cliHandler_validatesGUICommandArgumentsBeforeIPC();
    void cliHandler_rejectsUnknownRecordActionBeforeIPC();
    void captureCommands_rejectUnsupportedCursorOption_data();
    void captureCommands_rejectUnsupportedCursorOption();
};

void tst_NumericArgumentValidation::screenCommand_rejectsNonNumericScreenOption()
{
    ScreenCommand command;
    QCommandLineParser parser;
    command.setupOptions(parser);

    QVERIFY(parser.parse({"snaptray", "--screen", "abc"}));
    CLIResult result = command.execute(parser);

    QCOMPARE(result.code, CLIResult::Code::InvalidArguments);
    QVERIFY(result.message.contains("Invalid screen number: abc"));
}

void tst_NumericArgumentValidation::regionCommand_rejectsNonNumericScreenOption()
{
    RegionCommand command;
    QCommandLineParser parser;
    command.setupOptions(parser);

    QVERIFY(parser.parse({"snaptray", "--region", "0,0,100,100", "--screen", "abc"}));
    CLIResult result = command.execute(parser);

    QCOMPARE(result.code, CLIResult::Code::InvalidArguments);
    QVERIFY(result.message.contains("Invalid screen number: abc"));
}

void tst_NumericArgumentValidation::regionCommand_rejectsNonNumericDelayOption()
{
    RegionCommand command;
    QCommandLineParser parser;
    command.setupOptions(parser);

    QVERIFY(parser.parse({"snaptray", "--region", "0,0,100,100", "--delay", "abc"}));
    CLIResult result = command.execute(parser);

    QCOMPARE(result.code, CLIResult::Code::InvalidArguments);
    QVERIFY(result.message.contains("Invalid delay value: abc"));
}

void tst_NumericArgumentValidation::fullCommand_rejectsNonNumericScreenOption()
{
    FullCommand command;
    QCommandLineParser parser;
    command.setupOptions(parser);

    QVERIFY(parser.parse({"snaptray", "--screen", "abc"}));
    CLIResult result = command.execute(parser);

    QCOMPARE(result.code, CLIResult::Code::InvalidArguments);
    QVERIFY(result.message.contains("Invalid screen number: abc"));
}

void tst_NumericArgumentValidation::fullCommand_rejectsNonNumericDelayOption()
{
    FullCommand command;
    QCommandLineParser parser;
    command.setupOptions(parser);

    QVERIFY(parser.parse({"snaptray", "--delay", "abc"}));
    CLIResult result = command.execute(parser);

    QCOMPARE(result.code, CLIResult::Code::InvalidArguments);
    QVERIFY(result.message.contains("Invalid delay value: abc"));
}

void tst_NumericArgumentValidation::recordCommand_rejectsNonNumericScreenOption()
{
    RecordCommand command;
    QCommandLineParser parser;
    command.setupOptions(parser);

    QVERIFY(parser.parse({"snaptray", "--screen", "abc"}));
    CLIResult result = command.execute(parser);

    QCOMPARE(result.code, CLIResult::Code::InvalidArguments);
    QVERIFY(result.message.contains("Invalid screen number: abc"));
}

void tst_NumericArgumentValidation::recordCommand_rejectsUnknownAction()
{
    RecordCommand command;
    QCommandLineParser parser;
    command.setupOptions(parser);

    QVERIFY(parser.parse({"snaptray", "foo"}));
    CLIResult result = command.execute(parser);

    QCOMPARE(result.code, CLIResult::Code::InvalidArguments);
    QVERIFY(result.message.contains("Invalid record action: foo"));
}

void tst_NumericArgumentValidation::pinCommand_rejectsNonNumericPosition()
{
    PinCommand command;
    QCommandLineParser parser;
    command.setupOptions(parser);

    QVERIFY(parser.parse({"snaptray", "--clipboard", "--pos-x", "abc"}));
    CLIResult result = command.execute(parser);

    QCOMPARE(result.code, CLIResult::Code::InvalidArguments);
    QVERIFY(result.message.contains("Invalid x position: abc"));
}

void tst_NumericArgumentValidation::pinCommand_rejectsNonImageFile()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString nonImagePath = tempDir.filePath("not-image.txt");
    QFile file(nonImagePath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(file.write("this is not an image") > 0);
    file.close();

    PinCommand command;
    QCommandLineParser parser;
    command.setupOptions(parser);

    QVERIFY(parser.parse({"snaptray", "--file", nonImagePath}));
    CLIResult result = command.execute(parser);

    QCOMPARE(result.code, CLIResult::Code::FileError);
    QVERIFY(result.message.contains("Unsupported or invalid image file"));
}

void tst_NumericArgumentValidation::recordCommand_buildIPCMessage_skipsInvalidScreenValue()
{
    RecordCommand command;
    QCommandLineParser parser;
    command.setupOptions(parser);

    QVERIFY(parser.parse({"snaptray", "--screen", "abc"}));
    const QJsonObject options = command.buildIPCMessage(parser);
    QVERIFY(!options.contains("screen"));
}

void tst_NumericArgumentValidation::pinCommand_buildIPCMessage_skipsInvalidPositionValues()
{
    PinCommand command;
    QCommandLineParser parser;
    command.setupOptions(parser);

    QVERIFY(parser.parse({"snaptray", "--clipboard", "--pos-x", "abc", "--pos-y", "xyz"}));
    const QJsonObject options = command.buildIPCMessage(parser);

    QVERIFY(!options.contains("x"));
    QVERIFY(!options.contains("y"));
}

void tst_NumericArgumentValidation::cliHandler_validatesGUICommandArgumentsBeforeIPC()
{
    CLIHandler handler;

    CLIResult recordResult = handler.process({"snaptray", "record", "--screen", "abc"});
    QCOMPARE(recordResult.code, CLIResult::Code::InvalidArguments);
    QVERIFY(recordResult.message.contains("Invalid screen number: abc"));

    CLIResult pinResult = handler.process({"snaptray", "pin", "--clipboard", "--pos-y", "bad"});
    QCOMPARE(pinResult.code, CLIResult::Code::InvalidArguments);
    QVERIFY(pinResult.message.contains("Invalid y position: bad"));
}

void tst_NumericArgumentValidation::cliHandler_rejectsUnknownRecordActionBeforeIPC()
{
    CLIHandler handler;

    const CLIResult result = handler.process({"snaptray", "record", "foo"});
    QCOMPARE(result.code, CLIResult::Code::InvalidArguments);
    QVERIFY(result.message.contains("Invalid record action: foo"));
}

void tst_NumericArgumentValidation::captureCommands_rejectUnsupportedCursorOption_data()
{
    QTest::addColumn<QString>("command");
    QTest::addColumn<QStringList>("args");

    QTest::newRow("full") << "full" << QStringList{"--cursor"};
    QTest::newRow("screen") << "screen" << QStringList{"0", "--cursor"};
    QTest::newRow("region") << "region" << QStringList{"--region", "0,0,100,100", "--cursor"};
}

void tst_NumericArgumentValidation::captureCommands_rejectUnsupportedCursorOption()
{
    QFETCH(QString, command);
    QFETCH(QStringList, args);

    CLIHandler handler;
    QStringList cliArgs{"snaptray", command};
    cliArgs.append(args);

    const CLIResult result = handler.process(cliArgs);
    QCOMPARE(result.code, CLIResult::Code::InvalidArguments);
    QVERIFY(result.message.contains("cursor", Qt::CaseInsensitive));
}

QTEST_MAIN(tst_NumericArgumentValidation)
#include "tst_NumericArgumentValidation.moc"
