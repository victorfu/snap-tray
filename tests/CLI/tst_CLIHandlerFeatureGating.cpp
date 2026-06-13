#include <QtTest/QtTest>

#include "cli/CLIHandler.h"

class tst_CLIHandlerFeatureGating : public QObject
{
    Q_OBJECT

private slots:
    void helpHidesRecordWhenRecordingUnsupported();
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

QTEST_MAIN(tst_CLIHandlerFeatureGating)
#include "tst_CLIHandlerFeatureGating.moc"
