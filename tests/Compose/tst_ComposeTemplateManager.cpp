#include <QtTest/QtTest>

#include "compose/ComposeTemplateManager.h"

class tst_ComposeTemplateManager : public QObject
{
    Q_OBJECT

private slots:
    void testBuiltInTemplatesExist();
    void testFreeTemplateIsEmpty();
};

void tst_ComposeTemplateManager::testBuiltInTemplatesExist()
{
    auto& manager = ComposeTemplateManager::instance();
    const QVector<ComposeTemplate> templates = manager.templates();

    QVERIFY(templates.size() >= 4);

    const ComposeTemplate bugReport = manager.templateById(QStringLiteral("bug_report"));
    QCOMPARE(bugReport.id, QStringLiteral("bug_report"));
    QVERIFY(bugReport.bodyTemplate.contains(QStringLiteral("Steps to Reproduce")));
}

void tst_ComposeTemplateManager::testFreeTemplateIsEmpty()
{
    auto& manager = ComposeTemplateManager::instance();
    const ComposeTemplate freeTemplate = manager.templateById(QStringLiteral("free"));

    QCOMPARE(freeTemplate.id, QStringLiteral("free"));
    QVERIFY(freeTemplate.bodyTemplate.isEmpty());
}

QTEST_MAIN(tst_ComposeTemplateManager)
#include "tst_ComposeTemplateManager.moc"
