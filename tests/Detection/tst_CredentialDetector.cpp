#include <QtTest>
#include <QtMath>

#include "detection/CredentialDetector.h"

namespace {

OCRTextBlock makeBlock(const QString& text, qreal x, qreal y, qreal width = 0.14, qreal height = 0.04)
{
    OCRTextBlock block;
    block.text = text;
    block.boundingRect = QRectF(x, y, width, height);
    block.confidence = 0.9f;
    return block;
}

QRect expectedPixelRect(const QRectF& normalizedRect, const QSize& imageSize)
{
    const QRectF normalized = normalizedRect.normalized();
    const int left = qBound(0, qFloor(normalized.left() * imageSize.width()), imageSize.width() - 1);
    const int top = qBound(0, qFloor(normalized.top() * imageSize.height()), imageSize.height() - 1);
    const int rightEdge = qBound(1, qCeil(normalized.right() * imageSize.width()), imageSize.width());
    const int bottomEdge = qBound(1, qCeil(normalized.bottom() * imageSize.height()), imageSize.height());
    return QRect(left, top, qMax(1, rightEdge - left), qMax(1, bottomEdge - top));
}

bool containsRectCenter(const QVector<QRect>& regions, const QRect& rect)
{
    const QPoint center = rect.center();
    for (const QRect& region : regions) {
        if (region.contains(center)) {
            return true;
        }
    }
    return false;
}

bool containsPoint(const QVector<QRect>& regions, const QPoint& point)
{
    for (const QRect& region : regions) {
        if (region.contains(point)) {
            return true;
        }
    }
    return false;
}

} // namespace

class tst_CredentialDetector : public QObject
{
    Q_OBJECT

private slots:
    void testDetect_KeyValue_MarksValueOnly();
    void testDetect_SplitApiKeyPhrase_MarksValue();
    void testDetect_SplitApiDashKeyPhrase_MarksValue();
    void testDetect_InlineApiKeyLabel_MarksValue();
    void testDetect_EmbeddedInlineKeyValue_MarksValueSubRectOnly();
    void testDetect_UuidSegmentsAfterApiKey_MarksAllSegments();
    void testDetect_PasswordColon_MarksValue();
    void testDetect_DirectPatternsWithoutKeyword();
    void testDetect_HyphenatedSkPatternWithoutKeyword();
    void testDetect_GenericLongTokenRequiresKeyword();
    void testDetect_KeywordWithoutAssignmentAndPlainValue_NotMarked();
    void testDetect_KeywordWithDirectPatternWithoutAssignment_MarksValue();
    void testDetect_KeywordWithValuePrefixedByAssignment_MarksValue();
    void testDetect_SplitAdjacentFragments_MarksAllFragments();
    void testDetect_AdjacentPlainWordsAfterValue_NotMarked();
    void testDetect_CommaDelimitedPlainWordsAfterValue_NotMarked();
    void testDetect_MergeOverlappingRegions();
};

void tst_CredentialDetector::testDetect_KeyValue_MarksValueOnly()
{
    const QSize imageSize(1200, 700);
    const QVector<OCRTextBlock> blocks{
        makeBlock("api_key", 0.10, 0.20, 0.10, 0.04),
        makeBlock("=", 0.22, 0.20, 0.02, 0.04),
        makeBlock("sk-ABCDEFGHIJKLMNOPQRSTUVWXYZ12", 0.26, 0.20, 0.34, 0.04)
    };

    const QVector<QRect> regions = CredentialDetector::detect(blocks, imageSize);
    QCOMPARE(regions.size(), 1);

    const QRect keyRect = expectedPixelRect(blocks[0].boundingRect, imageSize);
    const QRect valueRect = expectedPixelRect(blocks[2].boundingRect, imageSize);

    QVERIFY(containsRectCenter(regions, valueRect));
    QVERIFY(!containsRectCenter(regions, keyRect));
}

void tst_CredentialDetector::testDetect_SplitApiKeyPhrase_MarksValue()
{
    const QSize imageSize(1200, 700);
    const QVector<OCRTextBlock> blocks{
        makeBlock("nookal", 0.08, 0.24, 0.12, 0.04),
        makeBlock("api", 0.22, 0.24, 0.06, 0.04),
        makeBlock("key:", 0.29, 0.24, 0.09, 0.04),
        makeBlock("49bbC5Ad-6d17-C955-E3f9-E650f9F1234122", 0.40, 0.24, 0.48, 0.04)
    };

    const QVector<QRect> regions = CredentialDetector::detect(blocks, imageSize);
    QCOMPARE(regions.size(), 1);

    const QRect apiRect = expectedPixelRect(blocks[1].boundingRect, imageSize);
    const QRect valueRect = expectedPixelRect(blocks[3].boundingRect, imageSize);
    QVERIFY(containsRectCenter(regions, valueRect));
    QVERIFY(!containsRectCenter(regions, apiRect));
}

void tst_CredentialDetector::testDetect_SplitApiDashKeyPhrase_MarksValue()
{
    const QSize imageSize(1200, 700);
    const QVector<OCRTextBlock> blocks{
        makeBlock("api", 0.20, 0.24, 0.06, 0.04),
        makeBlock("-", 0.27, 0.24, 0.02, 0.04),
        makeBlock("key", 0.30, 0.24, 0.08, 0.04),
        makeBlock(":", 0.39, 0.24, 0.02, 0.04),
        makeBlock("49bbC5Ad-6d17-C955-E3f9-E650f9F1234122", 0.43, 0.24, 0.44, 0.04)
    };

    const QVector<QRect> regions = CredentialDetector::detect(blocks, imageSize);
    QCOMPARE(regions.size(), 1);

    const QRect keyRect = expectedPixelRect(blocks[2].boundingRect, imageSize);
    const QRect valueRect = expectedPixelRect(blocks[4].boundingRect, imageSize);
    QVERIFY(containsRectCenter(regions, valueRect));
    QVERIFY(!containsRectCenter(regions, keyRect));
}

void tst_CredentialDetector::testDetect_InlineApiKeyLabel_MarksValue()
{
    const QSize imageSize(1200, 700);
    const QVector<OCRTextBlock> blocks{
        makeBlock("nookal api key:", 0.08, 0.22, 0.28, 0.04),
        makeBlock("49bbC5Ad-6d17-C955-E3f9-E650f9F6465B", 0.38, 0.22, 0.48, 0.04)
    };

    const QVector<QRect> regions = CredentialDetector::detect(blocks, imageSize);
    QCOMPARE(regions.size(), 1);

    const QRect labelRect = expectedPixelRect(blocks[0].boundingRect, imageSize);
    const QRect valueRect = expectedPixelRect(blocks[1].boundingRect, imageSize);
    QVERIFY(containsRectCenter(regions, valueRect));
    QVERIFY(!containsRectCenter(regions, labelRect));
}

void tst_CredentialDetector::testDetect_EmbeddedInlineKeyValue_MarksValueSubRectOnly()
{
    const QSize imageSize(1200, 700);
    const QVector<OCRTextBlock> blocks{
        makeBlock("cliniko api key: MS0xODkxODQ0NTg2NDMxNTkwMDcxLTY5NmE=", 0.08, 0.30, 0.76, 0.05)
    };

    const QVector<QRect> regions = CredentialDetector::detect(blocks, imageSize);
    QVERIFY(!regions.isEmpty());

    const QRect tokenRect = expectedPixelRect(blocks[0].boundingRect, imageSize);
    const int valueStartIndex = blocks[0].text.indexOf(QStringLiteral("MS0x"));
    QVERIFY(valueStartIndex > 0);

    const QPoint labelPoint(tokenRect.left() + tokenRect.width() / 6, tokenRect.center().y());
    const int valueStartX = tokenRect.left()
        + qFloor((static_cast<qreal>(valueStartIndex) / static_cast<qreal>(blocks[0].text.size()))
                 * tokenRect.width());
    const QPoint valueStartPoint(valueStartX + 2, tokenRect.center().y());
    const QPoint valuePoint(tokenRect.left() + (tokenRect.width() * 3) / 4, tokenRect.center().y());

    QVERIFY(!containsPoint(regions, labelPoint));
    QVERIFY(containsPoint(regions, valueStartPoint));
    QVERIFY(containsPoint(regions, valuePoint));
}

void tst_CredentialDetector::testDetect_UuidSegmentsAfterApiKey_MarksAllSegments()
{
    const QSize imageSize(1400, 800);
    const QVector<OCRTextBlock> blocks{
        makeBlock("api", 0.06, 0.28, 0.05, 0.04),
        makeBlock("key:", 0.12, 0.28, 0.07, 0.04),
        makeBlock("49bbC5Ad", 0.22, 0.28, 0.10, 0.04),
        makeBlock("-", 0.33, 0.28, 0.01, 0.04),
        makeBlock("6d17", 0.35, 0.28, 0.05, 0.04),
        makeBlock("-", 0.41, 0.28, 0.01, 0.04),
        makeBlock("C955", 0.43, 0.28, 0.05, 0.04),
        makeBlock("-", 0.49, 0.28, 0.01, 0.04),
        makeBlock("E3f9", 0.51, 0.28, 0.05, 0.04),
        makeBlock("-", 0.57, 0.28, 0.01, 0.04),
        makeBlock("E650f9F6465B", 0.59, 0.28, 0.12, 0.04)
    };

    const QVector<QRect> regions = CredentialDetector::detect(blocks, imageSize);
    QVERIFY(!regions.isEmpty());

    const QRect firstSegmentRect = expectedPixelRect(blocks[2].boundingRect, imageSize);
    const QRect lastSegmentRect = expectedPixelRect(blocks[10].boundingRect, imageSize);
    QVERIFY(containsRectCenter(regions, firstSegmentRect));
    QVERIFY(containsRectCenter(regions, lastSegmentRect));
}

void tst_CredentialDetector::testDetect_PasswordColon_MarksValue()
{
    const QSize imageSize(1000, 600);
    const QVector<OCRTextBlock> blocks{
        makeBlock("password", 0.12, 0.28, 0.12, 0.04),
        makeBlock(":", 0.26, 0.28, 0.02, 0.04),
        makeBlock("hunter2", 0.30, 0.28, 0.10, 0.04)
    };

    const QVector<QRect> regions = CredentialDetector::detect(blocks, imageSize);
    QCOMPARE(regions.size(), 1);

    const QRect valueRect = expectedPixelRect(blocks[2].boundingRect, imageSize);
    QVERIFY(containsRectCenter(regions, valueRect));
}

void tst_CredentialDetector::testDetect_DirectPatternsWithoutKeyword()
{
    const QSize imageSize(1400, 800);
    const QVector<OCRTextBlock> blocks{
        makeBlock("AKIA1234567890ABCDEF", 0.10, 0.15, 0.22, 0.04),
        makeBlock("eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
                  "eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4iLCJpYXQiOjE1MTYyMzkwMjJ9."
                  "SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c",
                  0.10, 0.22, 0.76, 0.05)
    };

    const QVector<QRect> regions = CredentialDetector::detect(blocks, imageSize);
    QCOMPARE(regions.size(), 2);

    const QRect akiaRect = expectedPixelRect(blocks[0].boundingRect, imageSize);
    const QRect jwtRect = expectedPixelRect(blocks[1].boundingRect, imageSize);
    QVERIFY(containsRectCenter(regions, akiaRect));
    QVERIFY(containsRectCenter(regions, jwtRect));
}

void tst_CredentialDetector::testDetect_HyphenatedSkPatternWithoutKeyword()
{
    const QSize imageSize(1400, 800);
    const QVector<OCRTextBlock> blocks{
        makeBlock("sk-proj-4xYz1abcDEF-0123456789mnopqrstuv", 0.12, 0.18, 0.58, 0.05)
    };

    const QVector<QRect> regions = CredentialDetector::detect(blocks, imageSize);
    QCOMPARE(regions.size(), 1);

    const QRect skRect = expectedPixelRect(blocks[0].boundingRect, imageSize);
    QVERIFY(containsRectCenter(regions, skRect));
}

void tst_CredentialDetector::testDetect_GenericLongTokenRequiresKeyword()
{
    const QSize imageSize(1200, 700);

    const QVector<OCRTextBlock> withoutKeyword{
        makeBlock("abcdefghijklmnopqrstuvwx123456", 0.20, 0.34, 0.32, 0.04)
    };
    const QVector<QRect> regionsWithoutKeyword = CredentialDetector::detect(withoutKeyword, imageSize);
    QVERIFY(regionsWithoutKeyword.isEmpty());

    const QVector<OCRTextBlock> withKeyword{
        makeBlock("token", 0.08, 0.34, 0.10, 0.04),
        makeBlock("=", 0.19, 0.34, 0.02, 0.04),
        makeBlock("abcdefghijklmnopqrstuvwx123456", 0.23, 0.34, 0.32, 0.04)
    };
    const QVector<QRect> regionsWithKeyword = CredentialDetector::detect(withKeyword, imageSize);
    QCOMPARE(regionsWithKeyword.size(), 1);

    const QRect valueRect = expectedPixelRect(withKeyword[2].boundingRect, imageSize);
    QVERIFY(containsRectCenter(regionsWithKeyword, valueRect));
}

void tst_CredentialDetector::testDetect_KeywordWithoutAssignmentAndPlainValue_NotMarked()
{
    const QSize imageSize(1200, 700);
    const QVector<OCRTextBlock> blocks{
        makeBlock("access", 0.08, 0.36, 0.10, 0.04),
        makeBlock("token", 0.19, 0.36, 0.10, 0.04),
        makeBlock("expires", 0.31, 0.36, 0.12, 0.04),
        makeBlock("tomorrow", 0.45, 0.36, 0.13, 0.04)
    };

    const QVector<QRect> regions = CredentialDetector::detect(blocks, imageSize);
    QVERIFY(regions.isEmpty());
}

void tst_CredentialDetector::testDetect_KeywordWithDirectPatternWithoutAssignment_MarksValue()
{
    const QSize imageSize(1200, 700);
    const QVector<OCRTextBlock> blocks{
        makeBlock("token", 0.08, 0.36, 0.10, 0.04),
        makeBlock("sk-proj-4xYz1abcDEF-0123456789mnopqrstuv", 0.20, 0.36, 0.56, 0.04)
    };

    const QVector<QRect> regions = CredentialDetector::detect(blocks, imageSize);
    QCOMPARE(regions.size(), 1);

    const QRect valueRect = expectedPixelRect(blocks[1].boundingRect, imageSize);
    QVERIFY(containsRectCenter(regions, valueRect));
}

void tst_CredentialDetector::testDetect_KeywordWithValuePrefixedByAssignment_MarksValue()
{
    const QSize imageSize(1200, 700);
    const QVector<OCRTextBlock> blocks{
        makeBlock("password", 0.10, 0.36, 0.12, 0.04),
        makeBlock("=hunter2", 0.24, 0.36, 0.14, 0.04)
    };

    const QVector<QRect> regions = CredentialDetector::detect(blocks, imageSize);
    QCOMPARE(regions.size(), 1);

    const QRect valueRect = expectedPixelRect(blocks[1].boundingRect, imageSize);
    QVERIFY(containsRectCenter(regions, valueRect));
}

void tst_CredentialDetector::testDetect_SplitAdjacentFragments_MarksAllFragments()
{
    const QSize imageSize(1200, 700);
    const QVector<OCRTextBlock> blocks{
        makeBlock("token", 0.08, 0.37, 0.10, 0.04),
        makeBlock("=", 0.19, 0.37, 0.02, 0.04),
        makeBlock("abcdefghijklmnop", 0.23, 0.37, 0.14, 0.04),
        makeBlock("qrstuvwxyz123456", 0.38, 0.37, 0.14, 0.04)
    };

    const QVector<QRect> regions = CredentialDetector::detect(blocks, imageSize);
    QVERIFY(!regions.isEmpty());

    const QRect firstFragmentRect = expectedPixelRect(blocks[2].boundingRect, imageSize);
    const QRect secondFragmentRect = expectedPixelRect(blocks[3].boundingRect, imageSize);
    QVERIFY(containsRectCenter(regions, firstFragmentRect));
    QVERIFY(containsRectCenter(regions, secondFragmentRect));
}

void tst_CredentialDetector::testDetect_AdjacentPlainWordsAfterValue_NotMarked()
{
    const QSize imageSize(1200, 700);
    const QVector<OCRTextBlock> blocks{
        makeBlock("token", 0.08, 0.37, 0.10, 0.04),
        makeBlock("=", 0.19, 0.37, 0.02, 0.04),
        makeBlock("abcDEF1234567890", 0.23, 0.37, 0.14, 0.04),
        makeBlock("expires", 0.38, 0.37, 0.09, 0.04),
        makeBlock("tomorrow", 0.48, 0.37, 0.11, 0.04)
    };

    const QVector<QRect> regions = CredentialDetector::detect(blocks, imageSize);
    QVERIFY(!regions.isEmpty());

    const QRect valueRect = expectedPixelRect(blocks[2].boundingRect, imageSize);
    const QRect expiresRect = expectedPixelRect(blocks[3].boundingRect, imageSize);
    const QRect tomorrowRect = expectedPixelRect(blocks[4].boundingRect, imageSize);
    QVERIFY(containsRectCenter(regions, valueRect));
    QVERIFY(!containsRectCenter(regions, expiresRect));
    QVERIFY(!containsRectCenter(regions, tomorrowRect));
}

void tst_CredentialDetector::testDetect_CommaDelimitedPlainWordsAfterValue_NotMarked()
{
    const QSize imageSize(1200, 700);
    const QVector<OCRTextBlock> blocks{
        makeBlock("token", 0.08, 0.37, 0.10, 0.04),
        makeBlock("=", 0.19, 0.37, 0.02, 0.04),
        makeBlock("abcDEF1234567890", 0.23, 0.37, 0.14, 0.04),
        makeBlock(",", 0.38, 0.37, 0.01, 0.04),
        makeBlock("expires", 0.40, 0.37, 0.09, 0.04),
        makeBlock("tomorrow", 0.50, 0.37, 0.11, 0.04)
    };

    const QVector<QRect> regions = CredentialDetector::detect(blocks, imageSize);
    QVERIFY(!regions.isEmpty());

    const QRect valueRect = expectedPixelRect(blocks[2].boundingRect, imageSize);
    const QRect expiresRect = expectedPixelRect(blocks[4].boundingRect, imageSize);
    const QRect tomorrowRect = expectedPixelRect(blocks[5].boundingRect, imageSize);
    QVERIFY(containsRectCenter(regions, valueRect));
    QVERIFY(!containsRectCenter(regions, expiresRect));
    QVERIFY(!containsRectCenter(regions, tomorrowRect));
}

void tst_CredentialDetector::testDetect_MergeOverlappingRegions()
{
    const QSize imageSize(1200, 700);
    const QVector<OCRTextBlock> blocks{
        makeBlock("token", 0.08, 0.40, 0.10, 0.04),
        makeBlock("=", 0.19, 0.40, 0.02, 0.04),
        makeBlock("sk-ABCDEFGHIJKLMNOPQRSTUVWXYZ12", 0.23, 0.40, 0.22, 0.04),
        makeBlock("sk-ABCDEFGHIJKLMNOPQRSTUVWXYZ34", 0.42, 0.40, 0.22, 0.04)
    };

    const QVector<QRect> regions = CredentialDetector::detect(blocks, imageSize);
    QCOMPARE(regions.size(), 1);

    const QRect firstValueRect = expectedPixelRect(blocks[2].boundingRect, imageSize);
    const QRect secondValueRect = expectedPixelRect(blocks[3].boundingRect, imageSize);
    QVERIFY(regions[0].contains(firstValueRect.center()));
    QVERIFY(regions[0].contains(secondValueRect.center()));
}

QTEST_MAIN(tst_CredentialDetector)
#include "tst_CredentialDetector.moc"
