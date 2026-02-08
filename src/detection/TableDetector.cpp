#include "detection/TableDetector.h"

#include <QRectF>
#include <QStringList>
#include <QtGlobal>

#include <algorithm>
#include <limits>

namespace {

struct NormalizedBlock {
    QString text;
    QRectF rect;
    qreal leftX = 0.0;
    qreal centerY = 0.0;
};

struct RowBucket {
    qreal anchorY = 0.0;
    QVector<NormalizedBlock> blocks;
};

struct RowCell {
    QString text;
    QRectF rect;
    qreal leftX = 0.0;
};

qreal medianOf(QVector<qreal> values)
{
    if (values.isEmpty()) {
        return 0.0;
    }

    std::sort(values.begin(), values.end());
    const int n = values.size();
    if ((n % 2) == 1) {
        return values[n / 2];
    }

    return (values[(n / 2) - 1] + values[n / 2]) * 0.5;
}

int nearestIndex(const QVector<qreal> &anchors, qreal value)
{
    if (anchors.isEmpty()) {
        return -1;
    }

    int bestIndex = 0;
    qreal bestDistance = qAbs(value - anchors.front());
    for (int i = 1; i < anchors.size(); ++i) {
        const qreal distance = qAbs(value - anchors[i]);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = i;
        }
    }

    return bestIndex;
}

QVector<RowBucket> clusterRows(QVector<NormalizedBlock> blocks, qreal tolerance)
{
    std::sort(blocks.begin(), blocks.end(), [](const NormalizedBlock &a, const NormalizedBlock &b) {
        return a.centerY < b.centerY;
    });

    QVector<RowBucket> rows;
    rows.reserve(blocks.size());

    for (const NormalizedBlock &block : blocks) {
        int bestRow = -1;
        qreal bestDistance = std::numeric_limits<qreal>::max();

        for (int i = 0; i < rows.size(); ++i) {
            const qreal distance = qAbs(block.centerY - rows[i].anchorY);
            if (distance <= tolerance && distance < bestDistance) {
                bestDistance = distance;
                bestRow = i;
            }
        }

        if (bestRow < 0) {
            RowBucket row;
            row.anchorY = block.centerY;
            row.blocks.push_back(block);
            rows.push_back(row);
            continue;
        }

        RowBucket &row = rows[bestRow];
        row.blocks.push_back(block);
        const int count = row.blocks.size();
        row.anchorY = ((row.anchorY * static_cast<qreal>(count - 1)) + block.centerY)
            / static_cast<qreal>(count);
    }

    std::sort(rows.begin(), rows.end(), [](const RowBucket &a, const RowBucket &b) {
        return a.anchorY < b.anchorY;
    });

    for (RowBucket &row : rows) {
        std::sort(row.blocks.begin(), row.blocks.end(), [](const NormalizedBlock &a, const NormalizedBlock &b) {
            return a.leftX < b.leftX;
        });
    }

    return rows;
}

QVector<qreal> clusterAnchors(QVector<qreal> values, qreal tolerance)
{
    std::sort(values.begin(), values.end());

    QVector<qreal> anchors;
    QVector<int> counts;
    anchors.reserve(values.size());
    counts.reserve(values.size());

    for (qreal value : values) {
        if (anchors.isEmpty()) {
            anchors.push_back(value);
            counts.push_back(1);
            continue;
        }

        const qreal distance = qAbs(value - anchors.last());
        if (distance > tolerance) {
            anchors.push_back(value);
            counts.push_back(1);
            continue;
        }

        const int newCount = counts.last() + 1;
        anchors.last() = ((anchors.last() * static_cast<qreal>(counts.last())) + value)
            / static_cast<qreal>(newCount);
        counts.last() = newCount;
    }

    return anchors;
}

QString normalizeCellText(const QStringList &tokens)
{
    return tokens.join(QStringLiteral(" ")).simplified();
}

qreal deriveWordJoinGapThreshold(const QVector<RowBucket> &rows, qreal medianWidth, qreal medianHeight)
{
    QVector<qreal> positiveGaps;
    for (const RowBucket &row : rows) {
        if (row.blocks.size() < 2) {
            continue;
        }

        for (int i = 1; i < row.blocks.size(); ++i) {
            const qreal gap = row.blocks[i].rect.left() - row.blocks[i - 1].rect.right();
            if (gap > 0.0) {
                positiveGaps.push_back(gap);
            }
        }
    }

    // Conservative fallback: merge only near-adjacent words.
    const qreal fallback = qBound<qreal>(
        0.004,
        qMin(medianWidth * 0.2, medianHeight * 0.55),
        0.018);

    if (positiveGaps.size() < 4) {
        return fallback;
    }

    std::sort(positiveGaps.begin(), positiveGaps.end());

    qreal bestJump = 0.0;
    int bestIndex = -1;
    for (int i = 0; i + 1 < positiveGaps.size(); ++i) {
        const qreal jump = positiveGaps[i + 1] - positiveGaps[i];
        if (jump > bestJump) {
            bestJump = jump;
            bestIndex = i;
        }
    }

    // No clear separation between intra-cell and inter-column gaps.
    if (bestIndex < 0 || bestJump < fallback * 0.75) {
        return fallback;
    }

    const qreal lowerGap = positiveGaps[bestIndex];
    const qreal upperGap = positiveGaps[bestIndex + 1];
    const bool hasDistinctGapGroups =
        (upperGap >= qMax(lowerGap * 1.8, lowerGap + fallback))
        && (lowerGap <= qMax(fallback * 2.0, 0.02));
    if (!hasDistinctGapGroups) {
        return fallback;
    }

    const qreal threshold = (lowerGap + upperGap) * 0.5;
    return qBound<qreal>(0.005, threshold, 0.06);
}

QVector<RowCell> mergeWordsIntoCells(const QVector<NormalizedBlock> &sortedBlocks, qreal joinGapThreshold)
{
    QVector<RowCell> cells;
    if (sortedBlocks.isEmpty()) {
        return cells;
    }

    cells.reserve(sortedBlocks.size());

    RowCell currentCell;
    currentCell.text = sortedBlocks.front().text;
    currentCell.rect = sortedBlocks.front().rect;
    currentCell.leftX = sortedBlocks.front().leftX;

    for (int i = 1; i < sortedBlocks.size(); ++i) {
        const NormalizedBlock &block = sortedBlocks[i];
        const qreal gap = block.rect.left() - currentCell.rect.right();
        const bool shouldMerge = gap <= joinGapThreshold;

        if (!shouldMerge) {
            cells.push_back(currentCell);
            currentCell.text = block.text;
            currentCell.rect = block.rect;
            currentCell.leftX = block.leftX;
            continue;
        }

        if (!currentCell.text.isEmpty() && !block.text.isEmpty()) {
            currentCell.text += QLatin1Char(' ');
        }
        currentCell.text += block.text;
        currentCell.rect = currentCell.rect.united(block.rect);
        currentCell.leftX = currentCell.rect.left();
    }

    cells.push_back(currentCell);
    return cells;
}

} // namespace

TableDetectionResult TableDetector::detect(const QVector<OCRTextBlock> &blocks)
{
    TableDetectionResult result;

    if (blocks.size() < 4) {
        return result;
    }

    QVector<NormalizedBlock> prepared;
    QVector<qreal> widths;
    QVector<qreal> heights;
    prepared.reserve(blocks.size());
    widths.reserve(blocks.size());
    heights.reserve(blocks.size());

    for (const OCRTextBlock &block : blocks) {
        const QString text = block.text.trimmed();
        if (text.isEmpty()) {
            continue;
        }

        QRectF rect = block.boundingRect.normalized();
        if (rect.width() <= 0.0 || rect.height() <= 0.0) {
            continue;
        }

        const qreal left = qBound<qreal>(0.0, rect.left(), 1.0);
        const qreal top = qBound<qreal>(0.0, rect.top(), 1.0);
        const qreal right = qBound<qreal>(0.0, rect.right(), 1.0);
        const qreal bottom = qBound<qreal>(0.0, rect.bottom(), 1.0);
        if (right <= left || bottom <= top) {
            continue;
        }

        rect = QRectF(QPointF(left, top), QPointF(right, bottom));

        NormalizedBlock preparedBlock;
        preparedBlock.text = text;
        preparedBlock.rect = rect;
        preparedBlock.leftX = rect.left();
        preparedBlock.centerY = rect.center().y();
        prepared.push_back(preparedBlock);
        widths.push_back(rect.width());
        heights.push_back(rect.height());
    }

    if (prepared.size() < 4) {
        return result;
    }

    const qreal medianHeight = medianOf(heights);
    const qreal medianWidth = medianOf(widths);
    const qreal rowTolerance = qMax<qreal>(0.012, medianHeight * 0.6);
    const qreal columnTolerance = qMax<qreal>(0.015, medianWidth * 0.5);

    QVector<RowBucket> rows = clusterRows(std::move(prepared), rowTolerance);
    result.rowCount = rows.size();
    if (result.rowCount < 2) {
        return result;
    }

    const qreal joinGapThreshold = deriveWordJoinGapThreshold(rows, medianWidth, medianHeight);
    QVector<QVector<RowCell>> rowCells;
    rowCells.reserve(rows.size());
    for (const RowBucket &row : rows) {
        rowCells.push_back(mergeWordsIntoCells(row.blocks, joinGapThreshold));
    }

    QVector<qreal> columnCandidates;
    for (const QVector<RowCell> &cells : rowCells) {
        for (const RowCell &cell : cells) {
            columnCandidates.push_back(cell.leftX);
        }
    }

    QVector<qreal> allAnchors = clusterAnchors(std::move(columnCandidates), columnTolerance);
    if (allAnchors.size() < 2) {
        return result;
    }

    QVector<QVector<QStringList>> cellTokens(
        rows.size(),
        QVector<QStringList>(allAnchors.size()));

    for (int rowIndex = 0; rowIndex < rowCells.size(); ++rowIndex) {
        const QVector<RowCell> &cells = rowCells[rowIndex];
        for (const RowCell &cell : cells) {
            const int nearestColumn = nearestIndex(allAnchors, cell.leftX);
            if (nearestColumn < 0) {
                continue;
            }

            const qreal distance = qAbs(cell.leftX - allAnchors[nearestColumn]);
            if (distance > qMax(columnTolerance * 1.5, 0.03)) {
                continue;
            }

            const QString normalizedText = normalizeCellText(cell.text.split(QLatin1Char(' '), Qt::SkipEmptyParts));
            if (!normalizedText.isEmpty()) {
                cellTokens[rowIndex][nearestColumn].append(normalizedText);
            }
        }
    }

    int rowsWithAtLeastTwoColumns = 0;
    QVector<int> columnPresence(allAnchors.size(), 0);
    for (int rowIndex = 0; rowIndex < cellTokens.size(); ++rowIndex) {
        int nonEmptyColumns = 0;
        for (int columnIndex = 0; columnIndex < cellTokens[rowIndex].size(); ++columnIndex) {
            if (!cellTokens[rowIndex][columnIndex].isEmpty()) {
                ++nonEmptyColumns;
                ++columnPresence[columnIndex];
            }
        }

        if (nonEmptyColumns >= 2) {
            ++rowsWithAtLeastTwoColumns;
        }
    }

    if (rowsWithAtLeastTwoColumns < 2) {
        return result;
    }

    QVector<int> stableColumns;
    stableColumns.reserve(allAnchors.size());
    for (int columnIndex = 0; columnIndex < columnPresence.size(); ++columnIndex) {
        const qreal coverage = static_cast<qreal>(columnPresence[columnIndex])
            / static_cast<qreal>(rows.size());
        if (coverage >= 0.6) {
            stableColumns.push_back(columnIndex);
        }
    }

    if (stableColumns.size() < 2) {
        return result;
    }

    QStringList tsvLines;
    tsvLines.reserve(rows.size());
    for (int rowIndex = 0; rowIndex < cellTokens.size(); ++rowIndex) {
        QStringList cells;
        cells.reserve(stableColumns.size());

        for (int stableColumn : stableColumns) {
            cells.push_back(normalizeCellText(cellTokens[rowIndex][stableColumn]));
        }

        tsvLines.push_back(cells.join(QLatin1Char('\t')));
    }

    result.columnCount = stableColumns.size();
    result.tsv = tsvLines.join(QLatin1Char('\n'));
    result.isTable = !result.tsv.isEmpty();
    return result;
}
