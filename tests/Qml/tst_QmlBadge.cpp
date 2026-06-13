#include <QtTest/QtTest>

#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWidget>
#include <QWidget>

#include "qml/QmlBadge.h"

class tst_QmlBadge : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testAutoHideClearsWidgetVisibility();
    void testCanShowAgainAfterAutoHide();
    void testRetriggerDuringFadeOutRestoresVisibleState();
};

void tst_QmlBadge::initTestCase()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("No screens available for QmlBadge tests in this environment.");
    }
}

void tst_QmlBadge::testAutoHideClearsWidgetVisibility()
{
    QWidget host;
    host.resize(240, 160);
    host.show();
    QTRY_VERIFY(host.isVisible());

    SnapTray::QmlBadge badge(&host);
    badge.showBadge(QStringLiteral("100%"), 40);

    QTRY_VERIFY(badge.isVisible());
    QTRY_VERIFY(!badge.isVisible());
}

void tst_QmlBadge::testCanShowAgainAfterAutoHide()
{
    QWidget host;
    host.resize(240, 160);
    host.show();
    QTRY_VERIFY(host.isVisible());

    SnapTray::QmlBadge badge(&host);
    badge.showBadge(QStringLiteral("100%"), 40);

    QTRY_VERIFY(badge.isVisible());
    QTRY_VERIFY(!badge.isVisible());

    badge.showBadge(QStringLiteral("125%"), 40);
    QTRY_VERIFY(badge.isVisible());
    QTRY_VERIFY(!badge.isVisible());
}

void tst_QmlBadge::testRetriggerDuringFadeOutRestoresVisibleState()
{
    QWidget host;
    host.resize(240, 160);
    host.show();
    QTRY_VERIFY(host.isVisible());

    SnapTray::QmlBadge badge(&host);
    badge.showBadge(QStringLiteral("100%"), 40);
    QTRY_VERIFY(badge.isVisible());

    auto* quickWidget = badge.findChild<QQuickWidget*>();
    QVERIFY(quickWidget != nullptr);
    QQuickItem* rootObject = quickWidget->rootObject();
    QVERIFY(rootObject != nullptr);

    QTRY_VERIFY(!rootObject->property("badgeVisible").toBool());

    badge.showBadge(QStringLiteral("110%"), 40);

    QTRY_VERIFY(rootObject->property("badgeVisible").toBool());
    QTRY_VERIFY(badge.isVisible());
    QTRY_VERIFY(!badge.isVisible());
}

QTEST_MAIN(tst_QmlBadge)
#include "tst_QmlBadge.moc"
