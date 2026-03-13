#include <QtTest/QtTest>

#include <QWidget>

#include "cursor/CursorAuthority.h"

class TestCursorAuthority : public QObject
{
    Q_OBJECT

private slots:
    void testPriorityResolution();
    void testLegacyMismatchTracking();
    void testGroupModeDefaultsAndOverride();
};

void TestCursorAuthority::testPriorityResolution()
{
    QWidget widget;
    auto& authority = CursorAuthority::instance();
    authority.registerWidgetSurface(&widget, QStringLiteral("RegionSelector"),
                                    CursorSurfaceMode::Authority);

    authority.submitWidgetRequest(
        &widget, QStringLiteral("tool"), CursorRequestSource::Tool,
        CursorStyleSpec::fromShape(Qt::CrossCursor));
    authority.submitWidgetRequest(
        &widget, QStringLiteral("overlay"), CursorRequestSource::Overlay,
        CursorStyleSpec::fromShape(Qt::PointingHandCursor));

    QCOMPARE(authority.resolvedSourceForWidget(&widget), CursorRequestSource::Overlay);
    QCOMPARE(authority.resolvedStyleForWidget(&widget).styleId, CursorStyleId::PointingHand);

    authority.submitWidgetRequest(
        &widget, QStringLiteral("popup"), CursorRequestSource::Popup,
        CursorStyleSpec::fromShape(Qt::ArrowCursor));
    QCOMPARE(authority.resolvedSourceForWidget(&widget), CursorRequestSource::Popup);
    QCOMPARE(authority.resolvedStyleForWidget(&widget).styleId, CursorStyleId::Arrow);

    authority.clearWidgetRequest(&widget, QStringLiteral("popup"));
    QCOMPARE(authority.resolvedSourceForWidget(&widget), CursorRequestSource::Overlay);

    authority.clearWidgetRequest(&widget, QStringLiteral("overlay"));
    QCOMPARE(authority.resolvedSourceForWidget(&widget), CursorRequestSource::Tool);

    authority.unregisterWidgetSurface(&widget);
}

void TestCursorAuthority::testLegacyMismatchTracking()
{
    QWidget widget;
    auto& authority = CursorAuthority::instance();
    authority.registerWidgetSurface(&widget, QStringLiteral("RegionSelector"),
                                    CursorSurfaceMode::Authority);

    authority.submitWidgetRequest(
        &widget, QStringLiteral("overlay"), CursorRequestSource::Overlay,
        CursorStyleSpec::fromShape(Qt::PointingHandCursor));
    authority.recordLegacyAppliedForWidget(&widget, CursorStyleSpec::fromShape(Qt::CrossCursor));
    QVERIFY(authority.lastLegacyComparisonMismatched(authority.surfaceIdForWidget(&widget)));

    authority.recordLegacyAppliedForWidget(
        &widget, CursorStyleSpec::fromShape(Qt::PointingHandCursor));
    QVERIFY(!authority.lastLegacyComparisonMismatched(authority.surfaceIdForWidget(&widget)));

    authority.unregisterWidgetSurface(&widget);
}

void TestCursorAuthority::testGroupModeDefaultsAndOverride()
{
    const QByteArray envKey("SNAPTRAY_CURSOR_MODE_GROUP_REGIONSELECTOR");
    const QByteArray originalValue = qgetenv(envKey.constData());

    qunsetenv(envKey.constData());
    QCOMPARE(CursorAuthority::defaultManagedModeForGroup(QStringLiteral("RegionSelector")),
             CursorSurfaceMode::Authority);
    QCOMPARE(CursorAuthority::defaultManagedModeForGroup(QStringLiteral("QmlFloatingToolbar")),
             CursorSurfaceMode::Authority);
    QCOMPARE(CursorAuthority::defaultManagedModeForGroup(QStringLiteral("QmlOverlayPanel")),
             CursorSurfaceMode::Authority);
    QCOMPARE(CursorAuthority::defaultManagedModeForGroup(QStringLiteral("QmlEmojiPickerPopup")),
             CursorSurfaceMode::Authority);
    QCOMPARE(CursorAuthority::defaultManagedModeForGroup(QStringLiteral("UnknownSurface")),
             CursorSurfaceMode::Shadow);

    qputenv(envKey.constData(), QByteArrayLiteral("shadow"));
    QCOMPARE(CursorAuthority::defaultManagedModeForGroup(QStringLiteral("RegionSelector")),
             CursorSurfaceMode::Shadow);

    qputenv(envKey.constData(), QByteArrayLiteral("legacy"));
    QCOMPARE(CursorAuthority::defaultManagedModeForGroup(QStringLiteral("RegionSelector")),
             CursorSurfaceMode::Legacy);

    if (originalValue.isEmpty()) {
        qunsetenv(envKey.constData());
    } else {
        qputenv(envKey.constData(), originalValue);
    }
}

QTEST_MAIN(TestCursorAuthority)
#include "tst_CursorAuthority.moc"
