#include <QtTest>
#include "utils/OCRTextUtils.h"

class TestOCRTextUtils : public QObject
{
    Q_OBJECT
private slots:
    void joinWords_data();
    void joinWords();
};

void TestOCRTextUtils::joinWords_data()
{
    QTest::addColumn<QStringList>("words");
    QTest::addColumn<QString>("expected");
    const auto add = [](const char* name, const QString& input, const QString& expected) {
        QTest::newRow(name) << input.split(QLatin1Char('|')) << expected;
    };
    add("name", QStringLiteral("庭|宇"), QStringLiteral("庭宇"));
    add("mixed", QStringLiteral("因|為|要|用|vision|api|XD"), QStringLiteral("因為要用 vision api XD"));
    add("latin-name", QStringLiteral("傅|懷|磊|Victor"), QStringLiteral("傅懷磊 Victor"));
    add("time", QStringLiteral("19|:|52"), QStringLiteral("19:52"));
    add("numbered-step", QStringLiteral("Step|1:|2|items"), QStringLiteral("Step 1: 2 items"));
    add("attached-colon", QStringLiteral("19:|52"), QStringLiteral("19: 52"));
    add("non-number-right", QStringLiteral("1|:|x"), QStringLiteral("1 : x"));
    add("non-number-left", QStringLiteral("a|:|5"), QStringLiteral("a : 5"));
    add("fullwidth-time", QStringLiteral("19|：|52"), QStringLiteral("19：52"));
    add("time-empty-tokens", QStringLiteral("19||:||52"), QStringLiteral("19:52"));
    add("label", QStringLiteral("Time|:|19|:|52"), QStringLiteral("Time : 19:52"));
    add("english", QStringLiteral("hello|world"), QStringLiteral("hello world"));
    add("korean", QStringLiteral("안녕|세계"), QStringLiteral("안녕 세계"));
    add("japanese", QStringLiteral("日|本|語|で|す"), QStringLiteral("日本語です"));
    add("katakana-long-vowels", QStringLiteral("コ|ー|ヒ|ー"), QStringLiteral("コーヒー"));
    add("halfwidth-katakana", QStringLiteral("ｺ|ｰ|ﾋ|ｰ"), QStringLiteral("ｺｰﾋｰ"));
    add("fullwidth-latin", QStringLiteral("Ａ|Ｂ"), QStringLiteral("Ａ Ｂ"));
    add("ideographic-space", QStringLiteral("庭|　|宇"), QStringLiteral("庭　宇"));
    add("punctuation", QStringLiteral("你|好|，|世|界|！"), QStringLiteral("你好，世界！"));
    add("native-line", QStringLiteral("庭 宇 vision api XD"), QStringLiteral("庭 宇 vision api XD"));
    add("line-break", QStringLiteral("庭宇\n|Victor"), QStringLiteral("庭宇\nVictor"));
    add("supplementary-han", QString::fromUcs4(U"𠀀|宇"), QString::fromUcs4(U"𠀀宇"));
    add("empty", QString(), QString());
    add("empty-token", QStringLiteral("庭||宇"), QStringLiteral("庭宇"));
}

void TestOCRTextUtils::joinWords()
{
    QFETCH(QStringList, words);
    QFETCH(QString, expected);
    QCOMPARE(OCRTextUtils::joinWords(words), expected);
}

QTEST_APPLESS_MAIN(TestOCRTextUtils)
#include "tst_OCRTextUtils.moc"
