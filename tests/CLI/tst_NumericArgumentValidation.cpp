#include <QtTest>

#include "cli/CLIHandler.h"
#include "cli/commands/PinCommand.h"
#include "cli/commands/RecordCommand.h"
#include "cli/commands/RegionCommand.h"
#include "cli/commands/ScreenCommand.h"

using SnapTray::CLI::CLIHandler;
using SnapTray::CLI::CLIResult;
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
    void recordCommand_rejectsNonNumericScreenOption();
    void pinCommand_rejectsNonNumericPosition();
    void recordCommand_buildIPCMessage_skipsInvalidScreenValue();
    void pinCommand_buildIPCMessage_skipsInvalidPositionValues();
    void cliHandler_validatesGUICommandArgumentsBeforeIPC();
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

QTEST_MAIN(tst_NumericArgumentValidation)
#include "tst_NumericArgumentValidation.moc"
