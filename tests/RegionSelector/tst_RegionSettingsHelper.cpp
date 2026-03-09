#include <QtTest/QtTest>

#include "TextFormattingState.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/LineStyle.h"
#include "region/RegionSettingsHelper.h"
#include "settings/Settings.h"

#include <QAction>
#include <QGuiApplication>
#include <QMenu>
#include <QMutex>
#include <QMutexLocker>
#include <QTimer>
#include <QSignalSpy>

Q_DECLARE_METATYPE(LineEndStyle)
Q_DECLARE_METATYPE(LineStyle)

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

QMutex g_warningMutex;
QStringList g_warningMessages;
QtMessageHandler g_previousHandler = nullptr;

void warningCaptureHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    Q_UNUSED(context);

    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        QMutexLocker locker(&g_warningMutex);
        g_warningMessages.push_back(message);
    }

    if (g_previousHandler) {
        g_previousHandler(type, context, message);
    }
}

class WarningCaptureScope
{
public:
    WarningCaptureScope()
    {
        QMutexLocker locker(&g_warningMutex);
        g_warningMessages.clear();
        g_previousHandler = qInstallMessageHandler(warningCaptureHandler);
    }

    ~WarningCaptureScope()
    {
        qInstallMessageHandler(g_previousHandler);
        g_previousHandler = nullptr;
    }

    QStringList svgWarnings() const
    {
        QMutexLocker locker(&g_warningMutex);
        QStringList filtered;
        for (const QString& message : g_warningMessages) {
            if (message.contains(QStringLiteral("qt.svg:"), Qt::CaseInsensitive)
                || message.contains(QStringLiteral("Cannot open file"), Qt::CaseInsensitive)) {
                filtered.push_back(message);
            }
        }
        return filtered;
    }
}
;

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
    void testShowMenu_EmitsDropdownLifecycleSignals();
    void testShowArrowStyleDropdown_EmitsSelectedStyle();
    void testShowLineStyleDropdown_EmitsSelectedStyle();

private:
    QHash<QString, QVariant> m_savedValues;
    QSet<QString> m_existingKeys;
};

void tst_RegionSettingsHelper::init()
{
    qRegisterMetaType<LineEndStyle>("LineEndStyle");
    qRegisterMetaType<LineStyle>("LineStyle");

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

void tst_RegionSettingsHelper::testShowMenu_EmitsDropdownLifecycleSignals()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("No screens available for popup menu tests in this environment.");
    }

    RegionSettingsHelper helper;
    QMenu menu;
    QSignalSpy shownSpy(&helper, &RegionSettingsHelper::dropdownShown);
    QSignalSpy hiddenSpy(&helper, &RegionSettingsHelper::dropdownHidden);

    QObject::connect(&menu, &QMenu::aboutToShow, &menu, [&menu]() {
        QTimer::singleShot(0, &menu, &QMenu::close);
    });

    helper.showMenu(&menu, QPoint(10, 10));

    QCOMPARE(shownSpy.count(), 1);
    QCOMPARE(hiddenSpy.count(), 1);
}

void tst_RegionSettingsHelper::testShowArrowStyleDropdown_EmitsSelectedStyle()
{
    RegionSettingsHelper helper;
    QSignalSpy spy(&helper, &RegionSettingsHelper::arrowStyleSelected);
    QMenu menu;
    WarningCaptureScope warningScope;

    helper.populateArrowStyleMenu(&menu, LineEndStyle::EndArrowOutline);

    const auto actions = menu.actions();
    QCOMPARE(actions.size(), 6);
    QVERIFY(!actions.at(0)->icon().isNull());
    QVERIFY2(warningScope.svgWarnings().isEmpty(),
             qPrintable(warningScope.svgWarnings().join('\n')));
    QVERIFY(actions.at(2)->isChecked());
    actions.at(4)->trigger();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), static_cast<int>(LineEndStyle::BothArrow));
}

void tst_RegionSettingsHelper::testShowLineStyleDropdown_EmitsSelectedStyle()
{
    RegionSettingsHelper helper;
    QSignalSpy spy(&helper, &RegionSettingsHelper::lineStyleSelected);
    QMenu menu;
    WarningCaptureScope warningScope;

    helper.populateLineStyleMenu(&menu, LineStyle::Dashed);

    const auto actions = menu.actions();
    QCOMPARE(actions.size(), 3);
    QVERIFY(!actions.at(0)->icon().isNull());
    QVERIFY2(warningScope.svgWarnings().isEmpty(),
             qPrintable(warningScope.svgWarnings().join('\n')));
    QVERIFY(actions.at(1)->isChecked());
    actions.at(2)->trigger();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), static_cast<int>(LineStyle::Dotted));
}

QTEST_MAIN(tst_RegionSettingsHelper)
#include "tst_RegionSettingsHelper.moc"
