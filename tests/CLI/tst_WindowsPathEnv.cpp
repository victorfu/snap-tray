#include <QtTest>

#include "platform/PathEnvUtils_win.h"

class tst_WindowsPathEnv : public QObject
{
    Q_OBJECT

private slots:
    void normalizePathEntry_handlesSlashCaseQuotesAndSpaces();
    void normalizePathEntry_handlesEmptyAndWhitespaceInputs();
    void normalizePathEntry_handlesTrailingSlash();
    void normalizePathEntry_handlesWhitespaceAroundEntries();
    void containsPathEntry_doesNotMatchPrefixPath();
    void containsPathEntry_matchesEquivalentPaths();
    void containsPathEntry_matchesWithTrailingWhitespaceAndQuotes();
    void splitPathEntries_skipsEmptySegments();
    void installPathEntry_isNoopForEmptyTarget();
    void uninstallPathEntry_isNoopForEmptyTarget();
    void installPathEntry_preservesRawTargetString();
    void installPathEntry_appendsWhenMissing();
    void installPathEntry_isIdempotent();
    void installPathEntry_deduplicatesLegacyEntries();
    void uninstallPathEntry_removesAllEquivalentEntries();
    void uninstallPathEntry_keepsNonEquivalentEntries();
};

void tst_WindowsPathEnv::normalizePathEntry_handlesSlashCaseQuotesAndSpaces()
{
    const QString normalized = PathEnvUtils::normalizePathEntry("C:\\Program Files\\SnapTray");
    const QString mixed = PathEnvUtils::normalizePathEntry("  \"C:/Program Files/snaptray\"  ");
    QCOMPARE(normalized, mixed);
}

void tst_WindowsPathEnv::normalizePathEntry_handlesEmptyAndWhitespaceInputs()
{
    QCOMPARE(PathEnvUtils::normalizePathEntry(""), QString());
    QCOMPARE(PathEnvUtils::normalizePathEntry("   "), QString());
    QCOMPARE(PathEnvUtils::normalizePathEntry("\"\""), QString());
}

void tst_WindowsPathEnv::normalizePathEntry_handlesTrailingSlash()
{
    const QString normalized = PathEnvUtils::normalizePathEntry("C:/Program Files/SnapTray/");
    const QString withBackslash = PathEnvUtils::normalizePathEntry("C:\\Program Files\\snaptray\\");
    QCOMPARE(normalized, withBackslash);
}

void tst_WindowsPathEnv::normalizePathEntry_handlesWhitespaceAroundEntries()
{
    const QString normalized = PathEnvUtils::normalizePathEntry("  \"C:\\Program Files\\SnapTray\"  ");
    const QString clean = PathEnvUtils::normalizePathEntry("C:\\Program Files\\SnapTray");
    QCOMPARE(normalized, clean);
}

void tst_WindowsPathEnv::containsPathEntry_doesNotMatchPrefixPath()
{
    const QStringList entries = PathEnvUtils::splitPathEntries("C:\\Apps\\SnapShot;C:\\Tools");
    QVERIFY(!PathEnvUtils::containsPathEntry(entries, "C:\\Apps\\Snap"));
}

void tst_WindowsPathEnv::containsPathEntry_matchesEquivalentPaths()
{
    const QStringList entries = PathEnvUtils::splitPathEntries("C:/Program Files/SnapTray;C:\\Tools");
    QVERIFY(PathEnvUtils::containsPathEntry(entries, "C:\\Program Files\\snaptray"));
}

void tst_WindowsPathEnv::containsPathEntry_matchesWithTrailingWhitespaceAndQuotes()
{
    const QStringList entries = PathEnvUtils::splitPathEntries("  \"C:\\Program Files\\SnapTray\" ;C:\\Tools");
    QVERIFY(PathEnvUtils::containsPathEntry(entries, "C:\\Program Files\\snaptray"));
}

void tst_WindowsPathEnv::splitPathEntries_skipsEmptySegments()
{
    const QStringList entries = PathEnvUtils::splitPathEntries("C:\\Tools;;C:\\Bin");
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.at(0), QString("C:\\Tools"));
    QCOMPARE(entries.at(1), QString("C:\\Bin"));
}

void tst_WindowsPathEnv::installPathEntry_isNoopForEmptyTarget()
{
    const QStringList entries = {"C:\\Tools"};
    const auto result1 = PathEnvUtils::installPathEntry(entries, "");
    const auto result2 = PathEnvUtils::installPathEntry(entries, "   ");

    QVERIFY(!result1.changed);
    QVERIFY(!result2.changed);
    QCOMPARE(result1.entries, entries);
    QCOMPARE(result2.entries, entries);
}

void tst_WindowsPathEnv::uninstallPathEntry_isNoopForEmptyTarget()
{
    const QStringList entries = {"C:\\Tools"};
    const auto result1 = PathEnvUtils::uninstallPathEntry(entries, "");
    const auto result2 = PathEnvUtils::uninstallPathEntry(entries, "   ");

    QVERIFY(!result1.changed);
    QVERIFY(!result2.changed);
    QCOMPARE(result1.entries, entries);
    QCOMPARE(result2.entries, entries);
}

void tst_WindowsPathEnv::installPathEntry_preservesRawTargetString()
{
    const QStringList entries = {"C:\\Tools"};
    const QString target = "C:/Program Files/snaptray";
    const auto result = PathEnvUtils::installPathEntry(entries, target);

    QVERIFY(result.changed);
    QCOMPARE(result.entries.size(), 2);
    QCOMPARE(result.entries.at(1), target);
}

void tst_WindowsPathEnv::installPathEntry_appendsWhenMissing()
{
    const QStringList entries = {"C:\\Tools"};
    const auto result = PathEnvUtils::installPathEntry(entries, "C:\\Program Files\\SnapTray");

    QVERIFY(result.changed);
    QCOMPARE(result.entries.size(), 2);
    QCOMPARE(result.entries.at(0), QString("C:\\Tools"));
    QCOMPARE(result.entries.at(1), QString("C:\\Program Files\\SnapTray"));
}

void tst_WindowsPathEnv::installPathEntry_isIdempotent()
{
    const QStringList entries = {"C:\\Program Files\\SnapTray", "C:\\Tools"};
    const auto result = PathEnvUtils::installPathEntry(entries, "C:/Program Files/snaptray");

    QVERIFY(!result.changed);
    QCOMPARE(result.entries, entries);
}

void tst_WindowsPathEnv::installPathEntry_deduplicatesLegacyEntries()
{
    const QStringList entries = {
        "C:\\Program Files\\SnapTray",
        " \"C:/Program Files/snaptray\" ",
        "C:\\Tools",
        "\"C:\\Program Files\\SNAPTRAY\""
    };

    const auto result = PathEnvUtils::installPathEntry(entries, "C:\\Program Files\\SnapTray");

    QVERIFY(result.changed);
    QCOMPARE(result.entries.size(), 2);
    QCOMPARE(result.entries.at(0), entries.at(0)); // Keep first existing entry
    QCOMPARE(result.entries.at(1), QString("C:\\Tools"));
}

void tst_WindowsPathEnv::uninstallPathEntry_removesAllEquivalentEntries()
{
    const QStringList entries = {
        "C:\\Program Files\\SnapTray",
        " \"C:/Program Files/snaptray\" ",
        "\"C:\\Program Files\\SNAPTRAY\"",
        "C:\\Tools"
    };

    const auto result = PathEnvUtils::uninstallPathEntry(entries, "C:\\Program Files\\SnapTray");

    QVERIFY(result.changed);
    QCOMPARE(result.entries, QStringList({"C:\\Tools"}));
}

void tst_WindowsPathEnv::uninstallPathEntry_keepsNonEquivalentEntries()
{
    const QStringList entries = {
        "C:\\Program Files\\SnapShot",
        "C:\\Program Files\\SnapTrayTools",
        "C:\\Tools"
    };

    const auto result = PathEnvUtils::uninstallPathEntry(entries, "C:\\Program Files\\SnapTray");

    QVERIFY(!result.changed);
    QCOMPARE(result.entries, entries);
}

QTEST_MAIN(tst_WindowsPathEnv)
#include "tst_WindowsPathEnv.moc"
