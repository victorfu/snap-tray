#include <QtTest>

#include "detection/TableDetector.h"

namespace {

OCRTextBlock makeBlock(const QString &text, qreal x, qreal y, qreal width = 0.08, qreal height = 0.03)
{
    OCRTextBlock block;
    block.text = text;
    block.boundingRect = QRectF(x, y, width, height);
    block.confidence = 0.9f;
    return block;
}

} // namespace

class tst_TableDetector : public QObject
{
    Q_OBJECT

private slots:
    void testDetect_EmptyInput();
    void testDetect_SingleRow_NotTable();
    void testDetect_RegularGrid_TableDetected();
    void testDetect_MultiWordCells_KeepColumns();
    void testDetect_RaggedRows_TableDetectedWithEmptyCell();
    void testDetect_Paragraph_NotTable();
    void testDetect_TerminalAlignedOutput_TableDetected();
};

void tst_TableDetector::testDetect_EmptyInput()
{
    const TableDetectionResult result = TableDetector::detect({});
    QVERIFY(!result.isTable);
    QVERIFY(result.tsv.isEmpty());
    QCOMPARE(result.rowCount, 0);
    QCOMPARE(result.columnCount, 0);
}

void tst_TableDetector::testDetect_SingleRow_NotTable()
{
    QVector<OCRTextBlock> blocks{
        makeBlock("A", 0.10, 0.10),
        makeBlock("B", 0.30, 0.10),
        makeBlock("C", 0.55, 0.10),
    };

    const TableDetectionResult result = TableDetector::detect(blocks);
    QVERIFY(!result.isTable);
    QVERIFY(result.tsv.isEmpty());
}

void tst_TableDetector::testDetect_RegularGrid_TableDetected()
{
    QVector<OCRTextBlock> blocks{
        makeBlock("Name", 0.10, 0.10),
        makeBlock("Score", 0.315, 0.102),
        makeBlock("Rank", 0.57, 0.099),

        makeBlock("Alice", 0.102, 0.145),
        makeBlock("95", 0.318, 0.147),
        makeBlock("1", 0.572, 0.143),

        makeBlock("Bob", 0.098, 0.19),
        makeBlock("88", 0.314, 0.191),
        makeBlock("2", 0.568, 0.189),
    };

    const TableDetectionResult result = TableDetector::detect(blocks);
    QVERIFY(result.isTable);
    QCOMPARE(result.rowCount, 3);
    QCOMPARE(result.columnCount, 3);
    QCOMPARE(result.tsv, QStringLiteral("Name\tScore\tRank\nAlice\t95\t1\nBob\t88\t2"));
}

void tst_TableDetector::testDetect_MultiWordCells_KeepColumns()
{
    QVector<OCRTextBlock> blocks{
        makeBlock("Full", 0.10, 0.10),
        makeBlock("Name", 0.17, 0.10),
        makeBlock("Age", 0.52, 0.10),

        makeBlock("Alice", 0.10, 0.145),
        makeBlock("Smith", 0.185, 0.145),
        makeBlock("30", 0.52, 0.145),

        makeBlock("Bob", 0.10, 0.19),
        makeBlock("Jones", 0.17, 0.19),
        makeBlock("28", 0.52, 0.19),
    };

    const TableDetectionResult result = TableDetector::detect(blocks);
    QVERIFY(result.isTable);
    QCOMPARE(result.rowCount, 3);
    QCOMPARE(result.columnCount, 2);
    QCOMPARE(result.tsv, QStringLiteral("Full Name\tAge\nAlice Smith\t30\nBob Jones\t28"));
}

void tst_TableDetector::testDetect_RaggedRows_TableDetectedWithEmptyCell()
{
    QVector<OCRTextBlock> blocks{
        makeBlock("A", 0.10, 0.10),
        makeBlock("B", 0.31, 0.10),
        makeBlock("C", 0.52, 0.10),

        makeBlock("D", 0.101, 0.145),
        makeBlock("F", 0.521, 0.145),

        makeBlock("G", 0.099, 0.19),
        makeBlock("H", 0.309, 0.19),
        makeBlock("I", 0.519, 0.19),
    };

    const TableDetectionResult result = TableDetector::detect(blocks);
    QVERIFY(result.isTable);
    QCOMPARE(result.rowCount, 3);
    QCOMPARE(result.columnCount, 3);

    const QStringList lines = result.tsv.split(QLatin1Char('\n'));
    QCOMPARE(lines.size(), 3);
    QCOMPARE(lines[1], QStringLiteral("D\t\tF"));
}

void tst_TableDetector::testDetect_Paragraph_NotTable()
{
    QVector<OCRTextBlock> blocks{
        makeBlock("This", 0.05, 0.10),
        makeBlock("is", 0.19, 0.10),
        makeBlock("text", 0.34, 0.10),

        makeBlock("with", 0.15, 0.145),
        makeBlock("varying", 0.30, 0.145),
        makeBlock("offsets", 0.46, 0.145),

        makeBlock("across", 0.25, 0.19),
        makeBlock("lines", 0.41, 0.19),
        makeBlock("only", 0.57, 0.19),
    };

    const TableDetectionResult result = TableDetector::detect(blocks);
    QVERIFY(!result.isTable);
    QVERIFY(result.tsv.isEmpty());
}

void tst_TableDetector::testDetect_TerminalAlignedOutput_TableDetected()
{
    QVector<OCRTextBlock> blocks{
        makeBlock("PID", 0.08, 0.10),
        makeBlock("CPU%", 0.30, 0.10),
        makeBlock("MEM", 0.47, 0.10),

        makeBlock("123", 0.081, 0.145),
        makeBlock("5", 0.301, 0.145),
        makeBlock("120M", 0.468, 0.145),

        makeBlock("42", 0.079, 0.19),
        makeBlock("17", 0.302, 0.19),
        makeBlock("980M", 0.471, 0.19),
    };

    const TableDetectionResult result = TableDetector::detect(blocks);
    QVERIFY(result.isTable);
    QCOMPARE(result.rowCount, 3);
    QCOMPARE(result.columnCount, 3);
    QCOMPARE(result.tsv, QStringLiteral("PID\tCPU%\tMEM\n123\t5\t120M\n42\t17\t980M"));
}

QTEST_MAIN(tst_TableDetector)
#include "tst_TableDetector.moc"
