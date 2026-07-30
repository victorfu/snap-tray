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

    const QPointF expectedCenter =
        rootItem->mapToScene(QPointF(rootItem->width() / 2.0, rootItem->height() / 2.0));

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

QTEST_MAIN(tst_WidthSectionQml)
#include "tst_WidthSectionQml.moc"
