#include <QtTest/QtTest>
#include <QVariantList>
#include <QVariantMap>
#include <QStringList>

#include "qml/ShortcutHintsViewModel.h"

class tst_CaptureShortcutHintsOverlay : public QObject
{
    Q_OBJECT

private slots:
    void testHintCount();
    void testHintStructure();
    void testHintKeys();
    void testMultiKeyCombo();
};

void tst_CaptureShortcutHintsOverlay::testHintCount()
{
    ShortcutHintsViewModel vm;
    QCOMPARE(vm.hints().size(), 7);
}

void tst_CaptureShortcutHintsOverlay::testHintStructure()
{
    ShortcutHintsViewModel vm;
    const QVariantList hints = vm.hints();

    for (const auto& hint : hints) {
        QVariantMap map = hint.toMap();
        QVERIFY2(map.contains("keys"), "Each hint must have 'keys'");
        QVERIFY2(map.contains("description"), "Each hint must have 'description'");
        QVERIFY2(!map["description"].toString().isEmpty(), "Description must not be empty");

        QStringList keys = map["keys"].toStringList();
        QVERIFY2(!keys.isEmpty(), "Keys list must not be empty");
    }
}

void tst_CaptureShortcutHintsOverlay::testHintKeys()
{
    ShortcutHintsViewModel vm;
    const QVariantList hints = vm.hints();

    // First hint should be Esc
    QVariantMap first = hints.first().toMap();
    QStringList keys = first["keys"].toStringList();
    QCOMPARE(keys.size(), 1);
    QCOMPARE(keys.first(), QStringLiteral("Esc"));
}

void tst_CaptureShortcutHintsOverlay::testMultiKeyCombo()
{
    ShortcutHintsViewModel vm;
    const QVariantList hints = vm.hints();

    // Last hint should be Shift + Arrow (multi-key combo)
    QVariantMap last = hints.last().toMap();
    QStringList keys = last["keys"].toStringList();
    QCOMPARE(keys.size(), 2);
    QCOMPARE(keys.at(0), QStringLiteral("Shift"));
    QCOMPARE(keys.at(1), QStringLiteral("Arrow"));
}

QTEST_MAIN(tst_CaptureShortcutHintsOverlay)
#include "tst_CaptureShortcutHintsOverlay.moc"
