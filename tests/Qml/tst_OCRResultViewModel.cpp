#include <QtTest>
#include <QThreadPool>
#include <QPromise>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QtQml/qqmlextensionplugin.h>
#include <stdexcept>
#include "qml/OCRResultViewModel.h"
#include "detection/OCRTypes.h"
#include "detection/TableDetector.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

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
    void workerExceptionKeepsText();
    void newerTableWins();
    void buttonsStayInPlace();
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
    QPointer<OCRResultViewModel> guard(model);
    QSignalSpy updates(model, &OCRResultViewModel::resultSet);
    model->setOCRResult(tableResult());
    delete model;
    QVERIFY(guard.isNull());
    QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
    QCoreApplication::processEvents();
    QCOMPARE(updates.count(), 1);
}

void TestOCRResultViewModel::workerExceptionKeepsText()
{
    OCRResultViewModel model;
    OCRResult result;
    result.text = QStringLiteral("Still usable");
    model.setOCRResult(result);
    QPromise<TableDetectionResult> promise;
    promise.start();
    promise.setException(std::make_exception_ptr(std::runtime_error("test failure")));
    promise.finish();
    bool threw = false;
    QTest::ignoreMessage(QtWarningMsg, "OCR table detection failed; plain text remains available");
    try {
        model.applyTableDetectionResult(promise.future(), model.m_resultGeneration);
    } catch (...) {
        threw = true;
    }
    QVERIFY(!threw);
    QCOMPARE(model.text(), result.text);
    QVERIFY(!model.hasTsv());
}

void TestOCRResultViewModel::newerTableWins()
{
    OCRResultViewModel model;
    QSignalSpy updates(&model, &OCRResultViewModel::resultSet);
    model.setOCRResult(tableResult());
    auto next = tableResult();
    next.blocks[2].text = QStringLiteral("Carol");
    next.text = QStringLiteral("New table");
    model.setOCRResult(next);
    QTRY_COMPARE(updates.count(), 3);
    QCOMPARE(model.m_detectedTsv, TableDetector::detect(next.blocks).tsv);
    QCOMPARE(model.text(), next.text);
}

void TestOCRResultViewModel::buttonsStayInPlace()
{
    OCRResultViewModel model;
    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/SnapTrayQml/dialogs/OCRResultDialog.qml")));
    std::unique_ptr<QObject> root(component.createWithInitialProperties(
        {{QStringLiteral("viewModel"), QVariant::fromValue(&model)}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    QQuickItem* copy = nullptr;
    for (auto* item : root->findChildren<QQuickItem*>()) {
        if (item->property("text").toString() == QStringLiteral("Copy")
            && item->property("style").isValid()) {
            copy = item;
            break;
        }
    }
    QVERIFY(copy);
    QCoreApplication::processEvents();
    const QPointF position = copy->mapToScene(QPointF());
    const qreal width = copy->width();
    model.setOCRResult(tableResult());
    QTRY_VERIFY(model.hasTsv());
    QCoreApplication::processEvents();
    QCOMPARE(copy->width(), width);
    QCOMPARE(copy->mapToScene(QPointF()), position);
}

QTEST_MAIN(TestOCRResultViewModel)
#include "tst_OCRResultViewModel.moc"
