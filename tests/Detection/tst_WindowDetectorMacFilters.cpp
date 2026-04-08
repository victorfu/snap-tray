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
    void testPriorityContainerRoleClassification();
    void testGenericContainerRoleClassification();
    void testMenuContainerRoleClassification();
    void testMenuItemRoleClassification();
    void testAXRejectsContainerAtOrAboveRatioLimit();
    void testAXKeepsRegularRoleBelowRatioLimit();
    void testCandidateBoundsAcceptMeaningfulInset();
    void testCandidateBoundsAcceptMeaningfulAreaReduction();
    void testCandidateBoundsRejectsOutsideHitPoint();
    void testCandidateBoundsRejectsBelowMinimumSize();
    void testCandidateBoundsRejectsTinyAdjustment();
    void testPriorityContainerBeatsLeafCandidate();
    void testGenericContainerBeatsLeafCandidate();
    void testLargerContainerWinsWithinTier();
    void testLeafCandidatePrefersDeeperDepth();
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

void tst_WindowDetectorMacFilters::testPriorityContainerRoleClassification()
{
    QCOMPARE(WindowDetectorMacFilters::candidateTierForRole(QStringLiteral("AXSheet")),
             WindowDetectorMacFilters::AxCandidateTier::PriorityContainer);
    QCOMPARE(WindowDetectorMacFilters::candidateTierForRole(QStringLiteral("AXDialog")),
             WindowDetectorMacFilters::AxCandidateTier::PriorityContainer);
}

void tst_WindowDetectorMacFilters::testGenericContainerRoleClassification()
{
    QCOMPARE(WindowDetectorMacFilters::candidateTierForRole(QStringLiteral("AXGroup")),
             WindowDetectorMacFilters::AxCandidateTier::GenericContainer);
    QCOMPARE(WindowDetectorMacFilters::candidateTierForRole(QStringLiteral("AXScrollArea")),
             WindowDetectorMacFilters::AxCandidateTier::GenericContainer);
    QCOMPARE(WindowDetectorMacFilters::candidateTierForRole(QStringLiteral("AXButton")),
             WindowDetectorMacFilters::AxCandidateTier::LeafControl);
}

void tst_WindowDetectorMacFilters::testMenuContainerRoleClassification()
{
    QVERIFY(WindowDetectorMacFilters::isMenuContainerRole(QStringLiteral("AXMenu")));
    QVERIFY(!WindowDetectorMacFilters::isMenuContainerRole(QStringLiteral("AXButton")));
}

void tst_WindowDetectorMacFilters::testMenuItemRoleClassification()
{
    QVERIFY(WindowDetectorMacFilters::isMenuItemRole(QStringLiteral("AXMenuItem")));
    QVERIFY(WindowDetectorMacFilters::isMenuItemRole(QStringLiteral("AXMenuBarItem")));
    QVERIFY(!WindowDetectorMacFilters::isMenuItemRole(QStringLiteral("AXRow")));
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

void tst_WindowDetectorMacFilters::testCandidateBoundsAcceptMeaningfulInset()
{
    const QRect rawBounds(0, 0, 100, 100);
    const QRect candidateBounds(4, 0, 96, 100);
    const QPoint hitPoint(40, 40);

    QVERIFY(WindowDetectorMacFilters::shouldAcceptCandidateBounds(
        rawBounds, candidateBounds, hitPoint, 30));
}

void tst_WindowDetectorMacFilters::testCandidateBoundsAcceptMeaningfulAreaReduction()
{
    const QRect rawBounds(0, 0, 100, 100);
    const QRect candidateBounds(3, 3, 94, 94);
    const QPoint hitPoint(40, 40);

    QVERIFY(WindowDetectorMacFilters::shouldAcceptCandidateBounds(
        rawBounds, candidateBounds, hitPoint, 30));
}

void tst_WindowDetectorMacFilters::testCandidateBoundsRejectsOutsideHitPoint()
{
    const QRect rawBounds(0, 0, 120, 120);
    const QRect candidateBounds(10, 10, 80, 80);
    const QPoint hitPoint(5, 5);

    QVERIFY(!WindowDetectorMacFilters::shouldAcceptCandidateBounds(
        rawBounds, candidateBounds, hitPoint, 30));
}

void tst_WindowDetectorMacFilters::testCandidateBoundsRejectsBelowMinimumSize()
{
    const QRect rawBounds(0, 0, 120, 120);
    const QRect candidateBounds(8, 8, 20, 20);
    const QPoint hitPoint(12, 12);

    QVERIFY(!WindowDetectorMacFilters::shouldAcceptCandidateBounds(
        rawBounds, candidateBounds, hitPoint, 30));
}

void tst_WindowDetectorMacFilters::testCandidateBoundsRejectsTinyAdjustment()
{
    const QRect rawBounds(0, 0, 100, 100);
    const QRect candidateBounds(1, 1, 98, 98);
    const QPoint hitPoint(40, 40);

    QVERIFY(!WindowDetectorMacFilters::shouldAcceptCandidateBounds(
        rawBounds, candidateBounds, hitPoint, 30));
}

void tst_WindowDetectorMacFilters::testPriorityContainerBeatsLeafCandidate()
{
    QVERIFY(WindowDetectorMacFilters::shouldPreferAxCandidate(
        WindowDetectorMacFilters::AxCandidateTier::PriorityContainer, 6000.0, 2,
        WindowDetectorMacFilters::AxCandidateTier::LeafControl, 800.0, 0));
}

void tst_WindowDetectorMacFilters::testGenericContainerBeatsLeafCandidate()
{
    QVERIFY(WindowDetectorMacFilters::shouldPreferAxCandidate(
        WindowDetectorMacFilters::AxCandidateTier::GenericContainer, 5000.0, 2,
        WindowDetectorMacFilters::AxCandidateTier::LeafControl, 900.0, 0));
}

void tst_WindowDetectorMacFilters::testLargerContainerWinsWithinTier()
{
    QVERIFY(WindowDetectorMacFilters::shouldPreferAxCandidate(
        WindowDetectorMacFilters::AxCandidateTier::GenericContainer, 7200.0, 2,
        WindowDetectorMacFilters::AxCandidateTier::GenericContainer, 6400.0, 1));
}

void tst_WindowDetectorMacFilters::testLeafCandidatePrefersDeeperDepth()
{
    QVERIFY(!WindowDetectorMacFilters::shouldPreferAxCandidate(
        WindowDetectorMacFilters::AxCandidateTier::LeafControl, 900.0, 2,
        WindowDetectorMacFilters::AxCandidateTier::LeafControl, 1200.0, 1));
}

QTEST_MAIN(tst_WindowDetectorMacFilters)
#include "tst_WindowDetectorMacFilters.moc"
