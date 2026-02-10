#ifndef PINHISTORYWINDOW_H
#define PINHISTORYWINDOW_H

#include "pinwindow/PinHistoryStore.h"

#include <QKeyEvent>
#include <QPixmap>
#include <QPoint>
#include <QWidget>

class PinWindowManager;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

class PinHistoryWindow : public QWidget
{
    Q_OBJECT

public:
    explicit PinHistoryWindow(PinWindowManager* pinWindowManager, QWidget* parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void refreshEntries();
    void onItemClicked(QListWidgetItem* item);
    void onCustomContextMenuRequested(const QPoint& pos);
    void deleteSelectedEntry();
    void openCacheFolder();

private:
    void pinEntry(const PinHistoryEntry& entry);
    void copyEntry(const PinHistoryEntry& entry);
    void saveEntryAs(const PinHistoryEntry& entry);
    PinHistoryEntry entryFromItem(const QListWidgetItem* item) const;
    bool loadPixmap(const PinHistoryEntry& entry, QPixmap* pixmapOut) const;
    QString tooltipTextFor(const PinHistoryEntry& entry) const;
    void addEntryItem(const PinHistoryEntry& entry);
    void updateEmptyState();

    PinWindowManager* m_pinWindowManager = nullptr;
    QListWidget* m_listWidget = nullptr;
    QLabel* m_emptyLabel = nullptr;
    QPushButton* m_openCacheFolderButton = nullptr;
};

#endif // PINHISTORYWINDOW_H
