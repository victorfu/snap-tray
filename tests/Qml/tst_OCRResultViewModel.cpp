#include <QtTest>
#include <QThreadPool>
#include "qml/OCRResultViewModel.h"
#include "detection/OCRTypes.h"

namespace {
OCRResult tableResult()
{
    OCRResult result;
    result.success = true;
    result.text = QStringLiteral("Name Age\nAlice 30\nBob 28");
    const QStringList texts{QStringLiteral("Name"), QStringLiteral("Age"),
                            QStringLiteral("Alice"), QStringLiteral("30"),
                            QStringLiteral("Bob"), QStringLiteral("28")};
    for (int i = 0; i < texts.size(); ++i) {
        OCRTextBlock block;
        block.text = texts[i];
        block.boundingRect = QRectF(0.1 + (i % 2) * 0.4, 0.1 + (i / 2) * 0.1, 0.08, 0.03);
        result.blocks.append(block);
    }
    return result;
}
}

class TestOCRResultViewModel : public QObject
{
    Q_OBJECT
private slots:
    void textAvailableBeforeAnalysis();
    void staleAnalysisIgnored();
    void destroyedBeforeAnalysisCompletes();
};

void TestOCRResultViewModel::textAvailableBeforeAnalysis()
{
    OCRResultViewModel model;
    QSignalSpy updates(&model, &OCRResultViewModel::resultSet);
    model.setOCRResult(tableResult());
    QCOMPARE(model.text(), tableResult().text);
    QCOMPARE(updates.count(), 1);
    QVERIFY(!model.hasTsv());
    model.setText(QStringLiteral("Edited while analysis runs"));
    QTRY_VERIFY(model.hasTsv());
    QCOMPARE(updates.count(), 2);
    QCOMPARE(model.text(), QStringLiteral("Edited while analysis runs"));
    QVERIFY(model.isEdited());
}

void TestOCRResultViewModel::staleAnalysisIgnored()
{
    OCRResultViewModel model;
    QSignalSpy updates(&model, &OCRResultViewModel::resultSet);
    model.setOCRResult(tableResult());
    OCRResult replacement;
    replacement.text = QStringLiteral("New result");
    model.setOCRResult(replacement);
    // Finish background tasks, then deliver queued watcher notifications.
    QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
    QCoreApplication::processEvents();
    QCOMPARE(model.text(), replacement.text);
    QVERIFY(!model.hasTsv());
    QCOMPARE(updates.count(), 2);
}

void TestOCRResultViewModel::destroyedBeforeAnalysisCompletes()
{
    auto* model = new OCRResultViewModel;
    model->setOCRResult(tableResult());
    delete model;
    QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
    QCoreApplication::processEvents();
}

QTEST_MAIN(TestOCRResultViewModel)
#include "tst_OCRResultViewModel.moc"
