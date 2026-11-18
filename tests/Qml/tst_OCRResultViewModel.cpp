#include <QtTest>
#include <QThreadPool>
#include <QPromise>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QtQml/qqmlextensionplugin.h>
#include <QPainter>
#include <QPixmap>
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

// Three square "glyphs" stacked in one column: accepted by the normalizer.
QPixmap verticalColumnPixmap()
{
    QImage image(80, 220, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    for (int i = 0; i < 3; ++i) {
        painter.fillRect(QRect(15, 20 + i * 62, 50, 50), Qt::black);
    }
    painter.end();
    return QPixmap::fromImage(image);
}

QPixmap horizontalPixmap()
{
    QImage image(400, 60, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.fillRect(QRect(20, 20, 360, 20), Qt::black);
    painter.end();
    return QPixmap::fromImage(image);
}

struct FakeRecognizer {
    int calls = 0;
    QImage lastImage;
    OCRResultViewModel::OCRResultCallback pending;
};

OCRResultViewModel::Recognizer deferredRecognizer(FakeRecognizer* fake)
{
    return [fake](const QImage& image, const OCRResultViewModel::OCRResultCallback& done) {
        ++fake->calls;
        fake->lastImage = image;
        fake->pending = done;
    };
}

OCRResult textResult(const QString& text)
{
    OCRResult result;
    result.success = !text.isEmpty();
    result.text = text;
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
    void verticalRetryReplacesTextAndRestoresOriginal();
    void verticalRetryKeepsEditsPerView();
    void verticalRetryRejectsOutOfScopeImage();
    void verticalRetryFailureKeepsOriginal();
    void verticalRetryIgnoredAfterClose();
    void verticalRetryIgnoredAfterNewResult();
    void verticalRetryCancel();
    void verticalRetryButtonOnlyWhenAvailable();
    void copyDisabledWhileTextEmpty();
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

void TestOCRResultViewModel::verticalRetryReplacesTextAndRestoresOriginal()
{
    OCRResultViewModel model;
    model.setOCRResult(tableResult());
    QTRY_VERIFY(model.hasTsv());
    QVERIFY(!model.verticalRetryAvailable());
    FakeRecognizer fake;
    model.setVerticalRetrySource(verticalColumnPixmap(), deferredRecognizer(&fake));
    QVERIFY(model.verticalRetryAvailable());

    model.retryAsVerticalText();
    QVERIFY(model.verticalRetryInProgress());
    QTRY_COMPARE(fake.calls, 1);
    // The engine receives the re-laid horizontal image, not the source.
    QVERIFY(fake.lastImage.width() > fake.lastImage.height());
    fake.pending(textResult(QStringLiteral("直排文字")));

    QVERIFY(!model.verticalRetryInProgress());
    QVERIFY(model.showingVerticalResult());
    QCOMPARE(model.text(), QStringLiteral("直排文字"));
    QVERIFY(!model.isEdited());
    // TSV comes from the original geometry and is not offered for re-laid text.
    QVERIFY(!model.hasTsv());

    model.showOriginalResult();
    QVERIFY(!model.showingVerticalResult());
    QCOMPARE(model.text(), tableResult().text);
    QVERIFY(model.hasTsv());

    // Switching back reuses the earlier result instead of re-running OCR.
    model.retryAsVerticalText();
    QVERIFY(model.showingVerticalResult());
    QCOMPARE(model.text(), QStringLiteral("直排文字"));
    QCOMPARE(fake.calls, 1);
}

void TestOCRResultViewModel::verticalRetryKeepsEditsPerView()
{
    OCRResultViewModel model;
    model.setOCRResult(textResult(QStringLiteral("garbled")));
    FakeRecognizer fake;
    model.setVerticalRetrySource(verticalColumnPixmap(), deferredRecognizer(&fake));
    model.setText(QStringLiteral("garbled, edited"));
    model.retryAsVerticalText();
    QTRY_COMPARE(fake.calls, 1);
    fake.pending(textResult(QStringLiteral("直排")));
    model.setText(QStringLiteral("直排，修正"));
    QVERIFY(model.isEdited());
    model.showOriginalResult();
    QCOMPARE(model.text(), QStringLiteral("garbled, edited"));
    QVERIFY(model.isEdited());
    model.retryAsVerticalText();
    QCOMPARE(model.text(), QStringLiteral("直排，修正"));
}

void TestOCRResultViewModel::verticalRetryRejectsOutOfScopeImage()
{
    OCRResultViewModel model;
    model.setOCRResult(textResult(QStringLiteral("horizontal text")));
    FakeRecognizer fake;
    model.setVerticalRetrySource(horizontalPixmap(), deferredRecognizer(&fake));
    model.retryAsVerticalText();
    QTRY_VERIFY(!model.verticalRetryInProgress());
    QCOMPARE(fake.calls, 0);
    QVERIFY(!model.showingVerticalResult());
    QCOMPARE(model.text(), QStringLiteral("horizontal text"));
    QCOMPARE(model.verticalStatusText(), QStringLiteral("Not a single column of vertical text"));
}

void TestOCRResultViewModel::verticalRetryFailureKeepsOriginal()
{
    OCRResultViewModel model;
    model.setOCRResult(textResult(QStringLiteral("original")));
    model.setText(QStringLiteral("original, edited"));
    FakeRecognizer fake;
    model.setVerticalRetrySource(verticalColumnPixmap(), deferredRecognizer(&fake));
    model.retryAsVerticalText();
    QTRY_COMPARE(fake.calls, 1);
    OCRResult failed;
    failed.error = QStringLiteral("No text detected");
    fake.pending(failed);
    QVERIFY(!model.verticalRetryInProgress());
    QVERIFY(!model.showingVerticalResult());
    QCOMPARE(model.text(), QStringLiteral("original, edited"));
    QCOMPARE(model.verticalStatusText(), QStringLiteral("Vertical retry found no text"));
}

void TestOCRResultViewModel::verticalRetryIgnoredAfterClose()
{
    OCRResultViewModel model;
    model.setOCRResult(textResult(QStringLiteral("original")));
    FakeRecognizer fake;
    model.setVerticalRetrySource(verticalColumnPixmap(), deferredRecognizer(&fake));
    model.retryAsVerticalText();
    QTRY_COMPARE(fake.calls, 1);
    model.close();
    fake.pending(textResult(QStringLiteral("late")));
    QVERIFY(!model.showingVerticalResult());
    QCOMPARE(model.text(), QStringLiteral("original"));

    // Closing while preprocessing is still queued drops that result as well.
    model.retryAsVerticalText();
    model.close();
    QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
    QCoreApplication::processEvents();
    QCOMPARE(fake.calls, 1);
    QVERIFY(!model.verticalRetryInProgress());
}

void TestOCRResultViewModel::verticalRetryIgnoredAfterNewResult()
{
    OCRResultViewModel model;
    model.setOCRResult(textResult(QStringLiteral("first image")));
    FakeRecognizer fake;
    model.setVerticalRetrySource(verticalColumnPixmap(), deferredRecognizer(&fake));
    model.retryAsVerticalText();
    QTRY_COMPARE(fake.calls, 1);
    model.setOCRResult(textResult(QStringLiteral("second image")));
    QVERIFY(!model.verticalRetryAvailable());
    fake.pending(textResult(QStringLiteral("late result for first image")));
    QVERIFY(!model.showingVerticalResult());
    QCOMPARE(model.text(), QStringLiteral("second image"));
}

void TestOCRResultViewModel::verticalRetryCancel()
{
    OCRResultViewModel model;
    model.setOCRResult(textResult(QStringLiteral("original")));
    FakeRecognizer fake;
    model.setVerticalRetrySource(verticalColumnPixmap(), deferredRecognizer(&fake));
    model.retryAsVerticalText();
    QTRY_COMPARE(fake.calls, 1);
    model.cancelVerticalRetry();
    QVERIFY(!model.verticalRetryInProgress());
    fake.pending(textResult(QStringLiteral("cancelled")));
    QVERIFY(!model.showingVerticalResult());
    QCOMPARE(model.text(), QStringLiteral("original"));

    // A cancelled retry can be started again.
    model.retryAsVerticalText();
    QTRY_COMPARE(fake.calls, 2);
    fake.pending(textResult(QStringLiteral("直排")));
    QVERIFY(model.showingVerticalResult());
}

void TestOCRResultViewModel::verticalRetryButtonOnlyWhenAvailable()
{
    OCRResultViewModel model;
    model.setOCRResult(textResult(QStringLiteral("original")));
    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/SnapTrayQml/dialogs/OCRResultDialog.qml")));
    std::unique_ptr<QObject> root(component.createWithInitialProperties(
        {{QStringLiteral("viewModel"), QVariant::fromValue(&model)}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    QCoreApplication::processEvents();
    auto* button = root->findChild<QQuickItem*>(QStringLiteral("verticalRetryButton"));
    QVERIFY(button);
    QVERIFY(!button->isVisible());

    FakeRecognizer fake;
    model.setVerticalRetrySource(verticalColumnPixmap(), deferredRecognizer(&fake));
    QTRY_VERIFY(button->isVisible());
    QCOMPARE(button->property("text").toString(), QStringLiteral("Retry as Vertical Text"));
    QMetaObject::invokeMethod(button, "clicked");
    QCOMPARE(button->property("text").toString(), QStringLiteral("Cancel"));
    QTRY_COMPARE(fake.calls, 1);
    fake.pending(textResult(QStringLiteral("直排")));
    QCOMPARE(button->property("text").toString(), QStringLiteral("Show Original"));
}

void TestOCRResultViewModel::copyDisabledWhileTextEmpty()
{
    OCRResultViewModel model;
    model.setOCRResult(OCRResult{}); // A "no text" result opens the editor empty
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
    QVERIFY(!copy->isEnabled());

    // A retry result (or a manual edit) supplies text and re-enables Copy.
    FakeRecognizer fake;
    model.setVerticalRetrySource(verticalColumnPixmap(), deferredRecognizer(&fake));
    model.retryAsVerticalText();
    QTRY_COMPARE(fake.calls, 1);
    fake.pending(textResult(QStringLiteral("直排")));
    QVERIFY(copy->isEnabled());
    model.setText(QString());
    QVERIFY(!copy->isEnabled());
}

QTEST_MAIN(TestOCRResultViewModel)
#include "tst_OCRResultViewModel.moc"
