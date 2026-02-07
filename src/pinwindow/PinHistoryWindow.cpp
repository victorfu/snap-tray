#include "pinwindow/PinHistoryWindow.h"

#include "PinWindow.h"
#include "PinWindowManager.h"
#include "pinwindow/RegionLayoutManager.h"
#include "settings/FileSettingsManager.h"

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QSize>
#include <QUrl>
#include <QVBoxLayout>

namespace {
constexpr int kThumbnailWidth = 180;
constexpr int kThumbnailHeight = 120;
constexpr int kGridWidth = 210;
constexpr int kGridHeight = 165;
constexpr int kListSpacing = 10;
constexpr int kWindowWidth = 760;
constexpr int kWindowHeight = 520;
constexpr int kRoleImagePath = Qt::UserRole + 1;
constexpr int kRoleMetadataPath = Qt::UserRole + 2;
constexpr int kRoleDprMetadataPath = Qt::UserRole + 3;
constexpr int kRoleCapturedAt = Qt::UserRole + 4;
constexpr int kRoleImageSize = Qt::UserRole + 5;
constexpr int kRoleDevicePixelRatio = Qt::UserRole + 6;
}

PinHistoryWindow::PinHistoryWindow(PinWindowManager* pinWindowManager, QWidget* parent)
    : QWidget(parent)
    , m_pinWindowManager(pinWindowManager)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlag(Qt::Window, true);
    setWindowTitle(tr("Pin History"));
    resize(kWindowWidth, kWindowHeight);
    setMinimumSize(480, 320);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);

    auto* headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    m_openCacheFolderButton = new QPushButton(tr("Open Cache Folder"), this);
    m_openCacheFolderButton->setObjectName("openCacheFolderButton");
    headerLayout->addWidget(m_openCacheFolderButton);
    headerLayout->addStretch();
    mainLayout->addLayout(headerLayout);

    auto* contentLayout = new QHBoxLayout();
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    m_listWidget = new QListWidget(this);
    m_listWidget->setObjectName("pinHistoryListWidget");
    m_listWidget->setViewMode(QListView::IconMode);
    m_listWidget->setFlow(QListView::LeftToRight);
    m_listWidget->setWrapping(true);
    m_listWidget->setResizeMode(QListView::Adjust);
    m_listWidget->setMovement(QListView::Static);
    m_listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_listWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    m_listWidget->setIconSize(QSize(kThumbnailWidth, kThumbnailHeight));
    m_listWidget->setGridSize(QSize(kGridWidth, kGridHeight));
    m_listWidget->setSpacing(kListSpacing);
    m_listWidget->setWordWrap(true);
    m_listWidget->setMouseTracking(true);
    contentLayout->addWidget(m_listWidget, 1);

    m_emptyLabel = new QLabel(tr("No pinned screenshots yet"), this);
    m_emptyLabel->setObjectName("pinHistoryEmptyLabel");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    contentLayout->addWidget(m_emptyLabel, 1);
    mainLayout->addLayout(contentLayout, 1);

    connect(m_listWidget, &QListWidget::itemClicked,
            this, &PinHistoryWindow::onItemClicked);
    connect(m_listWidget, &QListWidget::customContextMenuRequested,
            this, &PinHistoryWindow::onCustomContextMenuRequested);
    connect(m_openCacheFolderButton, &QPushButton::clicked,
            this, &PinHistoryWindow::openCacheFolder);

    refreshEntries();
}

void PinHistoryWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void PinHistoryWindow::refreshEntries()
{
    const QList<PinHistoryEntry> entries = PinHistoryStore::loadEntries();
    m_listWidget->clear();

    for (const PinHistoryEntry& entry : entries) {
        addEntryItem(entry);
    }

    updateEmptyState();
}

void PinHistoryWindow::onItemClicked(QListWidgetItem* item)
{
    if (!item) {
        return;
    }

    pinEntry(entryFromItem(item));
}

void PinHistoryWindow::onCustomContextMenuRequested(const QPoint& pos)
{
    QListWidgetItem* item = m_listWidget->itemAt(pos);
    if (!item) {
        return;
    }

    m_listWidget->setCurrentItem(item);
    const PinHistoryEntry entry = entryFromItem(item);

    QMenu menu(this);
    QAction* pinAction = menu.addAction(tr("Pin"));
    QAction* copyAction = menu.addAction(tr("Copy"));
    QAction* saveAsAction = menu.addAction(tr("Save As"));
    menu.addSeparator();
    QAction* deleteAction = menu.addAction(tr("Delete"));

    QAction* selected = menu.exec(m_listWidget->viewport()->mapToGlobal(pos));
    if (selected == pinAction) {
        pinEntry(entry);
        return;
    }
    if (selected == copyAction) {
        copyEntry(entry);
        return;
    }
    if (selected == saveAsAction) {
        saveEntryAs(entry);
        return;
    }
    if (selected == deleteAction) {
        deleteSelectedEntry();
    }
}

void PinHistoryWindow::deleteSelectedEntry()
{
    QListWidgetItem* item = m_listWidget->currentItem();
    if (!item) {
        return;
    }

    const PinHistoryEntry entry = entryFromItem(item);
    const bool success = PinHistoryStore::deleteEntry(entry);
    if (!success) {
        QMessageBox::warning(this, tr("Delete Failed"), tr("Failed to delete cached screenshot."));
    }

    refreshEntries();
}

void PinHistoryWindow::openCacheFolder()
{
    const QString path = PinHistoryStore::historyFolderPath();
    QDir dir(path);
    if (!dir.exists() && !dir.mkpath(".")) {
        qWarning() << "PinHistoryWindow: Failed to create cache folder:" << path;
        return;
    }

    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void PinHistoryWindow::pinEntry(const PinHistoryEntry& entry)
{
    if (!m_pinWindowManager) {
        close();
        return;
    }

    QPixmap pixmap;
    if (!loadPixmap(entry, &pixmap)) {
        QMessageBox::warning(this, tr("Pin Failed"), tr("Failed to load cached screenshot."));
        close();
        return;
    }

    PinWindow* pinWindow = m_pinWindowManager->createPinWindow(pixmap, centeredPositionFor(pixmap));
    if (pinWindow && !entry.metadataPath.isEmpty() && QFile::exists(entry.metadataPath)) {
        QFile metadataFile(entry.metadataPath);
        if (metadataFile.open(QIODevice::ReadOnly)) {
            const QByteArray data = metadataFile.readAll();
            const QVector<LayoutRegion> regions = RegionLayoutManager::deserializeRegions(data);
            if (!regions.isEmpty()) {
                pinWindow->setMultiRegionData(regions);
            }
        }
    }

    close();
}

void PinHistoryWindow::copyEntry(const PinHistoryEntry& entry)
{
    QPixmap pixmap;
    if (!loadPixmap(entry, &pixmap)) {
        QMessageBox::warning(this, tr("Copy Failed"), tr("Failed to load cached screenshot."));
        close();
        return;
    }

    QGuiApplication::clipboard()->setPixmap(pixmap);
    close();
}

void PinHistoryWindow::saveEntryAs(const PinHistoryEntry& entry)
{
    QPixmap pixmap;
    if (!loadPixmap(entry, &pixmap)) {
        QMessageBox::warning(this, tr("Save Failed"), tr("Failed to load cached screenshot."));
        close();
        return;
    }

    auto& fileSettings = FileSettingsManager::instance();
    const QString defaultDir = fileSettings.loadScreenshotPath();
    const QString baseName = entry.capturedAt.isValid()
        ? QString("Pinned_%1.png").arg(entry.capturedAt.toString("yyyyMMdd_HHmmss_zzz"))
        : QString("Pinned_%1.png").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz"));
    const QString defaultPath = defaultDir.isEmpty() ? baseName : QString("%1/%2").arg(defaultDir, baseName);

    const QString filePath = QFileDialog::getSaveFileName(
        this,
        tr("Save Screenshot"),
        defaultPath,
        tr("PNG Image (*.png);;JPEG Image (*.jpg *.jpeg);;All Files (*)"));

    if (!filePath.isEmpty() && !pixmap.save(filePath)) {
        QMessageBox::warning(this, tr("Save Failed"), tr("Failed to save screenshot."));
    }

    close();
}

PinHistoryEntry PinHistoryWindow::entryFromItem(const QListWidgetItem* item) const
{
    PinHistoryEntry entry;
    if (!item) {
        return entry;
    }

    entry.imagePath = item->data(kRoleImagePath).toString();
    entry.metadataPath = item->data(kRoleMetadataPath).toString();
    entry.dprMetadataPath = item->data(kRoleDprMetadataPath).toString();
    entry.capturedAt = item->data(kRoleCapturedAt).toDateTime();
    entry.imageSize = item->data(kRoleImageSize).toSize();
    entry.devicePixelRatio = item->data(kRoleDevicePixelRatio).toDouble();
    if (entry.devicePixelRatio <= 0.0) {
        entry.devicePixelRatio = 1.0;
    }
    return entry;
}

bool PinHistoryWindow::loadPixmap(const PinHistoryEntry& entry, QPixmap* pixmapOut) const
{
    if (!pixmapOut || entry.imagePath.isEmpty()) {
        return false;
    }

    const QPixmap pixmap(entry.imagePath);
    if (pixmap.isNull()) {
        return false;
    }

    QPixmap restored = pixmap;
    if (entry.devicePixelRatio > 0.0) {
        restored.setDevicePixelRatio(entry.devicePixelRatio);
    }

    *pixmapOut = restored;
    return true;
}

QPoint PinHistoryWindow::centeredPositionFor(const QPixmap& pixmap) const
{
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        return QPoint(50, 50);
    }

    const QRect screenGeometry = screen->availableGeometry();
    qreal dpr = pixmap.devicePixelRatio();
    if (dpr <= 0.0) {
        dpr = 1.0;
    }
    const QSize logicalSize = pixmap.size() / dpr;
    return screenGeometry.center() - QPoint(logicalSize.width() / 2, logicalSize.height() / 2);
}

QString PinHistoryWindow::tooltipTextFor(const PinHistoryEntry& entry) const
{
    const QString timeText = entry.capturedAt.isValid()
        ? entry.capturedAt.toString("yyyy-MM-dd HH:mm:ss")
        : tr("Unknown");
    const QString sizeText = entry.imageSize.isValid()
        ? QString("%1 x %2").arg(entry.imageSize.width()).arg(entry.imageSize.height())
        : tr("Unknown");

    return tr("Time: %1\nSize: %2").arg(timeText, sizeText);
}

void PinHistoryWindow::addEntryItem(const PinHistoryEntry& entry)
{
    auto* item = new QListWidgetItem(m_listWidget);
    item->setData(kRoleImagePath, entry.imagePath);
    item->setData(kRoleMetadataPath, entry.metadataPath);
    item->setData(kRoleDprMetadataPath, entry.dprMetadataPath);
    item->setData(kRoleCapturedAt, entry.capturedAt);
    item->setData(kRoleImageSize, entry.imageSize);
    item->setData(kRoleDevicePixelRatio, entry.devicePixelRatio);

    const QPixmap pixmap(entry.imagePath);
    if (!pixmap.isNull()) {
        const QPixmap thumbnail = pixmap.scaled(
            QSize(kThumbnailWidth, kThumbnailHeight),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation);
        item->setIcon(QIcon(thumbnail));
    }

    item->setText(entry.capturedAt.isValid()
        ? entry.capturedAt.toString("yyyy-MM-dd HH:mm:ss")
        : tr("Unknown time"));
    item->setTextAlignment(Qt::AlignCenter);
    item->setToolTip(tooltipTextFor(entry));
}

void PinHistoryWindow::updateEmptyState()
{
    const bool hasEntries = (m_listWidget->count() > 0);
    m_listWidget->setVisible(hasEntries);
    m_emptyLabel->setVisible(!hasEntries);
}
