#include "detection/CredentialDetector.h"

#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QtMath>
#include <QtGlobal>

#include <algorithm>
#include <limits>

namespace {

struct PreparedBlock {
    QString text;
    QRectF normalizedRect;
    QRect pixelRect;
    qreal centerY = 0.0;
    qreal leftX = 0.0;
};

struct RowBucket {
    qreal anchorY = 0.0;
    QVector<PreparedBlock> blocks;
};

struct EmbeddedSensitiveValueMatch {
    bool hasSensitiveKey = false;
    int valueStart = -1;
    int valueLength = 0;
};

const QSet<QString> kSensitiveKeywords = {
    QStringLiteral("api_key"),
    QStringLiteral("apikey"),
    QStringLiteral("secret"),
    QStringLiteral("client_secret"),
    QStringLiteral("password"),
    QStringLiteral("passwd"),
    QStringLiteral("pwd"),
    QStringLiteral("token"),
    QStringLiteral("access_token"),
    QStringLiteral("refresh_token"),
    QStringLiteral("bearer"),
    QStringLiteral("authorization"),
    QStringLiteral("private_key"),
    QStringLiteral("x_api_key")
};

const QVector<QStringList> kSensitiveKeywordPhrases = {
    QStringList{QStringLiteral("api"), QStringLiteral("key")},
    QStringList{QStringLiteral("access"), QStringLiteral("token")},
    QStringList{QStringLiteral("refresh"), QStringLiteral("token")},
    QStringList{QStringLiteral("client"), QStringLiteral("secret")},
    QStringList{QStringLiteral("private"), QStringLiteral("key")},
    QStringList{QStringLiteral("x"), QStringLiteral("api"), QStringLiteral("key")}
};

const QRegularExpression kSkPattern(
    QStringLiteral("\\bsk-[A-Za-z0-9](?:[A-Za-z0-9-]{18,}[A-Za-z0-9])\\b"),
    QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kAkiaPattern(
    QStringLiteral("\\bAKIA[0-9A-Z]{16}\\b"));
const QRegularExpression kGhPattern(
    QStringLiteral("\\bgh[pousr]_[A-Za-z0-9_]{20,}\\b"),
    QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kJwtLikePattern(
    QStringLiteral("\\b[A-Za-z0-9_-]{10,}\\.[A-Za-z0-9_-]{10,}\\.[A-Za-z0-9_-]{10,}\\b"));
const QRegularExpression kGenericLongPattern(
    QStringLiteral("\\b[A-Za-z0-9_-]{24,}\\b"));
const QRegularExpression kCredentialSegmentPattern(
    QStringLiteral("^[A-Za-z0-9]{4,}$"));
const QRegularExpression kEmbeddedKeyValuePattern(
    QStringLiteral("^\\s*([A-Za-z0-9_\\-\\s]+?)\\s*[:=]\\s*(.+)$"),
    QRegularExpression::CaseInsensitiveOption);

QString trimOuterPunctuation(const QString& text)
{
    if (text.isEmpty()) {
        return text;
    }

    int begin = 0;
    int end = text.size() - 1;

    auto isTokenChar = [](QChar ch) {
        return ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('-');
    };

    while (begin <= end && !isTokenChar(text[begin])) {
        ++begin;
    }
    while (end >= begin && !isTokenChar(text[end])) {
        --end;
    }

    if (begin > end) {
        return QString();
    }

    return text.mid(begin, end - begin + 1);
}

QString normalizeKeywordToken(const QString& token)
{
    QString normalized = trimOuterPunctuation(token).toLower();
    normalized.replace(QLatin1Char('-'), QLatin1Char('_'));
    normalized.replace(QRegularExpression(QStringLiteral("[^a-z0-9_]+")), QStringLiteral("_"));
    normalized.replace(QRegularExpression(QStringLiteral("_+")), QStringLiteral("_"));
    normalized.remove(QRegularExpression(QStringLiteral("^_+")));
    normalized.remove(QRegularExpression(QStringLiteral("_+$")));
    return normalized;
}

bool isSensitiveKeywordToken(const QString& token)
{
    const QString normalized = normalizeKeywordToken(token);
    if (normalized.isEmpty()) {
        return false;
    }

    if (kSensitiveKeywords.contains(normalized)) {
        return true;
    }

    const QStringList parts = normalized.split(QLatin1Char('_'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return false;
    }

    const int maxSpan = qMin(parts.size(), 3);
    for (int start = 0; start < parts.size(); ++start) {
        for (int span = 1; span <= maxSpan && (start + span) <= parts.size(); ++span) {
            const QString candidate = parts.mid(start, span).join(QStringLiteral("_"));
            if (kSensitiveKeywords.contains(candidate)) {
                return true;
            }
        }
    }

    return false;
}

bool isDelimiterToken(const QString& token)
{
    const QString trimmed = token.trimmed();
    if (trimmed.isEmpty()) {
        return true;
    }

    for (const QChar ch : trimmed) {
        if (ch.isLetterOrNumber()) {
            return false;
        }
    }
    return true;
}

bool isValueChainSeparatorToken(const QString& token)
{
    const QString trimmed = token.trimmed();
    if (trimmed.isEmpty()) {
        return true;
    }

    for (const QChar ch : trimmed) {
        if (ch.isLetterOrNumber()) {
            return false;
        }
        if (ch != QLatin1Char('_') && ch != QLatin1Char('-') && ch != QLatin1Char('.')) {
            return false;
        }
    }

    return true;
}

bool isAssignmentDelimiterToken(const QString& token)
{
    const QString trimmed = token.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    for (const QChar ch : trimmed) {
        if (ch != QLatin1Char(':') && ch != QLatin1Char('=')) {
            return false;
        }
    }
    return true;
}

bool tokenEndsWithAssignmentDelimiter(const QString& token)
{
    const QString trimmed = token.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    const QChar last = trimmed.back();
    return last == QLatin1Char(':') || last == QLatin1Char('=');
}

bool tokenStartsWithAssignmentDelimiter(const QString& token)
{
    const QString trimmed = token.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    const QChar first = trimmed.front();
    return first == QLatin1Char(':') || first == QLatin1Char('=');
}

int nextNonDelimiterIndex(const QVector<PreparedBlock>& row, int fromIndex)
{
    int index = fromIndex;
    while (index < row.size() && isDelimiterToken(row[index].text)) {
        ++index;
    }
    return index;
}

bool matchKeywordPhraseAt(
    const QVector<PreparedBlock>& row,
    int startIndex,
    const QStringList& phrase,
    int* outEndIndex)
{
    if (startIndex < 0 || startIndex >= row.size() || phrase.isEmpty()) {
        return false;
    }

    if (isDelimiterToken(row[startIndex].text)) {
        return false;
    }

    int current = startIndex;
    int endIndex = -1;
    for (const QString& part : phrase) {
        current = nextNonDelimiterIndex(row, current);
        if (current >= row.size()) {
            return false;
        }

        if (normalizeKeywordToken(row[current].text) != part) {
            return false;
        }

        endIndex = current;
        ++current;
    }

    if (outEndIndex) {
        *outEndIndex = endIndex;
    }
    return endIndex >= 0;
}

bool isSensitiveKeywordAt(const QVector<PreparedBlock>& row, int startIndex, int* outEndIndex = nullptr)
{
    if (startIndex < 0 || startIndex >= row.size()) {
        return false;
    }

    if (isSensitiveKeywordToken(row[startIndex].text)) {
        if (outEndIndex) {
            *outEndIndex = startIndex;
        }
        return true;
    }

    for (const QStringList& phrase : kSensitiveKeywordPhrases) {
        int endIndex = -1;
        if (matchKeywordPhraseAt(row, startIndex, phrase, &endIndex)) {
            if (outEndIndex) {
                *outEndIndex = endIndex;
            }
            return true;
        }
    }

    return false;
}

EmbeddedSensitiveValueMatch parseEmbeddedSensitiveValueMatch(const QString& text)
{
    EmbeddedSensitiveValueMatch parsed;

    const QRegularExpressionMatch match = kEmbeddedKeyValuePattern.match(text);
    if (!match.hasMatch()) {
        return parsed;
    }

    const QString key = match.captured(1);
    if (!isSensitiveKeywordToken(key)) {
        return parsed;
    }

    parsed.hasSensitiveKey = true;
    parsed.valueStart = match.capturedStart(2);
    parsed.valueLength = match.capturedLength(2);
    return parsed;
}

bool matchesDirectCredentialPattern(const QString& text)
{
    return kSkPattern.match(text).hasMatch()
        || kAkiaPattern.match(text).hasMatch()
        || kGhPattern.match(text).hasMatch()
        || kJwtLikePattern.match(text).hasMatch();
}

bool matchesGenericLongToken(const QString& text)
{
    return kGenericLongPattern.match(text).hasMatch();
}

bool isAdjacentCredentialFragmentCandidate(const QString& token)
{
    if (matchesDirectCredentialPattern(token) || matchesGenericLongToken(token)) {
        return true;
    }

    if (!kCredentialSegmentPattern.match(token).hasMatch()) {
        return false;
    }

    bool hasDigit = false;
    bool hasUpper = false;
    for (const QChar ch : token) {
        if (ch.isDigit()) {
            hasDigit = true;
            break;
        }
        if (ch.isUpper()) {
            hasUpper = true;
        }
    }

    // Adjacent (non-delimited) continuation is stricter to avoid blurring
    // nearby plain words after a detected value.
    return hasDigit || (hasUpper && token.size() >= 12);
}

qreal medianOf(QVector<qreal> values)
{
    if (values.isEmpty()) {
        return 0.0;
    }

    std::sort(values.begin(), values.end());
    const int count = values.size();
    if ((count % 2) == 1) {
        return values[count / 2];
    }

    return (values[(count / 2) - 1] + values[count / 2]) * 0.5;
}

QRectF clampNormalizedRect(const QRectF& rect)
{
    QRectF normalized = rect.normalized();
    const qreal left = qBound<qreal>(0.0, normalized.left(), 1.0);
    const qreal top = qBound<qreal>(0.0, normalized.top(), 1.0);
    const qreal right = qBound<qreal>(0.0, normalized.right(), 1.0);
    const qreal bottom = qBound<qreal>(0.0, normalized.bottom(), 1.0);
    if (right <= left || bottom <= top) {
        return QRectF();
    }
    return QRectF(QPointF(left, top), QPointF(right, bottom));
}

QRect normalizedToPixelRect(const QRectF& normalizedRect, const QSize& imageSize)
{
    if (!normalizedRect.isValid() || imageSize.width() <= 0 || imageSize.height() <= 0) {
        return QRect();
    }

    const int left = qBound(0, qFloor(normalizedRect.left() * imageSize.width()), imageSize.width() - 1);
    const int top = qBound(0, qFloor(normalizedRect.top() * imageSize.height()), imageSize.height() - 1);
    const int rightEdge = qBound(1, qCeil(normalizedRect.right() * imageSize.width()), imageSize.width());
    const int bottomEdge = qBound(1, qCeil(normalizedRect.bottom() * imageSize.height()), imageSize.height());

    const int width = qMax(1, rightEdge - left);
    const int height = qMax(1, bottomEdge - top);
    return QRect(left, top, width, height);
}

QRect subRectByTextSpan(const PreparedBlock& block, int spanStart, int spanLength)
{
    if (spanStart < 0 || spanLength <= 0 || block.text.isEmpty() || block.pixelRect.isEmpty()) {
        return QRect();
    }

    const int tokenLength = block.text.size();
    if (tokenLength <= 0) {
        return QRect();
    }

    const qreal leftRatio = qBound<qreal>(0.0,
        static_cast<qreal>(spanStart) / static_cast<qreal>(tokenLength), 1.0);
    const qreal rightRatio = qBound<qreal>(0.0,
        static_cast<qreal>(spanStart + spanLength) / static_cast<qreal>(tokenLength), 1.0);
    if (rightRatio <= leftRatio) {
        return QRect();
    }

    const int blockLeft = block.pixelRect.left();
    const int blockTop = block.pixelRect.top();
    const int blockHeight = block.pixelRect.height();
    const int blockRightEdge = block.pixelRect.right() + 1;
    constexpr int kValueRectLeftPaddingPx = 8;
    constexpr int kValueRectRightPaddingPx = 2;

    const int left = qBound(blockLeft,
        blockLeft + qFloor(leftRatio * block.pixelRect.width()),
        blockRightEdge - 1);
    const int rightEdge = qBound(left + 1,
        blockLeft + qCeil(rightRatio * block.pixelRect.width()),
        blockRightEdge);
    const int paddedLeft = qMax(blockLeft, left - kValueRectLeftPaddingPx);
    const int paddedRightEdge = qMin(blockRightEdge, rightEdge + kValueRectRightPaddingPx);
    const int finalRightEdge = qMax(paddedLeft + 1, paddedRightEdge);

    return QRect(paddedLeft, blockTop, qMax(1, finalRightEdge - paddedLeft), blockHeight);
}

QVector<RowBucket> clusterRows(QVector<PreparedBlock> blocks, qreal tolerance)
{
    std::sort(blocks.begin(), blocks.end(), [](const PreparedBlock& a, const PreparedBlock& b) {
        return a.centerY < b.centerY;
    });

    QVector<RowBucket> rows;
    rows.reserve(blocks.size());

    for (const PreparedBlock& block : blocks) {
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

        RowBucket& row = rows[bestRow];
        row.blocks.push_back(block);
        const int count = row.blocks.size();
        row.anchorY = ((row.anchorY * static_cast<qreal>(count - 1)) + block.centerY)
            / static_cast<qreal>(count);
    }

    std::sort(rows.begin(), rows.end(), [](const RowBucket& a, const RowBucket& b) {
        return a.anchorY < b.anchorY;
    });

    for (RowBucket& row : rows) {
        std::sort(row.blocks.begin(), row.blocks.end(), [](const PreparedBlock& a, const PreparedBlock& b) {
            return a.leftX < b.leftX;
        });
    }

    return rows;
}

int findCredentialValueIndex(const QVector<PreparedBlock>& row, int keywordEndIndex)
{
    bool sawAssignmentEvidence = tokenEndsWithAssignmentDelimiter(row[keywordEndIndex].text);
    int i = keywordEndIndex + 1;
    while (i < row.size() && isDelimiterToken(row[i].text)) {
        sawAssignmentEvidence = sawAssignmentEvidence || isAssignmentDelimiterToken(row[i].text);
        ++i;
    }
    if (i >= row.size()) {
        return -1;
    }

    const QString candidate = normalizeKeywordToken(row[i].text);
    if (candidate == QStringLiteral("bearer")) {
        sawAssignmentEvidence = sawAssignmentEvidence || tokenEndsWithAssignmentDelimiter(row[i].text);
        ++i;
        while (i < row.size() && isDelimiterToken(row[i].text)) {
            sawAssignmentEvidence = sawAssignmentEvidence || isAssignmentDelimiterToken(row[i].text);
            ++i;
        }
        if (i >= row.size()) {
            return -1;
        }
    }

    if (isSensitiveKeywordToken(row[i].text)) {
        return -1;
    }

    sawAssignmentEvidence = sawAssignmentEvidence || tokenStartsWithAssignmentDelimiter(row[i].text);
    const QString valueToken = trimOuterPunctuation(row[i].text);
    const bool looksLikeCredentialValue =
        matchesDirectCredentialPattern(valueToken)
        || matchesGenericLongToken(valueToken);
    if (!sawAssignmentEvidence && !looksLikeCredentialValue) {
        return -1;
    }

    return i;
}

void markCredentialValueChain(const QVector<PreparedBlock>& row, QVector<bool>* mark, int valueIndex)
{
    if (!mark || valueIndex < 0 || valueIndex >= row.size()) {
        return;
    }

    (*mark)[valueIndex] = true;
    int lastValueTokenIndex = valueIndex;

    // Follow delimiter-separated value fragments (for example UUID split into segments).
    // Also allow OCR-split adjacent fragments without explicit separators when the
    // horizontal gap is small enough to look like the same token chain.
    bool sawDelimiter = false;
    constexpr qreal kAdjacentGapHeightFactor = 0.6;
    constexpr int kAdjacentGapMinPixels = 2;
    for (int i = valueIndex + 1; i < row.size(); ++i) {
        if (isValueChainSeparatorToken(row[i].text)) {
            sawDelimiter = true;
            continue;
        }

        if (isSensitiveKeywordToken(row[i].text)) {
            break;
        }

        const QString token = trimOuterPunctuation(row[i].text);
        if (token.isEmpty()) {
            break;
        }

        const bool looksLikeCredentialFragment =
            matchesDirectCredentialPattern(token)
            || matchesGenericLongToken(token)
            || kCredentialSegmentPattern.match(token).hasMatch();
        if (!looksLikeCredentialFragment) {
            break;
        }

        bool canContinueChain = sawDelimiter;
        if (!canContinueChain) {
            const QRect& previousRect = row[lastValueTokenIndex].pixelRect;
            const QRect& currentRect = row[i].pixelRect;
            const int gapPixels = currentRect.left() - (previousRect.right() + 1);
            const int maxAllowedGap = qMax(
                kAdjacentGapMinPixels,
                qRound(qMin(previousRect.height(), currentRect.height()) * kAdjacentGapHeightFactor)
            );
            canContinueChain =
                (gapPixels <= maxAllowedGap) && isAdjacentCredentialFragmentCandidate(token);
        }
        if (!canContinueChain) {
            break;
        }

        sawDelimiter = false;
        (*mark)[i] = true;
        lastValueTokenIndex = i;
    }
}

QVector<QRect> mergeNearbyRects(const QVector<QRect>& rects, const QSize& imageSize)
{
    QVector<QRect> merged;
    merged.reserve(rects.size());

    constexpr int kMergePadding = 3;
    const QRect imageBounds(QPoint(0, 0), imageSize);

    for (const QRect& rawRect : rects) {
        QRect current = rawRect.intersected(imageBounds);
        if (current.isEmpty()) {
            continue;
        }

        bool didMerge = true;
        while (didMerge) {
            didMerge = false;
            for (int i = 0; i < merged.size(); ++i) {
                const QRect expanded = merged[i].adjusted(
                    -kMergePadding, -kMergePadding, kMergePadding, kMergePadding);
                if (!expanded.intersects(current)) {
                    continue;
                }
                current = merged[i].united(current);
                merged.removeAt(i);
                didMerge = true;
                break;
            }
        }

        merged.push_back(current);
    }

    std::sort(merged.begin(), merged.end(), [](const QRect& a, const QRect& b) {
        if (a.y() != b.y()) {
            return a.y() < b.y();
        }
        return a.x() < b.x();
    });

    return merged;
}

} // namespace

QVector<QRect> CredentialDetector::detect(const QVector<OCRTextBlock>& blocks, const QSize& imageSize)
{
    if (blocks.isEmpty() || imageSize.width() <= 0 || imageSize.height() <= 0) {
        return {};
    }

    QVector<PreparedBlock> preparedBlocks;
    QVector<qreal> heights;
    preparedBlocks.reserve(blocks.size());
    heights.reserve(blocks.size());

    for (const OCRTextBlock& block : blocks) {
        const QString text = block.text.trimmed();
        if (text.isEmpty()) {
            continue;
        }

        const QRectF normalizedRect = clampNormalizedRect(block.boundingRect);
        if (!normalizedRect.isValid()) {
            continue;
        }

        PreparedBlock prepared;
        prepared.text = text;
        prepared.normalizedRect = normalizedRect;
        prepared.pixelRect = normalizedToPixelRect(normalizedRect, imageSize);
        if (prepared.pixelRect.isEmpty()) {
            continue;
        }
        prepared.centerY = normalizedRect.center().y();
        prepared.leftX = normalizedRect.left();
        preparedBlocks.push_back(prepared);
        heights.push_back(normalizedRect.height());
    }

    if (preparedBlocks.isEmpty()) {
        return {};
    }

    const qreal medianHeight = medianOf(heights);
    const qreal rowTolerance = qMax<qreal>(0.012, medianHeight * 0.65);
    const QVector<RowBucket> rows = clusterRows(std::move(preparedBlocks), rowTolerance);

    QVector<QRect> detectedRegions;
    for (const RowBucket& row : rows) {
        if (row.blocks.isEmpty()) {
            continue;
        }

        const QVector<PreparedBlock>& tokens = row.blocks;
        QVector<bool> mark(tokens.size(), false);
        QVector<EmbeddedSensitiveValueMatch> embeddedMatches(tokens.size());

        bool rowHasSensitiveKeyword = false;
        for (int i = 0; i < tokens.size(); ++i) {
            embeddedMatches[i] = parseEmbeddedSensitiveValueMatch(tokens[i].text);
            if (isSensitiveKeywordAt(tokens, i) || embeddedMatches[i].hasSensitiveKey) {
                rowHasSensitiveKeyword = true;
            }
        }

        for (int i = 0; i < tokens.size(); ++i) {
            const QString& text = tokens[i].text;
            const EmbeddedSensitiveValueMatch& embedded = embeddedMatches[i];

            if (embedded.hasSensitiveKey) {
                const QRect valueRect = subRectByTextSpan(tokens[i], embedded.valueStart, embedded.valueLength);
                if (!valueRect.isEmpty()) {
                    detectedRegions.push_back(valueRect);
                } else {
                    // Fallback: if span mapping fails, keep conservative full-token blur.
                    mark[i] = true;
                }
            }

            if (!embedded.hasSensitiveKey && matchesDirectCredentialPattern(text)) {
                mark[i] = true;
            }
        }

        for (int i = 0; i < tokens.size(); ++i) {
            int keywordEndIndex = -1;
            if (!isSensitiveKeywordAt(tokens, i, &keywordEndIndex)) {
                continue;
            }

            const int valueIndex = findCredentialValueIndex(tokens, keywordEndIndex);
            if (valueIndex >= 0) {
                markCredentialValueChain(tokens, &mark, valueIndex);
            }
        }

        if (rowHasSensitiveKeyword) {
            for (int i = 0; i < tokens.size(); ++i) {
                if (embeddedMatches[i].hasSensitiveKey) {
                    continue;
                }
                if (matchesGenericLongToken(tokens[i].text)) {
                    mark[i] = true;
                }
            }
        }

        for (int i = 0; i < tokens.size(); ++i) {
            if (mark[i]) {
                detectedRegions.push_back(tokens[i].pixelRect);
            }
        }
    }

    return mergeNearbyRects(detectedRegions, imageSize);
}
