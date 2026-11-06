#include <QAccessible>
#include <QtTest/QtTest>

#include "history/HistoryStore.h"
#include "qml/LibraryModel.h"
#include "qml/QmlHistoryWindow.h"
#include "recording/RecordingProject.h"
#include "settings/RecordingSettingsManager.h"

#include <QFile>
#include <QQuickItem>
#include <QQuickView>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

using SnapTray::LibraryModel;
using SnapTray::QmlHistoryWindow;

namespace {

// Wide enough for the sidebar layout (narrow is below 820 px), small enough
// for the 1028x749 work area of the Windows CI runner.
constexpr QSize kWideTestSize(960, 700);

QString writeScreenshot(const QDateTime& createdAt)
{
    QImage image(QSize(64, 40), QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(40, 90, 160));
    SnapTray::CaptureSessionWriteRequest request;
    request.canvasImage = image;
    request.resultImage = image;
    request.selectionRect = QRect(QPoint(0, 0), image.size());
    request.annotationsJson = QByteArrayLiteral("{\"items\":[]}");
    request.canvasLogicalSize = image.size();
    request.createdAt = createdAt;
    const auto entry = SnapTray::HistoryStore::writeCaptureSession(request);
    return entry ? entry->entryDirectory : QString();
}

QString writeProject(const QTemporaryDir& dir, const QString& name, const QString& savedPath)
{
    QFile file(dir.filePath(name));
    if (!file.open(QIODevice::WriteOnly)) return {};
    file.write(QByteArray(2048, 'v'));
    file.close();
    QImage thumbnail(32, 18, QImage::Format_RGB32);
    thumbnail.fill(Qt::blue);
    const auto project = SnapTray::RecordingProjects::create(dir.filePath(name), false, {}, thumbnail, 65000, savedPath);
    return project ? project->dir : QString();
}

} // namespace

// #226: the Library window browses screenshots and editable recording
// projects in one place, with actions and delete consequences per kind.
class tst_LibraryWindow : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void sectionsAndCounts();
    void showSectionOpensRecordings();
    void screenshotActions();
    void recordingActions();
    void deleteScreenshotAsksFirst();
    void deleteProjectAsksFirst();
    void escapeCancelsConfirmation();
    void privacyAndRetentionNote();
    void reopeningRefreshesRetentionNote();
    void busyProjectCannotOpenOrDelete();
    void narrowLayoutUsesSectionRow();
    void narrowLayoutReachesSmartFolders();
    void reopeningClearsPendingDelete();
    void openingInPreviewCancelsPendingDelete();
    void cancelledDeleteReturnsFocus();
    void itemsAndSectionsReadTheirSelection();

private:
    QQuickItem* item(const char* name) const;
    QObject* root() const { return m_window->view()->rootObject(); }
    LibraryModel* library() const { return m_window->libraryModel(); }
    void select(const QString& key);
    void click(const char* name);

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<QmlHistoryWindow> m_window;
    QString m_shotDir;
    QString m_project;
    QString m_savedProject;
    int m_savedCleanupDays = 0;
    bool m_savedKeep = false;
};

void tst_LibraryWindow::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    qputenv("SNAPTRAY_HISTORY_DIR", m_dir->filePath(QStringLiteral("history")).toLocal8Bit());
    SnapTray::RecordingProjects::setRootForTests(m_dir->filePath(QStringLiteral("projects")));
    auto& settings = RecordingSettingsManager::instance();
    m_savedCleanupDays = settings.projectCleanupDays();
    m_savedKeep = settings.keepProjects();
    settings.setProjectCleanupDays(30);
    settings.setKeepProjects(true);
    m_shotDir = writeScreenshot(QDateTime::currentDateTime().addDays(-1));
    m_project = writeProject(*m_dir, QStringLiteral("a.mp4"), QString());
    const QString saved = m_dir->filePath(QStringLiteral("saved-b.mp4"));
    QFile savedFile(saved);
    QVERIFY(savedFile.open(QIODevice::WriteOnly));
    savedFile.close();
    QTest::qWait(5);
    m_savedProject = writeProject(*m_dir, QStringLiteral("b.mp4"), saved);
    QVERIFY(!m_shotDir.isEmpty() && !m_project.isEmpty() && !m_savedProject.isEmpty());

    m_window = std::make_unique<QmlHistoryWindow>(nullptr, [](const QString&) { return false; });
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window->view()));
    m_window->view()->resize(kWideTestSize);
    QTRY_COMPARE(m_window->view()->width(), kWideTestSize.width());
}

void tst_LibraryWindow::cleanup()
{
    m_window.reset();
    auto& settings = RecordingSettingsManager::instance();
    settings.setProjectCleanupDays(m_savedCleanupDays);
    settings.setKeepProjects(m_savedKeep);
    SnapTray::RecordingProjects::setRootForTests(QString());
    qunsetenv("SNAPTRAY_HISTORY_DIR");
    m_dir.reset();
}

QQuickItem* tst_LibraryWindow::item(const char* name) const
{
    // Repeater delegates are in the visual tree, not always QObject children.
    const QString wanted = QString::fromLatin1(name);
    std::function<QQuickItem*(QQuickItem*)> walk = [&](QQuickItem* node) -> QQuickItem* {
        if (node->objectName() == wanted) return node;
        for (QQuickItem* child : node->childItems())
            if (QQuickItem* found = walk(child)) return found;
        return nullptr;
    };
    QQuickItem* found = walk(qobject_cast<QQuickItem*>(root()));
    if (!found) qWarning() << "no item" << wanted;
    return found;
}

void tst_LibraryWindow::select(const QString& key)
{
    QMetaObject::invokeMethod(root(), "selectKey", Q_ARG(QVariant, key));
    QCOMPARE(root()->property("selectedKey").toString(), key);
}

void tst_LibraryWindow::click(const char* name)
{
    QQuickItem* button = item(name);
    QVERIFY2(button, name);
    QVERIFY2(button->isVisible(), name);
    QVERIFY2(button->property("enabledButton").toBool(), name);
    QMetaObject::invokeMethod(button, "clicked");
}

// #248: items were list items, which Qt exposes on macOS as static text
// without a selected state, and sections reported selection as a 0/1 value.
// Items read as one-of-many choices, sections as tabs, and the view toggle
// says which view is shown.
void tst_LibraryWindow::itemsAndSectionsReadTheirSelection()
{
    QAccessibleInterface* all = QAccessible::queryAccessibleInterface(item("librarySection_all"));
    QAccessibleInterface* recordings = QAccessible::queryAccessibleInterface(item("librarySection_recordings"));
    QVERIFY(all && recordings);
    QCOMPARE(all->role(), QAccessible::PageTab);
    QVERIFY(all->state().checked);
    QVERIFY(!recordings->state().checked);

    QList<QAccessibleInterface*> entries;
    QAccessibleInterface* gridView = nullptr;
    QAccessibleInterface* listView = nullptr;
    std::function<void(QQuickItem*)> walk = [&](QQuickItem* node) {
        if (!node->isVisible()) return;
        if (QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(node)) {
            const QString name = accessible->text(QAccessible::Name);
            if (name == QStringLiteral("Grid view")) gridView = accessible;
            else if (name == QStringLiteral("List view")) listView = accessible;
            else if (accessible->role() == QAccessible::RadioButton) entries << accessible;
        }
        for (QQuickItem* child : node->childItems()) walk(child);
    };
    walk(qobject_cast<QQuickItem*>(root()));
    QCOMPARE(entries.size(), library()->rowCount());
    QVERIFY(gridView && listView);
    QVERIFY(gridView->state().checked);
    QVERIFY(!listView->state().checked);

    select(library()->keyAt(0));
    int checked = 0;
    QAccessibleInterface* unselected = nullptr;
    for (QAccessibleInterface* entry : entries) {
        QVERIFY(entry->state().checkable);
        if (entry->state().checked) ++checked;
        else unselected = entry;
    }
    QCOMPARE(checked, 1);
    QVERIFY(unselected);
    const QString before = root()->property("selectedKey").toString();
    unselected->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
    QTRY_VERIFY(root()->property("selectedKey").toString() != before);
    QTRY_VERIFY(unselected->state().checked);
}

void tst_LibraryWindow::sectionsAndCounts()
{
    QCOMPARE(m_window->view()->title(), QStringLiteral("Library"));
    QCOMPARE(library()->source(), int(LibraryModel::AllCaptures));
    QCOMPARE(library()->rowCount(), 3);
    QCOMPARE(item("librarySection_all")->property("countLabel").toString(), QStringLiteral("3"));
    QCOMPARE(item("librarySection_screenshots")->property("countLabel").toString(), QStringLiteral("1"));
    QCOMPARE(item("librarySection_recordings")->property("countLabel").toString(), QStringLiteral("2"));

    QMetaObject::invokeMethod(item("librarySection_recordings"), "clicked");
    QCOMPARE(library()->source(), int(LibraryModel::Recordings));
    QCOMPARE(library()->rowCount(), 2);
    QCOMPARE(item("libraryTitle")->property("text").toString(), QStringLiteral("Recordings"));
    QVERIFY(item("librarySection_recordings")->property("active").toBool());

    // A smart folder shows screenshots.
    QMetaObject::invokeMethod(item("librarySmartFolder_2"), "clicked");
    QCOMPARE(library()->source(), int(LibraryModel::Screenshots));
    QCOMPARE(item("libraryTitle")->property("text").toString(), QStringLiteral("Large Files"));
}

void tst_LibraryWindow::showSectionOpensRecordings()
{
    m_window->showSection(LibraryModel::Recordings);
    QCOMPARE(library()->source(), int(LibraryModel::Recordings));
    QCOMPARE(item("libraryTitle")->property("text").toString(), QStringLiteral("Recordings"));
}

void tst_LibraryWindow::screenshotActions()
{
    int shotRow = -1;
    for (int i = 0; i < library()->rowCount(); ++i)
        if (library()->kindAt(i) == LibraryModel::Screenshot) shotRow = i;
    QVERIFY(shotRow >= 0);
    select(library()->keyAt(shotRow));
    QCOMPARE(library()->kindAt(root()->property("selectedIndex").toInt()), int(LibraryModel::Screenshot));
    QCOMPARE(item("librarySelectionLabel")->property("text").toString(), QStringLiteral("Screenshot selected"));
    for (const char* name : {"libraryAction_edit", "libraryAction_pin", "libraryAction_copy", "libraryAction_save",
                             "libraryAction_delete"})
        QVERIFY2(item(name)->isVisible(), name);
    for (const char* name : {"libraryAction_open", "libraryAction_showSaved", "libraryAction_showFolder",
                             "libraryAction_deleteProject"})
        QVERIFY2(!item(name)->isVisible(), name);
}

void tst_LibraryWindow::recordingActions()
{
    select(m_project);
    QCOMPARE(item("librarySelectionLabel")->property("text").toString(), QStringLiteral("Editable project selected"));
    for (const char* name : {"libraryAction_open", "libraryAction_showSaved", "libraryAction_showFolder",
                             "libraryAction_deleteProject"})
        QVERIFY2(item(name)->isVisible(), name);
    QVERIFY(!item("libraryAction_edit")->isVisible());
    // Never saved: nothing to show.
    QVERIFY(!item("libraryAction_showSaved")->property("enabledButton").toBool());
    select(m_savedProject);
    QVERIFY(item("libraryAction_showSaved")->property("enabledButton").toBool());

    QSignalSpy open(m_window.get(), &QmlHistoryWindow::openProjectRequested);
    click("libraryAction_open");
    QCOMPARE(open.count(), 1);
    QCOMPARE(open.first().first().toString(), m_savedProject);
}

void tst_LibraryWindow::deleteScreenshotAsksFirst()
{
    library()->setSource(LibraryModel::Screenshots);
    select(library()->keyAt(0));
    click("libraryAction_delete");
    QVERIFY(item("libraryConfirmStrip")->isVisible());
    QVERIFY(item("libraryConfirmText")->property("text").toString().contains(QStringLiteral("Files you saved from it stay")));
    QVERIFY(QFileInfo::exists(m_shotDir));
    click("libraryConfirmCancel");
    QVERIFY(!item("libraryConfirmStrip")->isVisible());
    QVERIFY(QFileInfo::exists(m_shotDir));

    click("libraryAction_delete");
    click("libraryConfirmDelete");
    QTRY_VERIFY(!QFileInfo::exists(m_shotDir));
    QCOMPARE(library()->screenshotCount(), 0);
    QVERIFY(!item("libraryConfirmStrip")->isVisible());
}

void tst_LibraryWindow::deleteProjectAsksFirst()
{
    select(m_project);
    click("libraryAction_deleteProject");
    QVERIFY(item("libraryConfirmText")->property("text").toString().contains(QStringLiteral("full recording")));
    QVERIFY(QFileInfo::exists(m_project));
    // A change of selection cancels.
    select(m_savedProject);
    QVERIFY(!item("libraryConfirmStrip")->isVisible());
    select(m_project);
    click("libraryAction_deleteProject");
    click("libraryConfirmDelete");
    QVERIFY(!QFileInfo::exists(m_project));
    QVERIFY(QFileInfo::exists(m_savedProject));
    QCOMPARE(library()->recordingCount(), 1);
}

void tst_LibraryWindow::escapeCancelsConfirmation()
{
    select(m_project);
    click("libraryAction_deleteProject");
    QVERIFY(item("libraryConfirmStrip")->isVisible());
    QTest::keyClick(m_window->view(), Qt::Key_Escape);
    QVERIFY(!item("libraryConfirmStrip")->isVisible());
    QVERIFY(m_window->isVisible());
    QVERIFY(QFileInfo::exists(m_project));
    // Without a confirmation, Esc closes the window as before.
    QTest::keyClick(m_window->view(), Qt::Key_Escape);
    QTRY_VERIFY(!m_window->isVisible());
}

void tst_LibraryWindow::privacyAndRetentionNote()
{
    library()->setSource(LibraryModel::Screenshots);
    QVERIFY(!item("libraryProjectNote")->isVisible());
    library()->setSource(LibraryModel::Recordings);
    QVERIFY(item("libraryProjectNote")->isVisible());
    const QString note = item("libraryProjectNoteText")->property("text").toString();
    QVERIFY2(note.contains(QStringLiteral("including anything you blurred or cropped out")), qPrintable(note));
    QVERIFY2(note.contains(QStringLiteral("30 days")), qPrintable(note));
}

// Review: the window outlives a close, so its note must follow Settings
// changes made while it was closed.
void tst_LibraryWindow::reopeningRefreshesRetentionNote()
{
    library()->setSource(LibraryModel::Recordings);
    QVERIFY(item("libraryProjectNoteText")->property("text").toString().contains(QStringLiteral("30 days")));
    m_window->view()->close();
    QTRY_VERIFY(!m_window->isVisible());
    RecordingSettingsManager::instance().setProjectCleanupDays(14);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window->view()));
    const QString note = item("libraryProjectNoteText")->property("text").toString();
    QVERIFY2(note.contains(QStringLiteral("14 days")), qPrintable(note));
}

void tst_LibraryWindow::busyProjectCannotOpenOrDelete()
{
    m_window->setOpenProjectDir(m_project);
    select(m_project);
    QVERIFY(!item("libraryAction_open")->property("enabledButton").toBool());
    QVERIFY(!item("libraryAction_deleteProject")->property("enabledButton").toBool());
    QVERIFY(item("libraryAction_showFolder")->property("enabledButton").toBool());
    m_window->setOpenProjectDir(QString());
    select(m_project);
    QVERIFY(item("libraryAction_open")->property("enabledButton").toBool());
}

void tst_LibraryWindow::narrowLayoutUsesSectionRow()
{
    QVERIFY(item("librarySidebar")->isVisible());
    QVERIFY(!item("librarySectionRow")->isVisible());
    QVERIFY(m_window->view()->minimumWidth() <= 640);
    m_window->view()->resize(700, 700);
    QTRY_COMPARE(m_window->view()->width(), 700);
    QTRY_VERIFY(!item("librarySidebar")->isVisible());
    QVERIFY(item("librarySectionRow")->isVisible());
    QMetaObject::invokeMethod(item("libraryNarrowSection_recordings"), "clicked");
    QCOMPARE(library()->source(), int(LibraryModel::Recordings));
}

// Review: the narrow layout keeps the smart folders (spec: a Filter menu).
void tst_LibraryWindow::narrowLayoutReachesSmartFolders()
{
    m_window->view()->resize(700, 700);
    QTRY_COMPARE(m_window->view()->width(), 700);
    QTRY_VERIFY(!item("librarySidebar")->isVisible());
    QVERIFY(item("libraryNarrowFilter"));
    QVERIFY(!item("libraryNarrowFilter")->isVisible());
    QMetaObject::invokeMethod(item("libraryNarrowSection_screenshots"), "clicked");
    QVERIFY(item("libraryNarrowFilter")->isVisible());
    QMetaObject::invokeMethod(item("libraryNarrowFilter"), "activated", Q_ARG(int, 2));
    QCOMPARE(item("libraryTitle")->property("text").toString(), QStringLiteral("Large Files"));
    QCOMPARE(library()->source(), int(LibraryModel::Screenshots));
}

// Review: a delete left pending when the window closed does not come back.
void tst_LibraryWindow::reopeningClearsPendingDelete()
{
    select(m_project);
    click("libraryAction_deleteProject");
    QVERIFY(item("libraryConfirmStrip")->isVisible());
    m_window->view()->close();
    QTRY_VERIFY(!m_window->isVisible());
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window->view()));
    QVERIFY(!item("libraryConfirmStrip")->isVisible());
    QVERIFY(QFileInfo::exists(m_project));
}

// Review: a project opened in the preview while its delete is pending cannot
// be deleted, so the question goes away instead of doing nothing.
void tst_LibraryWindow::openingInPreviewCancelsPendingDelete()
{
    select(m_project);
    click("libraryAction_deleteProject");
    QVERIFY(item("libraryConfirmStrip")->isVisible());
    m_window->setOpenProjectDir(m_project);
    QTRY_VERIFY(!item("libraryConfirmStrip")->isVisible());
    QVERIFY(QFileInfo::exists(m_project));
}

// Review: a selection or section change that drops the question also takes
// keyboard focus back from the hidden Delete button, so Return opens the new
// selection instead of confirming nothing.
void tst_LibraryWindow::cancelledDeleteReturnsFocus()
{
    auto* rootItem = qobject_cast<QQuickItem*>(root());
    QVERIFY(rootItem);

    select(m_project);
    click("libraryAction_deleteProject");
    QTRY_VERIFY(item("libraryConfirmDelete")->hasActiveFocus());
    select(m_savedProject);
    QVERIFY(!item("libraryConfirmStrip")->isVisible());
    QVERIFY(!item("libraryConfirmDelete")->hasActiveFocus());
    QVERIFY(rootItem->hasActiveFocus());
    QSignalSpy open(m_window.get(), &QmlHistoryWindow::openProjectRequested);
    QTest::keyClick(m_window->view(), Qt::Key_Return);
    QCOMPARE(open.count(), 1);
    QCOMPARE(open.first().first().toString(), m_savedProject);

    // Changing sections does the same.
    select(m_project);
    click("libraryAction_deleteProject");
    QTRY_VERIFY(item("libraryConfirmDelete")->hasActiveFocus());
    QMetaObject::invokeMethod(item("librarySection_screenshots"), "clicked");
    QVERIFY(!item("libraryConfirmStrip")->isVisible());
    QVERIFY(rootItem->hasActiveFocus());
    QVERIFY(QFileInfo::exists(m_project));
}

QTEST_MAIN(tst_LibraryWindow)
#include "tst_LibraryWindow.moc"
