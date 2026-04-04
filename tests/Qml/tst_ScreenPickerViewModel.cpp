#include <QtTest/QtTest>
#include <QGuiApplication>
#include <QImage>
#include <QScreen>
#include <QSignalSpy>

#include "qml/ScreenPickerViewModel.h"

class TestScreenPickerViewModel : public QObject
{
    Q_OBJECT

private slots:
    void setSources_populatesScreens();
    void chooseScreen_emitsChosenScreen();
    void chooseScreen_outOfRangeDoesNotEmit();
    void cancel_emitsCancelled();
};

void TestScreenPickerViewModel::setSources_populatesScreens()
{
    QScreen* primaryScreen = QGuiApplication::primaryScreen();
    if (!primaryScreen) {
        QSKIP("No screen available for ScreenPickerViewModel test");
    }

    SnapTray::ScreenSource sourceA;
    sourceA.id = QStringLiteral("screen-a");
    sourceA.name = QStringLiteral("Display A");
    sourceA.resolutionText = QStringLiteral("1920 x 1080");
    sourceA.geometry = QRect(0, 0, 1920, 1080);
    sourceA.thumbnail = QImage(16, 9, QImage::Format_ARGB32_Premultiplied);
    sourceA.screen = primaryScreen;

    SnapTray::ScreenSource sourceB = sourceA;
    sourceB.id = QStringLiteral("screen-b");
    sourceB.name = QStringLiteral("Display B");
    sourceB.resolutionText = QStringLiteral("2560 x 1440");
    sourceB.geometry = QRect(0, 0, 2560, 1440);

    SnapTray::ScreenPickerViewModel viewModel({sourceA, sourceB});

    const QVariantList screens = viewModel.screens();
    QCOMPARE(screens.size(), 2);

    const QVariantMap first = screens.at(0).toMap();
    QCOMPARE(first.value(QStringLiteral("name")).toString(), QStringLiteral("Display A"));
    QCOMPARE(first.value(QStringLiteral("resolution")).toString(), QStringLiteral("1920 x 1080"));
    QVERIFY(first.value(QStringLiteral("hasThumbnail")).toBool());
    QCOMPARE(first.value(QStringLiteral("thumbnailId")).toString(), QStringLiteral("screen_picker_0"));
}

void TestScreenPickerViewModel::chooseScreen_emitsChosenScreen()
{
    QScreen* primaryScreen = QGuiApplication::primaryScreen();
    if (!primaryScreen) {
        QSKIP("No screen available for ScreenPickerViewModel test");
    }

    qRegisterMetaType<QScreen*>("QScreen*");

    SnapTray::ScreenSource source;
    source.id = QStringLiteral("screen-a");
    source.name = QStringLiteral("Display A");
    source.resolutionText = QStringLiteral("1920 x 1080");
    source.geometry = QRect(0, 0, 1920, 1080);
    source.screen = primaryScreen;

    SnapTray::ScreenPickerViewModel viewModel({source});
    QSignalSpy chosenSpy(&viewModel, &SnapTray::ScreenPickerViewModel::screenChosen);
    viewModel.chooseScreen(0);

    QCOMPARE(chosenSpy.count(), 1);
    QCOMPARE(qvariant_cast<QScreen*>(chosenSpy.at(0).at(0)), primaryScreen);
}

void TestScreenPickerViewModel::chooseScreen_outOfRangeDoesNotEmit()
{
    QScreen* primaryScreen = QGuiApplication::primaryScreen();
    if (!primaryScreen) {
        QSKIP("No screen available for ScreenPickerViewModel test");
    }

    SnapTray::ScreenSource source;
    source.id = QStringLiteral("screen-a");
    source.name = QStringLiteral("Display A");
    source.resolutionText = QStringLiteral("1920 x 1080");
    source.geometry = QRect(0, 0, 1920, 1080);
    source.screen = primaryScreen;

    SnapTray::ScreenPickerViewModel viewModel({source});
    QSignalSpy chosenSpy(&viewModel, &SnapTray::ScreenPickerViewModel::screenChosen);
    viewModel.chooseScreen(-1);
    viewModel.chooseScreen(1);

    QCOMPARE(chosenSpy.count(), 0);
}

void TestScreenPickerViewModel::cancel_emitsCancelled()
{
    SnapTray::ScreenPickerViewModel viewModel;
    QSignalSpy cancelledSpy(&viewModel, &SnapTray::ScreenPickerViewModel::cancelled);

    viewModel.cancel();

    QCOMPARE(cancelledSpy.count(), 1);
}

QTEST_MAIN(TestScreenPickerViewModel)
#include "tst_ScreenPickerViewModel.moc"
