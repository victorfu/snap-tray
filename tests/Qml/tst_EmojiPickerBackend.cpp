#include <QtTest/QtTest>

#include "qml/EmojiPickerBackend.h"

#include <QSignalSpy>

class tst_EmojiPickerBackend : public QObject
{
    Q_OBJECT

private slots:
    void testEmojiModel_MatchesLegacyOrder();
    void testActivateEmoji_UpdatesCurrentAndEmits();
};

void tst_EmojiPickerBackend::testEmojiModel_MatchesLegacyOrder()
{
    SnapTray::EmojiPickerBackend backend;

    const QStringList expected = {
        QString::fromUtf8("\xF0\x9F\x98\x80"),
        QString::fromUtf8("\xF0\x9F\x98\x82"),
        QString::fromUtf8("\xE2\x9D\xA4\xEF\xB8\x8F"),
        QString::fromUtf8("\xF0\x9F\x91\x8D"),
        QString::fromUtf8("\xF0\x9F\x8E\x89"),
        QString::fromUtf8("\xE2\xAD\x90"),
        QString::fromUtf8("\xF0\x9F\x94\xA5"),
        QString::fromUtf8("\xE2\x9C\xA8"),
        QString::fromUtf8("\xF0\x9F\x92\xA1"),
        QString::fromUtf8("\xF0\x9F\x93\x8C"),
        QString::fromUtf8("\xF0\x9F\x8E\xAF"),
        QString::fromUtf8("\xE2\x9C\x85"),
        QString::fromUtf8("\xE2\x9D\x8C"),
        QString::fromUtf8("\xE2\x9A\xA0\xEF\xB8\x8F"),
        QString::fromUtf8("\xF0\x9F\x92\xAC"),
        QString::fromUtf8("\xF0\x9F\x94\x92"),
    };

    QCOMPARE(backend.currentEmoji(), expected.first());
    QCOMPARE(backend.emojiModel().size(), expected.size());
    for (int i = 0; i < expected.size(); ++i) {
        QCOMPARE(backend.emojiModel().at(i).toString(), expected.at(i));
    }
}

void tst_EmojiPickerBackend::testActivateEmoji_UpdatesCurrentAndEmits()
{
    SnapTray::EmojiPickerBackend backend;
    QSignalSpy currentSpy(&backend, &SnapTray::EmojiPickerBackend::currentEmojiChanged);
    QSignalSpy selectedSpy(&backend, &SnapTray::EmojiPickerBackend::emojiSelected);

    const QString emoji = QString::fromUtf8("\xF0\x9F\x92\xAC");
    backend.activateEmoji(emoji);

    QCOMPARE(backend.currentEmoji(), emoji);
    QCOMPARE(currentSpy.count(), 1);
    QCOMPARE(selectedSpy.count(), 1);
    QCOMPARE(selectedSpy.at(0).at(0).toString(), emoji);
}

QTEST_MAIN(tst_EmojiPickerBackend)
#include "tst_EmojiPickerBackend.moc"
