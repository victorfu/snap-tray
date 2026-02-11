#include <QtTest/QtTest>

#include "TextFormattingState.h"
#include "region/RegionSettingsHelper.h"
#include "settings/Settings.h"

namespace {

constexpr const char* kTextBoldKey = "annotation/text_bold";
constexpr const char* kTextItalicKey = "annotation/text_italic";
constexpr const char* kTextUnderlineKey = "annotation/text_underline";
constexpr const char* kTextSizeKey = "annotation/text_size";
constexpr const char* kTextFamilyKey = "annotation/text_family";

constexpr const char* kLegacyTextBoldKey = "textBold";
constexpr const char* kLegacyTextItalicKey = "textItalic";
constexpr const char* kLegacyTextUnderlineKey = "textUnderline";
constexpr const char* kLegacyTextSizeKey = "textFontSize";
constexpr const char* kLegacyTextFamilyKey = "textFontFamily";

QStringList allTextSettingKeys()
{
    return {
        QString::fromLatin1(kTextBoldKey),
        QString::fromLatin1(kTextItalicKey),
        QString::fromLatin1(kTextUnderlineKey),
        QString::fromLatin1(kTextSizeKey),
        QString::fromLatin1(kTextFamilyKey),
        QString::fromLatin1(kLegacyTextBoldKey),
        QString::fromLatin1(kLegacyTextItalicKey),
        QString::fromLatin1(kLegacyTextUnderlineKey),
        QString::fromLatin1(kLegacyTextSizeKey),
        QString::fromLatin1(kLegacyTextFamilyKey)
    };
}

} // namespace

class tst_RegionSettingsHelper : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testLoadTextFormatting_UsesAnnotationKeysFirst();
    void testLoadTextFormatting_IgnoresLegacyKeys();
    void testLoadTextFormatting_UsesDefaultsWithoutKeys();

private:
    QHash<QString, QVariant> m_savedValues;
    QSet<QString> m_existingKeys;
};

void tst_RegionSettingsHelper::init()
{
    m_savedValues.clear();
    m_existingKeys.clear();

    QSettings settings = SnapTray::getSettings();
    for (const QString& key : allTextSettingKeys()) {
        if (!settings.contains(key)) {
            continue;
        }
        m_existingKeys.insert(key);
        m_savedValues.insert(key, settings.value(key));
    }
}

void tst_RegionSettingsHelper::cleanup()
{
    QSettings settings = SnapTray::getSettings();
    for (const QString& key : allTextSettingKeys()) {
        if (m_existingKeys.contains(key)) {
            settings.setValue(key, m_savedValues.value(key));
        } else {
            settings.remove(key);
        }
    }
}

void tst_RegionSettingsHelper::testLoadTextFormatting_UsesAnnotationKeysFirst()
{
    QSettings settings = SnapTray::getSettings();
    settings.setValue(kTextBoldKey, false);
    settings.setValue(kTextItalicKey, true);
    settings.setValue(kTextUnderlineKey, true);
    settings.setValue(kTextSizeKey, 30);
    settings.setValue(kTextFamilyKey, QStringLiteral("Georgia"));

    settings.setValue(kLegacyTextBoldKey, true);
    settings.setValue(kLegacyTextItalicKey, false);
    settings.setValue(kLegacyTextUnderlineKey, false);
    settings.setValue(kLegacyTextSizeKey, 12);
    settings.setValue(kLegacyTextFamilyKey, QStringLiteral("Arial"));

    const TextFormattingState state = RegionSettingsHelper::loadTextFormatting();
    QCOMPARE(state.bold, false);
    QCOMPARE(state.italic, true);
    QCOMPARE(state.underline, true);
    QCOMPARE(state.fontSize, 30);
    QCOMPARE(state.fontFamily, QStringLiteral("Georgia"));
}

void tst_RegionSettingsHelper::testLoadTextFormatting_IgnoresLegacyKeys()
{
    QSettings settings = SnapTray::getSettings();
    settings.remove(kTextBoldKey);
    settings.remove(kTextItalicKey);
    settings.remove(kTextUnderlineKey);
    settings.remove(kTextSizeKey);
    settings.remove(kTextFamilyKey);

    settings.setValue(kLegacyTextBoldKey, false);
    settings.setValue(kLegacyTextItalicKey, true);
    settings.setValue(kLegacyTextUnderlineKey, true);
    settings.setValue(kLegacyTextSizeKey, 26);
    settings.setValue(kLegacyTextFamilyKey, QStringLiteral("Verdana"));

    const TextFormattingState state = RegionSettingsHelper::loadTextFormatting();
    QCOMPARE(state.bold, true);
    QCOMPARE(state.italic, false);
    QCOMPARE(state.underline, false);
    QCOMPARE(state.fontSize, 16);
    QVERIFY(state.fontFamily.isEmpty());
}

void tst_RegionSettingsHelper::testLoadTextFormatting_UsesDefaultsWithoutKeys()
{
    QSettings settings = SnapTray::getSettings();
    for (const QString& key : allTextSettingKeys()) {
        settings.remove(key);
    }

    const TextFormattingState state = RegionSettingsHelper::loadTextFormatting();
    QCOMPARE(state.bold, true);
    QCOMPARE(state.italic, false);
    QCOMPARE(state.underline, false);
    QCOMPARE(state.fontSize, 16);
    QVERIFY(state.fontFamily.isEmpty());
}

QTEST_MAIN(tst_RegionSettingsHelper)
#include "tst_RegionSettingsHelper.moc"
