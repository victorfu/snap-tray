#include <QtTest/QtTest>

#include <QQuickItem>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

class FakeWidthViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int currentWidth READ currentWidth WRITE setCurrentWidth NOTIFY currentWidthChanged)
    Q_PROPERTY(int minWidth READ minWidth CONSTANT)
    Q_PROPERTY(int maxWidth READ maxWidth CONSTANT)

public:
    int currentWidth() const { return m_currentWidth; }
    void setCurrentWidth(int width)
    {
        if (m_currentWidth == width) {
            return;
        }

        m_currentWidth = width;
        emit currentWidthChanged();
    }

    int minWidth() const { return 1; }
    int maxWidth() const { return 30; }

public slots:
    void handleWidthChanged(int width)
    {
        setCurrentWidth(qBound(minWidth(), width, maxWidth()));
    }

signals:
    void currentWidthChanged();

private:
    int m_currentWidth = 1;
};

class tst_WidthSectionQml : public QObject
{
    Q_OBJECT

private slots:
    void testPreviewDotCenterStaysFixedAcrossWidthChanges();
    void testStepperButtonsChangeWidthByOne();
    void testStepperDimsAtBounds();
};

void tst_WidthSectionQml::testPreviewDotCenterStaysFixedAcrossWidthChanges()
{
    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/SnapTrayQml/toolbar/WidthSection.qml")));
    if (component.status() != QQmlComponent::Ready) {
        const auto errors = component.errors();
        QStringList messages;
        for (const auto& error : errors) {
            messages.append(error.toString());
        }
        QFAIL(qPrintable(messages.join('\n')));
    }

    FakeWidthViewModel viewModel;
    const QScopedPointer<QObject> created(component.create());
    auto* rootItem = qobject_cast<QQuickItem*>(created.get());
    QVERIFY(rootItem);
    QVERIFY(rootItem->setProperty("viewModel", QVariant::fromValue(static_cast<QObject*>(&viewModel))));

    auto* previewContainer =
        rootItem->findChild<QQuickItem*>(QStringLiteral("widthPreviewContainer"));
    QVERIFY(previewContainer);

    auto* previewDot =
        rootItem->findChild<QQuickItem*>(QStringLiteral("widthPreviewDot"));
    QVERIFY(previewDot);

    const QPointF expectedCenter = previewContainer->mapToScene(
        QPointF(previewContainer->width() / 2.0, previewContainer->height() / 2.0));

    const QList<int> widths = {1, 2, 3, 15, 29, 30};
    for (int width : widths) {
        viewModel.setCurrentWidth(width);
        QCoreApplication::processEvents();

        const QPointF containerCenter = previewContainer->mapToScene(
            QPointF(previewContainer->width() / 2.0, previewContainer->height() / 2.0));
        const QPointF dotCenter = previewDot->mapToScene(
            QPointF(previewDot->width() / 2.0, previewDot->height() / 2.0));

        QCOMPARE(containerCenter, expectedCenter);
        QCOMPARE(dotCenter, expectedCenter);
    }
}

void tst_WidthSectionQml::testStepperButtonsChangeWidthByOne()
{
    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/SnapTrayQml/toolbar/WidthSection.qml")));
    QVERIFY2(component.status() == QQmlComponent::Ready, qPrintable(component.errorString()));

    FakeWidthViewModel viewModel;
    viewModel.setCurrentWidth(10);
    const QScopedPointer<QObject> created(component.create());
    auto* rootItem = qobject_cast<QQuickItem*>(created.get());
    QVERIFY(rootItem);
    QVERIFY(rootItem->setProperty("viewModel", QVariant::fromValue(static_cast<QObject*>(&viewModel))));
    QCOMPARE(rootItem->implicitWidth(), 48.0);

    auto* stepUp = rootItem->findChild<QQuickItem*>(QStringLiteral("widthStepUp"));
    auto* stepDown = rootItem->findChild<QQuickItem*>(QStringLiteral("widthStepDown"));
    auto* valueLabel = rootItem->findChild<QQuickItem*>(QStringLiteral("widthValueLabel"));
    auto* contentRow = rootItem->findChild<QQuickItem*>(QStringLiteral("widthContentRow"));
    QVERIFY(stepUp);
    QVERIFY(stepDown);
    QVERIFY(valueLabel);
    QVERIFY(contentRow);
    QVERIFY(contentRow->x() >= 0.0);
    QVERIFY(contentRow->x() + contentRow->width() <= rootItem->width());
    QCOMPARE(valueLabel->property("text").toString(), QStringLiteral("10"));

    QMetaObject::invokeMethod(stepUp, "activate");
    QCOMPARE(viewModel.currentWidth(), 11);
    QCOMPARE(valueLabel->property("text").toString(), QStringLiteral("11"));

    QMetaObject::invokeMethod(stepDown, "activate");
    QMetaObject::invokeMethod(stepDown, "activate");
    QCOMPARE(viewModel.currentWidth(), 9);
    QCOMPARE(valueLabel->property("text").toString(), QStringLiteral("9"));
}

void tst_WidthSectionQml::testStepperDimsAtBounds()
{
    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/SnapTrayQml/toolbar/WidthSection.qml")));
    QVERIFY2(component.status() == QQmlComponent::Ready, qPrintable(component.errorString()));

    FakeWidthViewModel viewModel;
    const QScopedPointer<QObject> created(component.create());
    auto* rootItem = qobject_cast<QQuickItem*>(created.get());
    QVERIFY(rootItem);
    QVERIFY(rootItem->setProperty("viewModel", QVariant::fromValue(static_cast<QObject*>(&viewModel))));

    auto* stepUp = rootItem->findChild<QQuickItem*>(QStringLiteral("widthStepUp"));
    auto* stepDown = rootItem->findChild<QQuickItem*>(QStringLiteral("widthStepDown"));
    QVERIFY(stepUp);
    QVERIFY(stepDown);

    viewModel.setCurrentWidth(1);
    QCoreApplication::processEvents();
    QVERIFY(!stepDown->property("stepEnabled").toBool());
    QVERIFY(stepUp->property("stepEnabled").toBool());

    QMetaObject::invokeMethod(stepDown, "activate");
    QCOMPARE(viewModel.currentWidth(), 1);

    viewModel.setCurrentWidth(30);
    QCoreApplication::processEvents();
    QVERIFY(!stepUp->property("stepEnabled").toBool());
    QVERIFY(stepDown->property("stepEnabled").toBool());

    QMetaObject::invokeMethod(stepUp, "activate");
    QCOMPARE(viewModel.currentWidth(), 30);
}

QTEST_MAIN(tst_WidthSectionQml)
#include "tst_WidthSectionQml.moc"
