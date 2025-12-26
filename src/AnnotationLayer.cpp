#include "AnnotationLayer.h"
#include "annotations/MosaicStroke.h"
#include "annotations/StepBadgeAnnotation.h"
#include "annotations/ErasedItemsGroup.h"
#include <QPainterPath>
#include <QtMath>
#include <QDebug>
#include <QSet>
#include <QRandomGenerator>
#include <algorithm>

// ============================================================================
// TextAnnotation Implementation
// ============================================================================

TextAnnotation::TextAnnotation(const QPoint &position, const QString &text, const QFont &font, const QColor &color)
    : m_position(position)
    , m_text(text)
    , m_font(font)
    , m_color(color)
{
}

QPointF TextAnnotation::center() const
{
    QFontMetrics fm(m_font);
    QStringList lines = m_text.split('\n');

    int maxWidth = 0;
    for (const QString &line : lines) {
        maxWidth = qMax(maxWidth, fm.horizontalAdvance(line));
    }
    int totalHeight = lines.count() * fm.lineSpacing();

    // m_position is baseline position of first line
    // Center is at middle of the text bounding box
    qreal cx = m_position.x() + maxWidth / 2.0;
    qreal cy = m_position.y() - fm.ascent() + totalHeight / 2.0;

    return QPointF(cx, cy);
}

QPolygonF TextAnnotation::transformedBoundingPolygon() const
{
    QFontMetrics fm(m_font);
    QStringList lines = m_text.split('\n');

    int maxWidth = 0;
    for (const QString &line : lines) {
        maxWidth = qMax(maxWidth, fm.horizontalAdvance(line));
    }
    int totalHeight = lines.count() * fm.lineSpacing();

    // Build untransformed bounding rect with margin for outline
    QRectF textRect(m_position.x(),
                    m_position.y() - fm.ascent(),
                    maxWidth,
                    totalHeight);
    textRect.adjust(-5, -5, 5, 5);

    // Build transformation matrix around center
    QPointF c = center();
    QTransform t;
    t.translate(c.x(), c.y());
    t.rotate(m_rotation);
    t.scale(m_scale, m_scale);
    t.translate(-c.x(), -c.y());

    // Transform corners
    QPolygonF poly;
    poly << t.map(textRect.topLeft())
         << t.map(textRect.topRight())
         << t.map(textRect.bottomRight())
         << t.map(textRect.bottomLeft());

    return poly;
}

bool TextAnnotation::containsPoint(const QPoint &point) const
{
    return transformedBoundingPolygon().containsPoint(QPointF(point), Qt::OddEvenFill);
}

bool TextAnnotation::isCacheValid(qreal dpr) const
{
    return !m_cachedPixmap.isNull() &&
           m_cachedDpr == dpr &&
           m_cachedText == m_text &&
           m_cachedFont == m_font &&
           m_cachedColor == m_color &&
           m_cachedPosition == m_position;
}

void TextAnnotation::regenerateCache(qreal dpr) const
{
    QFontMetrics fm(m_font);
    QStringList lines = m_text.split('\n');

    // Calculate bounds
    int maxWidth = 0;
    for (const QString &line : lines) {
        maxWidth = qMax(maxWidth, fm.horizontalAdvance(line));
    }
    int totalHeight = lines.count() * fm.lineSpacing();

    // Create pixmap with padding for outline (3px on each side)
    int padding = 4;
    QSize pixmapSize(maxWidth + 2 * padding, totalHeight + 2 * padding);

    m_cachedPixmap = QPixmap(pixmapSize * dpr);
    m_cachedPixmap.setDevicePixelRatio(dpr);
    m_cachedPixmap.fill(Qt::transparent);

    {
        QPainter offPainter(&m_cachedPixmap);
        offPainter.setRenderHint(QPainter::TextAntialiasing, true);
        offPainter.setFont(m_font);

        QPoint pos(padding, padding + fm.ascent());

        for (const QString &line : lines) {
            if (!line.isEmpty()) {
                QPainterPath path;
                path.addText(pos, m_font, line);

                // White outline
                offPainter.setPen(QPen(Qt::white, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                offPainter.drawPath(path);

                // Fill with color
                offPainter.setPen(Qt::NoPen);
                offPainter.setBrush(m_color);
                offPainter.drawPath(path);
            }
            pos.setY(pos.y() + fm.lineSpacing());
        }
    }

    // Calculate origin: position is baseline, so offset by ascent and padding
    m_cachedOrigin = QPoint(m_position.x() - padding, m_position.y() - fm.ascent() - padding);

    // Update cache state
    m_cachedDpr = dpr;
    m_cachedText = m_text;
    m_cachedFont = m_font;
    m_cachedColor = m_color;
    m_cachedPosition = m_position;
}

void TextAnnotation::draw(QPainter &painter) const
{
    if (m_text.isEmpty()) return;

    qreal dpr = painter.device()->devicePixelRatio();

    // Check if cache is valid, regenerate if needed
    if (!isCacheValid(dpr)) {
        regenerateCache(dpr);
    }

    painter.save();

    // Apply transformations around center
    QPointF c = center();
    painter.translate(c);
    painter.rotate(m_rotation);
    painter.scale(m_scale, m_scale);
    painter.translate(-c);

    // Draw cached pixmap
    painter.drawPixmap(m_cachedOrigin, m_cachedPixmap);

    painter.restore();
}

QRect TextAnnotation::boundingRect() const
{
    // Return axis-aligned bounding rect of transformed polygon
    return transformedBoundingPolygon().boundingRect().toAlignedRect();
}

std::unique_ptr<AnnotationItem> TextAnnotation::clone() const
{
    auto cloned = std::make_unique<TextAnnotation>(m_position, m_text, m_font, m_color);
    cloned->m_rotation = m_rotation;
    cloned->m_scale = m_scale;
    return cloned;
}

void TextAnnotation::setText(const QString &text)
{
    m_text = text;
    invalidateCache();
}

// ============================================================================
// AnnotationLayer Implementation
// ============================================================================

AnnotationLayer::AnnotationLayer(QObject *parent)
    : QObject(parent)
{
}

AnnotationLayer::~AnnotationLayer() = default;

void AnnotationLayer::addItem(std::unique_ptr<AnnotationItem> item)
{
    m_items.push_back(std::move(item));
    m_redoStack.clear();  // Clear redo stack when new item is added
    trimHistory();
    emit changed();
}

void AnnotationLayer::trimHistory()
{
    size_t trimCount = 0;
    while (m_items.size() > kMaxHistorySize) {
        m_items.erase(m_items.begin());
        ++trimCount;
    }
    if (trimCount > 0) {
        // Adjust stored indices in all ErasedItemsGroups
        for (auto& item : m_items) {
            if (auto* group = dynamic_cast<ErasedItemsGroup*>(item.get())) {
                group->adjustIndicesForTrim(trimCount);
            }
        }
        for (auto& item : m_redoStack) {
            if (auto* group = dynamic_cast<ErasedItemsGroup*>(item.get())) {
                group->adjustIndicesForTrim(trimCount);
            }
        }
        renumberStepBadges();
    }
}

void AnnotationLayer::undo()
{
    if (m_items.empty()) return;

    // Check if the last item is an ErasedItemsGroup (from eraser action)
    if (auto* erasedGroup = dynamic_cast<ErasedItemsGroup*>(m_items.back().get())) {
        // Extract the erased items with their original indices
        auto restoredItems = erasedGroup->extractItems();

        // Store original indices for redo support
        std::vector<size_t> indices;
        indices.reserve(restoredItems.size());
        for (const auto& item : restoredItems) {
            indices.push_back(item.originalIndex);
        }
        erasedGroup->setOriginalIndices(std::move(indices));

        // Move the empty group to redo stack
        m_redoStack.push_back(std::move(m_items.back()));
        m_items.pop_back();

        // Sort by original index ascending to restore in correct order
        std::sort(restoredItems.begin(), restoredItems.end(),
            [](const auto& a, const auto& b) { return a.originalIndex < b.originalIndex; });

        // Re-insert items at their original indices
        for (auto &indexed : restoredItems) {
            size_t insertPos = std::min(indexed.originalIndex, m_items.size());
            m_items.insert(m_items.begin() + static_cast<ptrdiff_t>(insertPos), std::move(indexed.item));
        }
    } else {
        // Normal undo: move last item to redo stack
        m_redoStack.push_back(std::move(m_items.back()));
        m_items.pop_back();
    }

    renumberStepBadges();
    emit changed();
}

void AnnotationLayer::redo()
{
    if (m_redoStack.empty()) return;

    // Check if the item in redo stack is an ErasedItemsGroup
    if (auto* erasedGroup = dynamic_cast<ErasedItemsGroup*>(m_redoStack.back().get())) {
        // Get the stored original indices
        auto indices = erasedGroup->originalIndices();
        m_redoStack.pop_back();

        // Sort indices in descending order to remove from back to front
        // (prevents index shifting issues during removal)
        std::sort(indices.begin(), indices.end(), std::greater<size_t>());

        // Remove items at the stored indices
        std::vector<ErasedItemsGroup::IndexedItem> itemsToErase;
        itemsToErase.reserve(indices.size());

        for (size_t idx : indices) {
            if (idx < m_items.size() && !dynamic_cast<ErasedItemsGroup*>(m_items[idx].get())) {
                itemsToErase.push_back({idx, std::move(m_items[idx])});
                m_items.erase(m_items.begin() + static_cast<ptrdiff_t>(idx));
            }
        }

        // Reverse to restore original order (we collected in descending index order)
        std::reverse(itemsToErase.begin(), itemsToErase.end());
        m_items.push_back(std::make_unique<ErasedItemsGroup>(std::move(itemsToErase)));
    } else {
        // Normal redo
        m_items.push_back(std::move(m_redoStack.back()));
        m_redoStack.pop_back();
    }

    renumberStepBadges();
    emit changed();
}

void AnnotationLayer::clear()
{
    m_items.clear();
    m_redoStack.clear();
    emit changed();
}

void AnnotationLayer::draw(QPainter &painter) const
{
    for (const auto &item : m_items) {
        if (item->isVisible()) {
            item->draw(painter);
        }
    }
}

bool AnnotationLayer::canUndo() const
{
    return !m_items.empty();
}

bool AnnotationLayer::canRedo() const
{
    return !m_redoStack.empty();
}

bool AnnotationLayer::isEmpty() const
{
    return m_items.empty();
}

void AnnotationLayer::drawOntoPixmap(QPixmap &pixmap) const
{
    if (isEmpty()) return;

    QPainter painter(&pixmap);
    draw(painter);
}

int AnnotationLayer::countStepBadges() const
{
    int count = 0;
    for (const auto &item : m_items) {
        if (dynamic_cast<const StepBadgeAnnotation*>(item.get())) {
            ++count;
        }
    }
    return count;
}

void AnnotationLayer::renumberStepBadges()
{
    int badgeNumber = 1;
    for (auto &item : m_items) {
        if (auto* badge = dynamic_cast<StepBadgeAnnotation*>(item.get())) {
            badge->setNumber(badgeNumber++);
        }
    }
}

std::vector<ErasedItemsGroup::IndexedItem> AnnotationLayer::removeItemsIntersecting(const QPoint &point, int strokeWidth)
{
    std::vector<ErasedItemsGroup::IndexedItem> removedItems;
    int radius = strokeWidth / 2;
    size_t currentIndex = 0;

    for (auto it = m_items.begin(); it != m_items.end(); ) {
        // Skip ErasedItemsGroup items (they are invisible markers)
        if (dynamic_cast<ErasedItemsGroup*>(it->get())) {
            ++it;
            ++currentIndex;
            continue;
        }

        bool shouldRemove = false;

        // Use path-based intersection for strokes (more accurate)
        if (auto* pencil = dynamic_cast<PencilStroke*>(it->get())) {
            shouldRemove = pencil->intersectsCircle(point, radius);
        } else if (auto* marker = dynamic_cast<MarkerStroke*>(it->get())) {
            shouldRemove = marker->intersectsCircle(point, radius);
        } else {
            // Fallback: expanded bounding rect for shapes/text/badges/etc.
            QRect itemRect = (*it)->boundingRect();
            QRect expandedRect = itemRect.adjusted(-radius, -radius, radius, radius);
            shouldRemove = expandedRect.contains(point);
        }

        if (shouldRemove) {
            // Item intersects with eraser - remove it and record original index
            removedItems.push_back({currentIndex, std::move(*it)});
            it = m_items.erase(it);
            // Don't increment currentIndex since we erased
        } else {
            ++it;
            ++currentIndex;
        }
    }

    if (!removedItems.empty()) {
        m_redoStack.clear();  // Clear redo stack when items are erased
        renumberStepBadges();
        emit changed();
    }

    return removedItems;
}

int AnnotationLayer::hitTestText(const QPoint &pos) const
{
    // Iterate in reverse order (top-most items first)
    for (int i = static_cast<int>(m_items.size()) - 1; i >= 0; --i) {
        // Only hit-test TextAnnotation items
        if (auto* textItem = dynamic_cast<TextAnnotation*>(m_items[i].get())) {
            // Use containsPoint() for accurate hit-testing of rotated/scaled text
            if (textItem->containsPoint(pos)) {
                return i;
            }
        }
    }
    return -1;
}

AnnotationItem* AnnotationLayer::selectedItem()
{
    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_items.size())) {
        return m_items[m_selectedIndex].get();
    }
    return nullptr;
}

AnnotationItem* AnnotationLayer::itemAt(int index)
{
    if (index >= 0 && index < static_cast<int>(m_items.size())) {
        return m_items[index].get();
    }
    return nullptr;
}

bool AnnotationLayer::removeSelectedItem()
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_items.size())) {
        return false;
    }

    // Use ErasedItemsGroup for proper undo/redo support (same pattern as eraser)
    std::vector<ErasedItemsGroup::IndexedItem> removedItems;
    removedItems.push_back({static_cast<size_t>(m_selectedIndex), std::move(m_items[m_selectedIndex])});
    m_items.erase(m_items.begin() + m_selectedIndex);

    // Add ErasedItemsGroup to track the deletion for undo
    m_items.push_back(std::make_unique<ErasedItemsGroup>(std::move(removedItems)));

    m_redoStack.clear();
    m_selectedIndex = -1;
    renumberStepBadges();
    emit changed();
    return true;
}
