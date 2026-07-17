#include <QtTest/QtTest>

#include "cli/CLIHandler.h"

class tst_CLIHandlerFeatureGating : public QObject
{
    Q_OBJECT

private slots:
    void helpOmitsRemovedRecordCommand();
    void recordCommandIsRejected();
};

void tst_CLIHandlerFeatureGating::helpOmitsRemovedRecordCommand()
{
    SnapTray::CLI::CLIHandler handler;
    const QString help = handler.getHelpText();

    QVERIFY(!help.contains(QStringLiteral("record"), Qt::CaseInsensitive));
}

void tst_CLIHandlerFeatureGating::recordCommandIsRejected()
{
    SnapTray::CLI::CLIHandler handler;
    const SnapTray::CLI::CLIResult result =
        handler.process({QStringLiteral("snaptray"), QStringLiteral("record")});

    QCOMPARE(result.code, SnapTray::CLI::CLIResult::Code::InvalidArguments);
    QVERIFY(result.message.contains(QStringLiteral("Unknown command: record")));
}

QTEST_MAIN(tst_CLIHandlerFeatureGating)
#include "tst_CLIHandlerFeatureGating.moc"
