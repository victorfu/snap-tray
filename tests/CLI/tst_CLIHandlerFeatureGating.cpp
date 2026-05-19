#include <QtTest/QtTest>

#include "cli/CLIHandler.h"
#include "cli/CLIResult.h"

class tst_CLIHandlerFeatureGating : public QObject
{
    Q_OBJECT

private slots:
    void helpHidesRecordWhenRecordingUnsupported();
    void directRecordInvocationFailsWhenRecordingUnsupported();
};

void tst_CLIHandlerFeatureGating::helpHidesRecordWhenRecordingUnsupported()
{
    SnapTray::CLI::CLIHandler handler;
    const QString help = handler.getHelpText();

#if defined(Q_OS_LINUX)
    QVERIFY(!help.contains(QStringLiteral("record")));
    QVERIFY(!help.contains(QStringLiteral("Record screen")));
#else
    QVERIFY(help.contains(QStringLiteral("record")));
#endif
}

void tst_CLIHandlerFeatureGating::directRecordInvocationFailsWhenRecordingUnsupported()
{
    SnapTray::CLI::CLIHandler handler;
    const auto result = handler.process({QStringLiteral("snaptray"), QStringLiteral("record")});

#if defined(Q_OS_LINUX)
    QVERIFY(!result.isSuccess());
    QCOMPARE(result.code, SnapTray::CLI::CLIResult::Code::RecordingError);
    QVERIFY(result.message.contains(QStringLiteral("Linux beta")));
    QVERIFY(result.message.contains(QStringLiteral("recording")));
#else
    QVERIFY(!result.isSuccess());
    QCOMPARE(result.code, SnapTray::CLI::CLIResult::Code::InstanceError);
#endif
}

QTEST_MAIN(tst_CLIHandlerFeatureGating)
#include "tst_CLIHandlerFeatureGating.moc"
