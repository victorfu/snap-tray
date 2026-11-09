// Screenshots of the Library window (#226), for
// docs/developer/ui-design-guidelines.md. Runs only when its variables are set:
//   SNAPTRAY_LIBRARY_DESIGN_DIR    screenshots
//   SNAPTRAY_LIBRARY_TRANSLATION   a .qm to load (language added to the names)
#include <QtTest>
#include <QFileInfo>
#include <QPainter>
#include <QQuickItem>
#include <QQuickView>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTranslator>
#include <QtQml/qqmlextensionplugin.h>

#include "ToolbarStyle.h"
#include "history/HistoryStore.h"
#include "qml/LibraryModel.h"
#include "qml/QmlHistoryWindow.h"
#include "recording/RecordingProject.h"
#include "settings/RecordingSettingsManager.h"
#include "ui/DesignSystem.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

using SnapTray::LibraryModel;
using SnapTray::QmlHistoryWindow;

namespace {

QImage demoImage(QSize size, const QColor& accent, const QString& title, int rows)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(image.rect(), QColor("#fbfbfd"));
    p.fillRect(QRect(0, 0, size.width(), 40), accent);
    p.setPen(Qt::white);
    QFont font = p.font();
    font.setPixelSize(18);
    font.setBold(true);
    p.setFont(font);
    p.drawText(QRect(18, 0, size.width(), 40), Qt::AlignVCenter, title);
    for (int i = 0; i < rows; ++i) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#d1d4dc"));
        p.drawRoundedRect(QRectF(18, 60 + i * 34, size.width() * (0.45 + 0.08 * (i % 4)), 14), 7, 7);
    }
    p.setBrush(accent.lighter(170));
    p.drawRoundedRect(QRectF(size.width() * 0.62, 70, size.width() * 0.3, size.height() * 0.45), 10, 10);
    return image;
}

void writeScreenshot(const QDateTime& createdAt, const QImage& image)
{
    SnapTray::CaptureSessionWriteRequest request;
    request.canvasImage = image;
    request.resultImage = image;
    request.selectionRect = QRect(QPoint(0, 0), image.size());
    request.annotationsJson = QByteArrayLiteral("{\"items\":[]}");
    request.canvasLogicalSize = image.size();
    request.createdAt = createdAt;
    request.windowTitle = QStringLiteral("Release checklist");
    request.ownerApp = QStringLiteral("Notes");
    QVERIFY(SnapTray::HistoryStore::writeCaptureSession(request).has_value());
}

QString writeProject(const QTemporaryDir& dir, const QString& name, const QImage& thumbnail, qint64 ms,
                     const QString& savedPath)
{
    QFile file(dir.filePath(name));
    if (!file.open(QIODevice::WriteOnly)) return {};
    file.write(QByteArray(4 * 1024 * 1024, 'v'));
    file.close();
    const auto project = SnapTray::RecordingProjects::create(dir.filePath(name), false, {}, thumbnail, ms, savedPath);
    return project ? project->dir : QString();
}

void setTheme(bool dark)
{
    ToolbarStyleConfig::saveStyle(dark ? ToolbarStyleType::Dark : ToolbarStyleType::Light);
    DesignSystem::instance().refreshTheme();
}

} // namespace

class tst_LibraryEvidence : public QObject
{
    Q_OBJECT

private slots:
    void screenshots_data();
    void screenshots();
};

void tst_LibraryEvidence::screenshots_data()
{
    QTest::addColumn<bool>("dark");
    QTest::newRow("light") << false;
    QTest::newRow("dark") << true;
}

void tst_LibraryEvidence::screenshots()
{
    const QString out = qEnvironmentVariable("SNAPTRAY_LIBRARY_DESIGN_DIR");
    if (out.isEmpty()) QSKIP("Set SNAPTRAY_LIBRARY_DESIGN_DIR to capture Library screenshots");
    QFETCH(bool, dark);
    QString suffix = dark ? QStringLiteral("dark") : QStringLiteral("light");
    QTranslator translator;
    const QString qm = qEnvironmentVariable("SNAPTRAY_LIBRARY_TRANSLATION");
    if (!qm.isEmpty()) {
        QVERIFY2(translator.load(qm), qPrintable(qm));
        QCoreApplication::installTranslator(&translator);
        suffix += QLatin1Char('-') + QFileInfo(qm).completeBaseName().section(QLatin1Char('_'), 1);
    }
    auto removeTranslator = qScopeGuard([&translator] { QCoreApplication::removeTranslator(&translator); });
    const auto savedStyle = ToolbarStyleConfig::loadStyle();
    auto restoreStyle = qScopeGuard([savedStyle] {
        ToolbarStyleConfig::saveStyle(savedStyle);
        DesignSystem::instance().refreshTheme();
    });
    setTheme(dark);

    QTemporaryDir dir;
    qputenv("SNAPTRAY_HISTORY_DIR", dir.filePath(QStringLiteral("history")).toLocal8Bit());
    SnapTray::RecordingProjects::setRootForTests(dir.filePath(QStringLiteral("projects")));
    auto resetRoots = qScopeGuard([] {
        SnapTray::RecordingProjects::setRootForTests(QString());
        qunsetenv("SNAPTRAY_HISTORY_DIR");
    });
    auto& settings = RecordingSettingsManager::instance();
    const int savedDays = settings.projectCleanupDays();
    const bool savedKeep = settings.keepProjects();
    auto restoreDays = qScopeGuard([&settings, savedDays, savedKeep] {
        settings.setProjectCleanupDays(savedDays);
        settings.setKeepProjects(savedKeep);
    });
    settings.setProjectCleanupDays(30);
    settings.setKeepProjects(true);

    const QDateTime now = QDateTime::currentDateTime();
    writeScreenshot(now.addDays(-4), demoImage(QSize(960, 600), QColor("#5b6cff"), QStringLiteral("Release checklist"), 8));
    writeScreenshot(now.addDays(-1), demoImage(QSize(1200, 700), QColor("#f08a24"), QStringLiteral("Sales dashboard"), 6));
    const QString saved = dir.filePath(QStringLiteral("Product demo.mp4"));
    QFile savedFile(saved);
    QVERIFY(savedFile.open(QIODevice::WriteOnly));
    savedFile.close();
    const QString demo = writeProject(dir, QStringLiteral("demo.mp4"),
                                      demoImage(QSize(640, 360), QColor("#1f9d74"), QStringLiteral("Onboarding flow"), 5),
                                      95000, saved);
    QTest::qWait(5);
    writeScreenshot(now.addSecs(-1800), demoImage(QSize(800, 520), QColor("#c2410c"), QStringLiteral("Bug report"), 7));
    QTest::qWait(5);
    writeProject(dir, QStringLiteral("draft.mp4"),
                 demoImage(QSize(640, 360), QColor("#7c3aed"), QStringLiteral("Feature walkthrough"), 4), 42000, QString());
    QVERIFY(!demo.isEmpty());

    QmlHistoryWindow window(nullptr, [](const QString&) { return false; });
    window.show();
    QQuickView* view = window.view();
    QVERIFY(QTest::qWaitForWindowExposed(view));
    QObject* root = view->rootObject();
    const auto shot = [&](const QString& name, QSize size) {
        view->resize(size);
        QTRY_COMPARE(view->size(), size);
        QTest::qWait(400); // thumbnails load asynchronously
        const QImage image = view->grabWindow();
        QVERIFY(!image.isNull());
        QVERIFY(image.save(QDir(out).filePath(name + QLatin1Char('-') + suffix + QStringLiteral(".png"))));
    };

    QMetaObject::invokeMethod(root, "selectKey", Q_ARG(QVariant, demo));
    shot(QStringLiteral("01-library-all"), QSize(1240, 820));

    QMetaObject::invokeMethod(root, "showSection", Q_ARG(QVariant, int(LibraryModel::Recordings)));
    QMetaObject::invokeMethod(root, "selectKey", Q_ARG(QVariant, demo));
    root->setProperty("listMode", true);
    shot(QStringLiteral("02-recordings-list"), QSize(1100, 720));

    root->setProperty("listMode", false);
    QMetaObject::invokeMethod(root, "askDelete");
    shot(QStringLiteral("03-delete-project"), QSize(1100, 720));
    QMetaObject::invokeMethod(root, "cancelDelete");

    QMetaObject::invokeMethod(root, "showSection", Q_ARG(QVariant, int(LibraryModel::Screenshots)));
    shot(QStringLiteral("04-narrow-screenshots"), QSize(660, 760));
}

QTEST_MAIN(tst_LibraryEvidence)
#include "tst_LibraryEvidence.moc"
