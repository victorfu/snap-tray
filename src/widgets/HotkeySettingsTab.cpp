/**
 * @file HotkeySettingsTab.cpp
 * @brief HotkeySettingsTab implementation
 */

#include "widgets/HotkeySettingsTab.h"
#include "widgets/TypeHotkeyDialog.h"
#include "hotkey/HotkeyManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QPushButton>
#include <QLabel>
#include <QHeaderView>
#include <QMessageBox>

namespace SnapTray {

HotkeySettingsTab::HotkeySettingsTab(QWidget* parent)
    : QWidget(parent)
    , m_treeWidget(nullptr)
    , m_editBtn(nullptr)
    , m_clearBtn(nullptr)
    , m_resetBtn(nullptr)
    , m_restoreAllBtn(nullptr)
    , m_statusLabel(nullptr)
    , m_hasUnsavedChanges(false)
    , m_updatingFromManager(false)
{
    setupUi();
    setupConnections();
    populateTree();
}

HotkeySettingsTab::~HotkeySettingsTab() = default;

void HotkeySettingsTab::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(12);

    // Tree widget
    m_treeWidget = new QTreeWidget(this);
    m_treeWidget->setHeaderLabels({
        QString(),       // Enabled checkbox
        tr("Action"),
        tr("Hotkey"),
        tr("Status")
    });

    m_treeWidget->setColumnWidth(kColumnEnabled, 32);
    m_treeWidget->setColumnWidth(kColumnName, 180);
    m_treeWidget->setColumnWidth(kColumnHotkey, 140);
    m_treeWidget->setColumnWidth(kColumnStatus, 90);

    m_treeWidget->setAlternatingRowColors(true);
    m_treeWidget->setRootIsDecorated(true);
    m_treeWidget->setIndentation(20);
    m_treeWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_treeWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_treeWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_treeWidget->header()->setStretchLastSection(true);

    m_treeWidget->setStyleSheet(QStringLiteral(
        "QTreeWidget {"
        "  border: 1px solid #E0E0E0;"
        "  border-radius: 4px;"
        "}"
        "QTreeWidget::item {"
        "  padding: 4px 6px;"
        "  min-height: 28px;"
        "}"
        "QTreeWidget::item:selected {"
        "  background-color: #E3F2FD;"
        "  color: #1976D2;"
        "}"
        "QTreeWidget::item:hover {"
        "  background-color: #F5F5F5;"
        "}"
        "QHeaderView::section {"
        "  background-color: #FAFAFA;"
        "  padding: 6px;"
        "  border: none;"
        "  border-bottom: 1px solid #E0E0E0;"
        "  font-weight: 500;"
        "}"));

    mainLayout->addWidget(m_treeWidget, 1);

    // Hint label
    auto* hintLabel = new QLabel(
        tr("Double-click a hotkey to edit, or select and press Enter"), this);
    hintLabel->setStyleSheet(QStringLiteral("color: #757575; font-size: 11px;"));
    mainLayout->addWidget(hintLabel);

    // Button row
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(8);

    m_editBtn = new QPushButton(tr("Edit..."), this);
    m_editBtn->setEnabled(false);
    buttonLayout->addWidget(m_editBtn);

    m_clearBtn = new QPushButton(tr("Clear"), this);
    m_clearBtn->setEnabled(false);
    buttonLayout->addWidget(m_clearBtn);

    m_resetBtn = new QPushButton(tr("Reset to Default"), this);
    m_resetBtn->setEnabled(false);
    buttonLayout->addWidget(m_resetBtn);

    buttonLayout->addStretch();

    m_restoreAllBtn = new QPushButton(tr("Restore All Defaults"), this);
    buttonLayout->addWidget(m_restoreAllBtn);

    mainLayout->addLayout(buttonLayout);

    // Status label
    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet(QStringLiteral("color: #757575; font-size: 11px;"));
    mainLayout->addWidget(m_statusLabel);
}

void HotkeySettingsTab::setupConnections()
{
    connect(m_treeWidget, &QTreeWidget::itemDoubleClicked,
            this, &HotkeySettingsTab::onItemDoubleClicked);

    connect(m_treeWidget, &QTreeWidget::itemChanged,
            this, &HotkeySettingsTab::onItemChanged);

    connect(m_treeWidget, &QTreeWidget::itemSelectionChanged,
            this, &HotkeySettingsTab::onSelectionChanged);

    connect(m_editBtn, &QPushButton::clicked,
            this, &HotkeySettingsTab::onEditClicked);

    connect(m_clearBtn, &QPushButton::clicked,
            this, &HotkeySettingsTab::onClearClicked);

    connect(m_resetBtn, &QPushButton::clicked,
            this, &HotkeySettingsTab::onResetClicked);

    connect(m_restoreAllBtn, &QPushButton::clicked,
            this, &HotkeySettingsTab::onRestoreAllDefaults);

    // Connect to HotkeyManager signals
    connect(&HotkeyManager::instance(), &HotkeyManager::hotkeyChanged,
            this, &HotkeySettingsTab::onHotkeyManagerChanged);

    connect(&HotkeyManager::instance(), &HotkeyManager::registrationStatusChanged,
            this, &HotkeySettingsTab::onRegistrationStatusChanged);
}

void HotkeySettingsTab::populateTree()
{
    m_treeWidget->clear();
    m_categoryItems.clear();
    m_itemToAction.clear();

    // Get all configs from manager
    QList<HotkeyConfig> configs = HotkeyManager::instance().getAllConfigs();

    // Group by category
    for (const HotkeyConfig& config : configs) {
        QTreeWidgetItem* categoryItem = findOrCreateCategoryItem(config.category);
        QTreeWidgetItem* hotkeyItem = createHotkeyItem(config);
        categoryItem->addChild(hotkeyItem);
        m_itemToAction[hotkeyItem] = config.action;
    }

    // Expand all categories
    m_treeWidget->expandAll();
}

QTreeWidgetItem* HotkeySettingsTab::findOrCreateCategoryItem(HotkeyCategory category)
{
    auto it = m_categoryItems.find(category);
    if (it != m_categoryItems.end()) {
        return it.value();
    }

    auto* item = new QTreeWidgetItem(m_treeWidget);
    item->setData(0, kIsCategoryRole, true);

    // Category display with icon
    QString icon = getCategoryIcon(category);
    QString name = getCategoryDisplayName(category);
    item->setText(kColumnName, icon + QStringLiteral(" ") + name);

    // Style the category header
    QFont font = item->font(kColumnName);
    font.setBold(true);
    item->setFont(kColumnName, font);

    // Disable selection for category items
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);

    m_categoryItems[category] = item;
    return item;
}

QTreeWidgetItem* HotkeySettingsTab::createHotkeyItem(const HotkeyConfig& config)
{
    auto* item = new QTreeWidgetItem();
    item->setData(0, kActionRole, static_cast<int>(config.action));
    item->setData(0, kIsCategoryRole, false);

    // Enable checkbox
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(kColumnEnabled, config.enabled ? Qt::Checked : Qt::Unchecked);

    updateItemDisplay(item, config);
    return item;
}

void HotkeySettingsTab::updateItemDisplay(QTreeWidgetItem* item, const HotkeyConfig& config)
{
    // Name
    item->setText(kColumnName, config.displayName);
    item->setToolTip(kColumnName, config.description);

    // Hotkey
    QString hotkeyText = config.keySequence.isEmpty()
        ? (config.isOptional() ? tr("(Optional)") : tr("(Not set)"))
        : HotkeyManager::formatKeySequence(config.keySequence);
    item->setText(kColumnHotkey, hotkeyText);

    // Style based on state
    if (config.keySequence.isEmpty()) {
        item->setForeground(kColumnHotkey, QColor(0x9E, 0x9E, 0x9E));  // Gray
    } else {
        item->setForeground(kColumnHotkey, QColor(0x21, 0x21, 0x21));  // Dark
    }

    // Status
    updateStatusIndicator(item, config.status);

    // Enabled state
    item->setCheckState(kColumnEnabled, config.enabled ? Qt::Checked : Qt::Unchecked);
}

void HotkeySettingsTab::updateStatusIndicator(QTreeWidgetItem* item, HotkeyStatus status)
{
    QString statusText;
    QColor statusColor;

    switch (status) {
    case HotkeyStatus::Registered:
        statusText = tr("Active");
        statusColor = QColor(0x4C, 0xAF, 0x50);  // Green
        break;
    case HotkeyStatus::Failed:
        statusText = tr("Conflict");
        statusColor = QColor(0xF4, 0x43, 0x36);  // Red
        break;
    case HotkeyStatus::Disabled:
        statusText = tr("Disabled");
        statusColor = QColor(0x9E, 0x9E, 0x9E);  // Gray
        break;
    case HotkeyStatus::Unset:
    default:
        statusText = tr("Not Set");
        statusColor = QColor(0xBD, 0xBD, 0xBD);  // Light gray
        break;
    }

    item->setText(kColumnStatus, statusText);
    item->setForeground(kColumnStatus, statusColor);
}

QTreeWidgetItem* HotkeySettingsTab::findItemByAction(HotkeyAction action) const
{
    for (auto it = m_itemToAction.begin(); it != m_itemToAction.end(); ++it) {
        if (it.value() == action) {
            return it.key();
        }
    }
    return nullptr;
}

HotkeyAction HotkeySettingsTab::getActionFromItem(QTreeWidgetItem* item) const
{
    if (!item) {
        return HotkeyAction::None;
    }
    return static_cast<HotkeyAction>(item->data(0, kActionRole).toInt());
}

bool HotkeySettingsTab::isHotkeyItem(QTreeWidgetItem* item) const
{
    return item && !item->data(0, kIsCategoryRole).toBool();
}

void HotkeySettingsTab::refreshFromManager()
{
    m_updatingFromManager = true;

    QList<HotkeyConfig> configs = HotkeyManager::instance().getAllConfigs();
    for (const HotkeyConfig& config : configs) {
        QTreeWidgetItem* item = findItemByAction(config.action);
        if (item) {
            updateItemDisplay(item, config);
        }
    }

    m_updatingFromManager = false;
}

void HotkeySettingsTab::onItemDoubleClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column)

    if (!isHotkeyItem(item)) {
        return;
    }

    HotkeyAction action = getActionFromItem(item);
    if (action != HotkeyAction::None) {
        showEditDialog(action);
    }
}

void HotkeySettingsTab::onItemChanged(QTreeWidgetItem* item, int column)
{
    if (m_updatingFromManager) {
        return;
    }

    if (column != kColumnEnabled || !isHotkeyItem(item)) {
        return;
    }

    HotkeyAction action = getActionFromItem(item);
    if (action == HotkeyAction::None) {
        return;
    }

    bool enabled = (item->checkState(kColumnEnabled) == Qt::Checked);
    HotkeyManager::instance().setHotkeyEnabled(action, enabled);

    m_hasUnsavedChanges = true;
    emit settingsChanged();
}

void HotkeySettingsTab::onSelectionChanged()
{
    QList<QTreeWidgetItem*> selected = m_treeWidget->selectedItems();
    bool hasSelection = !selected.isEmpty() && isHotkeyItem(selected.first());

    m_editBtn->setEnabled(hasSelection);
    m_clearBtn->setEnabled(hasSelection);
    m_resetBtn->setEnabled(hasSelection);
}

void HotkeySettingsTab::onEditClicked()
{
    QList<QTreeWidgetItem*> selected = m_treeWidget->selectedItems();
    if (selected.isEmpty() || !isHotkeyItem(selected.first())) {
        return;
    }

    HotkeyAction action = getActionFromItem(selected.first());
    if (action != HotkeyAction::None) {
        showEditDialog(action);
    }
}

void HotkeySettingsTab::onClearClicked()
{
    QList<QTreeWidgetItem*> selected = m_treeWidget->selectedItems();
    if (selected.isEmpty() || !isHotkeyItem(selected.first())) {
        return;
    }

    HotkeyAction action = getActionFromItem(selected.first());
    if (action == HotkeyAction::None) {
        return;
    }

    HotkeyConfig config = HotkeyManager::instance().getConfig(action);

    // Warn if clearing a required hotkey
    if (!config.isOptional()) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            tr("Clear Hotkey"),
            tr("Are you sure you want to clear the hotkey for '%1'?\n"
               "This hotkey is required for normal operation.").arg(config.displayName),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);

        if (reply != QMessageBox::Yes) {
            return;
        }
    }

    HotkeyManager::instance().updateHotkey(action, QString());

    m_hasUnsavedChanges = true;
    emit settingsChanged();
}

void HotkeySettingsTab::onResetClicked()
{
    QList<QTreeWidgetItem*> selected = m_treeWidget->selectedItems();
    if (selected.isEmpty() || !isHotkeyItem(selected.first())) {
        return;
    }

    HotkeyAction action = getActionFromItem(selected.first());
    if (action != HotkeyAction::None) {
        HotkeyManager::instance().resetToDefault(action);

        m_hasUnsavedChanges = true;
        emit settingsChanged();
    }
}

void HotkeySettingsTab::onRestoreAllDefaults()
{
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        tr("Restore All Defaults"),
        tr("Are you sure you want to reset all hotkeys to their default values?"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        HotkeyManager::instance().resetAllToDefaults();

        m_hasUnsavedChanges = true;
        emit settingsChanged();
    }
}

void HotkeySettingsTab::onHotkeyManagerChanged(HotkeyAction action, const HotkeyConfig& config)
{
    QTreeWidgetItem* item = findItemByAction(action);
    if (item) {
        m_updatingFromManager = true;
        updateItemDisplay(item, config);
        m_updatingFromManager = false;
    }
}

void HotkeySettingsTab::onRegistrationStatusChanged(HotkeyAction action, HotkeyStatus status)
{
    QTreeWidgetItem* item = findItemByAction(action);
    if (item) {
        updateStatusIndicator(item, status);
    }

    // Update status label with conflict info if needed
    if (status == HotkeyStatus::Failed) {
        HotkeyConfig config = HotkeyManager::instance().getConfig(action);
        m_statusLabel->setText(tr("Failed to register: %1").arg(config.displayName));
        m_statusLabel->setStyleSheet(QStringLiteral("color: #F44336; font-size: 11px;"));
    } else {
        m_statusLabel->clear();
    }
}

void HotkeySettingsTab::showEditDialog(HotkeyAction action)
{
    HotkeyConfig config = HotkeyManager::instance().getConfig(action);

    // Suspend global hotkeys only while TypeHotkeyDialog is open (for key capture)
    HotkeyManager::instance().suspendRegistration();

    TypeHotkeyDialog dialog(this);
    dialog.setActionName(config.displayName);
    dialog.setInitialKeySequence(config.keySequence);

    int result = dialog.exec();

    // Resume global hotkeys immediately after TypeHotkeyDialog closes
    // (QMessageBox dialogs will be handled by MainApplication's modal detection)
    HotkeyManager::instance().resumeRegistration();

    if (result == QDialog::Accepted) {
        QString newSequence = dialog.keySequence();

        // Check for internal conflicts
        auto conflict = HotkeyManager::instance().hasConflict(newSequence, action);
        if (conflict) {
            HotkeyConfig conflictConfig = HotkeyManager::instance().getConfig(*conflict);
            QMessageBox::warning(
                this,
                tr("Hotkey Conflict"),
                tr("The hotkey '%1' is already assigned to '%2'.\n"
                   "Please choose a different hotkey.")
                    .arg(HotkeyManager::formatKeySequence(newSequence))
                    .arg(conflictConfig.displayName));
            return;
        }

        // Apply the new hotkey
        if (!HotkeyManager::instance().updateHotkey(action, newSequence)) {
            QMessageBox::warning(
                this,
                tr("Registration Failed"),
                tr("Failed to register the hotkey '%1'.\n"
                   "It may be in use by another application.")
                    .arg(HotkeyManager::formatKeySequence(newSequence)));
        } else {
            m_hasUnsavedChanges = true;
            emit settingsChanged();
        }
    }
}

}  // namespace SnapTray
