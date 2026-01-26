/**
 * @file HotkeySettingsTab.h
 * @brief Settings tab widget for managing hotkeys
 *
 * Features:
 * - QTreeWidget with category grouping
 * - Status indicator (green/red/gray)
 * - Double-click opens TypeHotkeyDialog
 * - Clear, Reset, and Restore All buttons
 */

#pragma once

#include "hotkey/HotkeyTypes.h"
#include <QWidget>
#include <QMap>

class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;
class QLabel;

namespace SnapTray {

/**
 * @brief Settings tab widget for managing hotkeys.
 *
 * Displays all hotkeys in a categorized tree view with status indicators
 * and editing capabilities.
 */
class HotkeySettingsTab : public QWidget
{
    Q_OBJECT

public:
    explicit HotkeySettingsTab(QWidget* parent = nullptr);
    ~HotkeySettingsTab() override;

    /**
     * @brief Reload all hotkey configurations from HotkeyManager.
     */
    void refreshFromManager();

    /**
     * @brief Check if there are unsaved changes.
     */
    bool hasUnsavedChanges() const { return m_hasUnsavedChanges; }

signals:
    /**
     * @brief Emitted when settings change (for dirty tracking).
     */
    void settingsChanged();

private slots:
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onEditClicked();
    void onClearClicked();
    void onResetClicked();
    void onRestoreAllDefaults();
    void onHotkeyManagerChanged(HotkeyAction action, const HotkeyConfig& config);
    void onRegistrationStatusChanged(HotkeyAction action, HotkeyStatus status);
    void onSelectionChanged();

private:
    void setupUi();
    void setupConnections();
    void populateTree();

    QTreeWidgetItem* findOrCreateCategoryItem(HotkeyCategory category);
    QTreeWidgetItem* createHotkeyItem(const HotkeyConfig& config);
    void updateItemDisplay(QTreeWidgetItem* item, const HotkeyConfig& config);
    void updateStatusIndicator(QTreeWidgetItem* item, HotkeyStatus status);

    QTreeWidgetItem* findItemByAction(HotkeyAction action) const;
    HotkeyAction getActionFromItem(QTreeWidgetItem* item) const;
    bool isHotkeyItem(QTreeWidgetItem* item) const;

    void showEditDialog(HotkeyAction action);

    // UI elements
    QTreeWidget* m_treeWidget;
    QPushButton* m_editBtn;
    QPushButton* m_clearBtn;
    QPushButton* m_resetBtn;
    QPushButton* m_restoreAllBtn;
    QLabel* m_statusLabel;

    // State tracking
    QMap<HotkeyCategory, QTreeWidgetItem*> m_categoryItems;
    QMap<QTreeWidgetItem*, HotkeyAction> m_itemToAction;
    bool m_hasUnsavedChanges;

    // Column indices
    static constexpr int kColumnName = 0;
    static constexpr int kColumnHotkey = 1;
    static constexpr int kColumnStatus = 2;

    // Item data roles
    static constexpr int kActionRole = Qt::UserRole + 1;
    static constexpr int kIsCategoryRole = Qt::UserRole + 2;
};

}  // namespace SnapTray
