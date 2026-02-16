#include "region/MultiRegionListPanel.h"

#include "utils/CoordinateHelper.h"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
constexpr int kThumbnailWidth = 72;
constexpr int kThumbnailHeight = 46;
constexpr int kItemHeight = 72;
}

MultiRegionListPanel::MultiRegionListPanel(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setFocusPolicy(Qt::NoFocus);
    setObjectName(QStringLiteral("multiRegionListPanel"));
    setStyleSheet(
        "#multiRegionListPanel {"
        "  background-color: rgba(25, 25, 25, 185);"
        "  border: 1px solid rgba(255, 255, 255, 35);"
        "  border-radius: 8px;"
        "}"
    );

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(8, 8, 8, 8);
    rootLayout->setSpacing(6);

    auto* titleLabel = new QLabel(tr("Regions"), this);
    titleLabel->setStyleSheet("color: white; font-weight: 600; padding-left: 2px;");
    titleLabel->setFocusPolicy(Qt::NoFocus);
    rootLayout->addWidget(titleLabel);

    m_listWidget = new QListWidget(this);
    m_listWidget->setObjectName(QStringLiteral("multiRegionListWidget"));
    m_listWidget->setFrameShape(QFrame::NoFrame);
    m_listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_listWidget->setDragDropMode(QAbstractItemView::InternalMove);
    m_listWidget->setDefaultDropAction(Qt::MoveAction);
    m_listWidget->setDragEnabled(true);
    m_listWidget->setAcceptDrops(true);
    m_listWidget->setDropIndicatorShown(true);
    m_listWidget->setSpacing(6);
    m_listWidget->setFocusPolicy(Qt::NoFocus);
    m_listWidget->setStyleSheet(
        "QListWidget { background: transparent; color: white; }"
        "QListWidget::item { border: none; }"
        "QListWidget::item:selected { background: rgba(0, 174, 255, 55); border-radius: 6px; }"
    );
    rootLayout->addWidget(m_listWidget);

    connect(m_listWidget, &QListWidget::currentRowChanged,
            this, [this](int row) {
                if (m_updatingList || row < 0) {
                    return;
                }
                emit regionActivated(row);
            });

    connect(m_listWidget->model(), &QAbstractItemModel::rowsMoved,
            this,
            [this](const QModelIndex&, int sourceStart, int sourceEnd,
                   const QModelIndex&, int destinationRow) {
                if (m_updatingList || sourceStart != sourceEnd) {
                    return;
                }

                int toIndex = destinationRow;
                if (sourceStart < destinationRow) {
                    toIndex -= 1;
                }

                const int maxIndex = m_listWidget->count() - 1;
                toIndex = qBound(0, toIndex, maxIndex);
                if (toIndex == sourceStart) {
                    return;
                }
                emit regionMoveRequested(sourceStart, toIndex);
            });

    applyEffectiveCursor();
}

void MultiRegionListPanel::setInteractionCursor(Qt::CursorShape shape)
{
    const bool changed = !m_hasInteractionCursorOverride || m_interactionCursorShape != shape;
    m_hasInteractionCursorOverride = true;
    m_interactionCursorShape = shape;
    if (changed) {
        applyEffectiveCursor();
    }
}

void MultiRegionListPanel::clearInteractionCursor()
{
    if (!m_hasInteractionCursorOverride) {
        return;
    }
    m_hasInteractionCursorOverride = false;
    applyEffectiveCursor();
}

Qt::CursorShape MultiRegionListPanel::effectiveCursorShape() const
{
    return m_hasInteractionCursorOverride ? m_interactionCursorShape : Qt::ArrowCursor;
}

void MultiRegionListPanel::applyEffectiveCursor()
{
    applyCursorToDescendants(effectiveCursorShape());
}

void MultiRegionListPanel::applyCursorToDescendants(Qt::CursorShape shape)
{
    if (m_hasAppliedCursor && m_appliedCursorShape == shape) {
        return;
    }

    m_hasAppliedCursor = true;
    m_appliedCursorShape = shape;
    setCursor(shape);

    if (m_listWidget) {
        m_listWidget->setCursor(shape);
        if (m_listWidget->viewport()) {
            m_listWidget->viewport()->setCursor(shape);
        }
    }

    const auto descendants = findChildren<QWidget*>();
    for (auto* child : descendants) {
        if (child) {
            child->setCursor(shape);
        }
    }
}

void MultiRegionListPanel::setCaptureContext(const QPixmap& background, qreal dpr)
{
    const qreal normalizedDpr = dpr > 0.0 ? dpr : 1.0;
    const qint64 cacheKey = background.cacheKey();
    if (m_backgroundCacheKey != cacheKey || m_devicePixelRatio != normalizedDpr) {
        m_thumbnailCache.clear();
    }

    m_backgroundPixmap = background;
    m_backgroundCacheKey = cacheKey;
    m_devicePixelRatio = normalizedDpr;
}

void MultiRegionListPanel::setRegions(const QVector<MultiRegionManager::Region>& regions)
{
    m_regions = regions;
    if (m_regions.isEmpty()) {
        m_thumbnailCache.clear();
    }
    rebuildList();
}

void MultiRegionListPanel::setActiveIndex(int index)
{
    m_updatingList = true;
    if (index >= 0 && index < m_listWidget->count()) {
        m_listWidget->setCurrentRow(index);
    } else {
        m_listWidget->setCurrentRow(-1);
    }
    m_updatingList = false;
}

void MultiRegionListPanel::updatePanelGeometry(const QSize& parentSize)
{
    const int width = kPanelWidth;
    const int height = qMax(120, parentSize.height() - (kPanelMargin * 2));
    setGeometry(kPanelMargin, kPanelMargin, width, height);
}

void MultiRegionListPanel::rebuildList()
{
    m_updatingList = true;
    m_listWidget->setUpdatesEnabled(false);
    m_listWidget->clear();

    for (const auto& region : m_regions) {
        auto* item = new QListWidgetItem();
        item->setSizeHint(QSize(kPanelWidth - 20, kItemHeight));
        m_listWidget->addItem(item);
        m_listWidget->setItemWidget(item, createItemWidget(region));
    }

    m_listWidget->setUpdatesEnabled(true);
    m_listWidget->viewport()->update();
    m_updatingList = false;
}

QWidget* MultiRegionListPanel::createItemWidget(const MultiRegionManager::Region& region)
{
    const Qt::CursorShape cursorShape = effectiveCursorShape();

    auto* container = new QWidget(m_listWidget);
    container->setFocusPolicy(Qt::NoFocus);
    container->setCursor(cursorShape);

    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(8);

    auto* thumbLabel = new QLabel(container);
    thumbLabel->setFixedSize(kThumbnailWidth, kThumbnailHeight);
    thumbLabel->setAlignment(Qt::AlignCenter);
    thumbLabel->setCursor(cursorShape);
    thumbLabel->setStyleSheet("border: 1px solid rgba(255,255,255,40); border-radius: 4px;");
    const QPixmap thumbnail = thumbnailForRegion(region);
    if (!thumbnail.isNull()) {
        thumbLabel->setPixmap(thumbnail.scaled(
            thumbLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    layout->addWidget(thumbLabel);

    auto* textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);

    auto* titleLabel = new QLabel(tr("Region %1").arg(region.index), container);
    titleLabel->setStyleSheet("color: white; font-weight: 600;");
    titleLabel->setFocusPolicy(Qt::NoFocus);
    titleLabel->setCursor(cursorShape);
    textLayout->addWidget(titleLabel);

    auto* sizeLabel = new QLabel(
        tr("%1 x %2").arg(region.rect.width()).arg(region.rect.height()), container);
    sizeLabel->setStyleSheet("color: rgba(255,255,255,180);");
    sizeLabel->setFocusPolicy(Qt::NoFocus);
    sizeLabel->setCursor(cursorShape);
    textLayout->addWidget(sizeLabel);
    textLayout->addStretch();
    layout->addLayout(textLayout, 1);

    auto* buttonsLayout = new QVBoxLayout();
    buttonsLayout->setContentsMargins(0, 0, 0, 0);
    buttonsLayout->setSpacing(4);

    auto* replaceButton = new QPushButton(tr("Replace"), container);
    replaceButton->setObjectName(QStringLiteral("replaceButton"));
    replaceButton->setFocusPolicy(Qt::NoFocus);
    replaceButton->setFixedHeight(24);
    replaceButton->setCursor(cursorShape);
    buttonsLayout->addWidget(replaceButton);

    auto* deleteButton = new QPushButton(tr("Delete"), container);
    deleteButton->setObjectName(QStringLiteral("deleteButton"));
    deleteButton->setFocusPolicy(Qt::NoFocus);
    deleteButton->setFixedHeight(24);
    deleteButton->setCursor(cursorShape);
    buttonsLayout->addWidget(deleteButton);
    layout->addLayout(buttonsLayout);

    auto resolveRow = [this](QWidget* sourceWidget) -> int {
        if (!sourceWidget) {
            return -1;
        }
        const QPoint center = sourceWidget->rect().center();
        const QPoint listPos = sourceWidget->mapTo(m_listWidget->viewport(), center);
        if (auto* item = m_listWidget->itemAt(listPos)) {
            return m_listWidget->row(item);
        }
        return -1;
    };

    connect(replaceButton, &QPushButton::clicked,
            this, [this, replaceButton, resolveRow]() {
                const int row = resolveRow(replaceButton);
                if (row < 0) {
                    return;
                }
                m_listWidget->setCurrentRow(row);
                emit regionReplaceRequested(row);
            });

    connect(deleteButton, &QPushButton::clicked,
            this, [this, deleteButton, resolveRow]() {
                const int row = resolveRow(deleteButton);
                if (row < 0) {
                    return;
                }
                m_listWidget->setCurrentRow(row);
                emit regionDeleteRequested(row);
            });

    return container;
}

QPixmap MultiRegionListPanel::thumbnailForRegion(const MultiRegionManager::Region& region) const
{
    if (m_backgroundPixmap.isNull() || !region.rect.isValid() || region.rect.isEmpty()) {
        return QPixmap();
    }

    const QString cacheKey = thumbnailCacheKey(region);
    auto cached = m_thumbnailCache.constFind(cacheKey);
    if (cached != m_thumbnailCache.cend()) {
        return cached.value();
    }

    const qreal dpr = m_devicePixelRatio > 0.0 ? m_devicePixelRatio : 1.0;
    const QRect physicalRect = CoordinateHelper::toPhysical(region.rect, dpr);
    if (!physicalRect.isValid() || physicalRect.isEmpty()) {
        return QPixmap();
    }

    const qreal thumbDpr = qMax<qreal>(1.0, devicePixelRatioF());
    QPixmap thumbnail(
        static_cast<int>(kThumbnailWidth * thumbDpr),
        static_cast<int>(kThumbnailHeight * thumbDpr));
    thumbnail.setDevicePixelRatio(thumbDpr);
    thumbnail.fill(Qt::transparent);

    QPainter painter(&thumbnail);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawPixmap(QRect(0, 0, kThumbnailWidth, kThumbnailHeight), m_backgroundPixmap, physicalRect);
    painter.end();

    m_thumbnailCache.insert(cacheKey, thumbnail);
    return thumbnail;
}

QString MultiRegionListPanel::thumbnailCacheKey(const MultiRegionManager::Region& region) const
{
    const QRect rect = region.rect.normalized();
    return QStringLiteral("%1|%2|%3|%4|%5|%6")
        .arg(m_backgroundCacheKey)
        .arg(m_devicePixelRatio, 0, 'f', 4)
        .arg(rect.x())
        .arg(rect.y())
        .arg(rect.width())
        .arg(rect.height());
}
