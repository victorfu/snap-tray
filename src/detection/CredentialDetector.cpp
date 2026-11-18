#include "detection/SensitiveCategory.h"
#include "detection/CredentialDetector.h"

#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QStringList>
#include <QtMath>
#include <QtGlobal>

#include <algorithm>
#include <limits>

namespace {

struct PreparedBlock {
    QString text;
    QVector<OCRTextRange> textRanges;
    int textOffset = 0;
    QSize imageSize;
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

    // The matcher uses trimmed text; producer ranges refer to the original text.
    const int start = spanStart + block.textOffset;
    const int end = start + spanLength;
    QVector<bool> covered(spanLength, false);
    QRect result;
    for (const auto& range : block.textRanges) {
        if (range.start < 0 || range.length <= 0
            || range.start > block.textOffset + block.text.size()
            || range.length > block.textOffset + block.text.size() - range.start) {
            continue;
        }
        const int overlapStart = qMax(start, range.start);
        const int overlapEnd = qMin(end, range.start + range.length);
        if (overlapStart >= overlapEnd) {
            continue;
        }
        const QRectF rect = range.boundingRect;
        if (!qIsFinite(rect.x()) || !qIsFinite(rect.y())
            || !qIsFinite(rect.width()) || !qIsFinite(rect.height())
            || !rect.isValid() || !QRectF(0, 0, 1, 1).contains(rect)) {
            continue;
        }
        result = result.united(normalizedToPixelRect(rect, block.imageSize));
        for (int i = overlapStart; i < overlapEnd; ++i) {
            covered[i - start] = true;
        }
    }
    // Never infer glyph positions from character counts. Incomplete native
    // geometry (including a missing whitespace range) conservatively blurs the block.
    if (result.isEmpty() || std::find(covered.cbegin(), covered.cend(), false) != covered.cend()) {
        return block.pixelRect.united(result);
    }
    return result;
}

constexpr int kMinCardDigits = 13;
constexpr int kMaxCardDigits = 19;

bool validCard(const QString& text)
{
    QString digits = text;
    digits.remove(' ').remove('-').remove('\n');
    if (digits.size() < kMinCardDigits || digits.size() > kMaxCardDigits || digits.count(digits.front()) == digits.size())
        return false;
    int sum = 0;
    bool doubleDigit = false;
    for (auto it = digits.crbegin(); it != digits.crend(); ++it) {
        int digit = it->digitValue();
        if (doubleDigit) {
            digit *= 2;
            if (digit > 9)
                digit -= 9;
        }
        sum += digit;
        doubleDigit = !doubleDigit;
    }
    return sum % 10 == 0;
}

// A following expiry or security code can extend the numeric match. Only
// shorten at a field boundary, never inside a contiguous run of digits.
qsizetype cardMatchLength(QString text)
{
    while (text.size() >= kMinCardDigits) {
        if (validCard(text))
            return text.size();
        const qsizetype boundary = text.lastIndexOf(QRegularExpression(QStringLiteral("[ \n-]")));
        if (boundary < 0)
            break;
        text.truncate(boundary);
    }
    return 0;
}

bool validAddress(const QString& text)
{
    for (const QString& part : text.split('.')) {
        if (part.toInt() > 255 || (part.size() > 1 && part.startsWith('0')))
            return false;
    }
    return true;
}

bool validWebUrl(const QString& text)
{
    const QUrl url(text, QUrl::StrictMode);
    return url.isValid() && !url.host().isEmpty();
}

struct SensitiveRule {
    QString category;
    QRegularExpression pattern;
    bool (*validate)(const QString&) = nullptr;
};

const QVector<SensitiveRule>& sensitiveRules()
{
    static const QVector<SensitiveRule> rules{
        {SensitiveCategory::Email, QRegularExpression(QStringLiteral(R"([A-Za-z0-9_%+-]+(?:\.[A-Za-z0-9_%+-]+)*@[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)*\.[A-Za-z]{2,})"))},
        {SensitiveCategory::Url, QRegularExpression(QStringLiteral(R"(https?://[^\s\x{2029}<>"，。]+)"), QRegularExpression::CaseInsensitiveOption), validWebUrl},
        {SensitiveCategory::Ipv4, QRegularExpression(QStringLiteral(R"((?<![A-Za-z0-9.])(?:[0-9]{1,3}\.){3}[0-9]{1,3}(?!\.?[A-Za-z0-9]))")), validAddress},
        {SensitiveCategory::Phone, QRegularExpression(QStringLiteral(R"((?<![\w+])(?<![0-9][ -])(?:\+[0-9]{1,3}[ -]?)?(?:\([0-9]{2,4}\)|[0-9]{2,4})[ -]?[0-9]{3,4}[- ][0-9]{3,4}(?![0-9 -]*[0-9]))"))},
        {SensitiveCategory::Card, QRegularExpression(QStringLiteral(R"((?<![0-9])(?:[0-9][ \n-]?){12,18}[0-9](?![0-9]))"))}
    };
    return rules;
}

// Keep offsets in the joined row so matches split by OCR can still use
// each contributing block's native UTF-16 geometry.
void appendSensitiveRegions(const QVector<PreparedBlock>& blocks, const QStringList& categories,
                            QVector<QRect>& regions, qsizetype wrapAt = -1)
{
    constexpr int kMinimumEdgePadding = 2;
    constexpr qreal kEdgePaddingToHeight = 0.10;
    const auto appendLine = [&](const QRect& line) {
        if (line.isEmpty())
            return;
        // Leave room for glyph edges outside the native OCR box.
        const int padding = qMax(kMinimumEdgePadding, qCeil(line.height() * kEdgePaddingToHeight));
        regions.append(line.adjusted(-padding, -padding, padding, padding));
    };
    QString row;
    qsizetype wrapOffset = -1;
    QVector<int> starts;
    for (const auto& block : blocks) {
        if (starts.size() == wrapAt) {
            wrapOffset = row.size();
            row += '\n';
        } else if (!row.isEmpty()) {
            const auto& previous = blocks[starts.size() - 1];
            const int gap = block.pixelRect.left() - previous.pixelRect.right() - 1;
            const int lineHeight = qMax(previous.pixelRect.height(), block.pixelRect.height());
            // Do not join columns. Keep punctuation attached across adjacent
            // word boxes, as native OCR can tokenize a URL or email address.
            if (gap > lineHeight)
                row += QChar::ParagraphSeparator;
            else if (!QStringLiteral("@./:_+-?&#=%").contains(row.back())
                     && !QStringLiteral("@./:_+-?&#=%").contains(block.text.front()))
                row += ' ';
        }
        starts.append(row.size());
        row += block.text;
    }
    for (const auto& rule : sensitiveRules()) {
        if (!categories.contains(rule.category))
            continue;
        qsizetype offset = 0;
        while (offset < row.size()) {
            const auto match = rule.pattern.match(row, offset);
            if (!match.hasMatch())
                break;
            const qsizetype length = rule.category == SensitiveCategory::Card
                ? cardMatchLength(match.captured()) : match.capturedLength();
            const qsizetype matchEnd = match.capturedStart() + length;
            // A rejected card may start with an unrelated numeric field. The
            // digit lookbehind makes the next retry start at a field boundary.
            // On success, leave any trimmed trailing fields available to match.
            offset = length ? matchEnd : match.capturedStart() + 1;
            if (!length || (wrapOffset >= 0 && (match.capturedStart() >= wrapOffset || matchEnd <= wrapOffset)))
                continue;
            if (rule.validate && !rule.validate(match.captured()))
                continue;
            QRect rect;
            for (qsizetype i = 0; i < blocks.size(); ++i) {
                if (i == wrapAt) {
                    appendLine(rect);
                    rect = {};
                }
                const int start = qMax<int>(match.capturedStart(), starts[i]);
                const int end = qMin<int>(matchEnd, starts[i] + blocks[i].text.size());
                if (end > start)
                    rect = rect.united(subRectByTextSpan(blocks[i], start - starts[i], end - start));
            }
            appendLine(rect);
        }
    }
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

QVector<QRect> CredentialDetector::detect(const QVector<OCRTextBlock>& blocks, const QSize& imageSize,
                                          const QStringList& categories)
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
        prepared.textRanges = block.textRanges;
        while (prepared.textOffset < block.text.size()
               && block.text[prepared.textOffset].isSpace()) {
            ++prepared.textOffset;
        }
        prepared.imageSize = imageSize;
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
    QVector<QRect> optionalRegions;
    for (const RowBucket& row : rows) {
        if (row.blocks.isEmpty()) {
            continue;
        }

        const QVector<PreparedBlock>& tokens = row.blocks;
        if (!categories.isEmpty())
            appendSensitiveRegions(tokens, categories, optionalRegions);
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

    if (categories.contains(SensitiveCategory::Card)) {
        for (qsizetype i = 1; i < rows.size(); ++i) {
            const auto& previous = rows[i - 1].blocks;
            const auto& next = rows[i].blocks;
            const QRect last = previous.back().pixelRect;
            const QRect first = next.front().pixelRect;
            const int lineHeight = qMax(last.height(), first.height());
            if (first.top() - last.bottom() > lineHeight ||
                first.left() > last.right() + lineHeight ||
                first.right() < previous.front().pixelRect.left() - lineHeight)
                continue;
            appendSensitiveRegions(previous + next, {SensitiveCategory::Card}, optionalRegions, previous.size());
        }
    }
    auto candidates = mergeNearbyRects(detectedRegions, imageSize);
    const QRect bounds(QPoint(), imageSize);
    for (const auto& region : optionalRegions) {
        const QRect clipped = region.intersected(bounds);
        // With word- or block-only geometry several matches can point to
        // the same pixels. Coalesce containment, but keep neighbours selectable.
        if (std::any_of(candidates.cbegin(), candidates.cend(),
                        [&](const QRect& item) { return item.contains(clipped); }))
            continue;
        candidates.removeIf([&](const QRect& item) { return clipped.contains(item); });
        candidates.append(clipped);
    }
    return candidates;
}
