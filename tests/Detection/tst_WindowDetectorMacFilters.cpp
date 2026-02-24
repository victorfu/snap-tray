#include <QtTest>

#include "WindowDetectorMacFilters.h"

class tst_WindowDetectorMacFilters : public QObject
{
    Q_OBJECT

private slots:
    void testTopLevelRejectsAlphaZero();
    void testTopLevelRejectsOnscreenFalse();
    void testTopLevelKeepsVisibleWindow();
    void testAXRejectsHiddenElement();
    void testAXRejectsVisibleFalseElement();
    void testAXRejectsContainerAtOrAboveRatioLimit();
    void testAXKeepsRegularRoleBelowRatioLimit();
};

void tst_WindowDetectorMacFilters::testTopLevelRejectsAlphaZero()
{
    QVERIFY(!WindowDetectorMacFilters::passesTopLevelVisibility(0.0, true));
}

void tst_WindowDetectorMacFilters::testTopLevelRejectsOnscreenFalse()
{
    QVERIFY(!WindowDetectorMacFilters::passesTopLevelVisibility(1.0, false));
}

void tst_WindowDetectorMacFilters::testTopLevelKeepsVisibleWindow()
{
    QVERIFY(WindowDetectorMacFilters::passesTopLevelVisibility(1.0, true));
}

void tst_WindowDetectorMacFilters::testAXRejectsHiddenElement()
{
    QVERIFY(WindowDetectorMacFilters::shouldRejectAxByVisibility(true, true));
}

void tst_WindowDetectorMacFilters::testAXRejectsVisibleFalseElement()
{
    QVERIFY(WindowDetectorMacFilters::shouldRejectAxByVisibility(false, false));
}

void tst_WindowDetectorMacFilters::testAXRejectsContainerAtOrAboveRatioLimit()
{
    const QString containerRole = QStringLiteral("AXGroup");
    QVERIFY(!WindowDetectorMacFilters::isAreaRatioAllowed(containerRole, 60.0, 100.0));
}

void tst_WindowDetectorMacFilters::testAXKeepsRegularRoleBelowRatioLimit()
{
    const QString regularRole = QStringLiteral("AXButton");
    QVERIFY(WindowDetectorMacFilters::isAreaRatioAllowed(regularRole, 89.0, 100.0));
}

QTEST_MAIN(tst_WindowDetectorMacFilters)
#include "tst_WindowDetectorMacFilters.moc"
