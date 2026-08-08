#include <QtTest/QtTest>

#include <QQuickItem>
#include <QQuickView>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QtQml/qqmlextensionplugin.h>

#include "qml/PinToolOptionsViewModel.h"

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
    void testPreviewRemainsCompactAndSupportsHintEmphasis();
    void testToolOptionsStripForwardsPreviewHoverAnchor();
    void testToolOptionsStripForwardsAutoBlurHoverAnchor();
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

void tst_WidthSectionQml::testPreviewRemainsCompactAndSupportsHintEmphasis()
{
    QQmlEngine engine;
    QQmlComponent component(
        &engine, QUrl(QStringLiteral("qrc:/SnapTrayQml/toolbar/WidthSection.qml")));
    if (component.status() != QQmlComponent::Ready) {
        const auto errors = component.errors();
        QStringList messages;
        for (const auto& error : errors)
            messages.append(error.toString());
        QFAIL(qPrintable(messages.join('\n')));
    }

    const QScopedPointer<QObject> created(component.create());
    auto* rootItem = qobject_cast<QQuickItem*>(created.get());
    QVERIFY(rootItem);
    QCOMPARE(rootItem->implicitWidth(), 28.0);
    QCOMPARE(rootItem->implicitHeight(), 28.0);
    QCOMPARE(rootItem->width(), 28.0);
    QCOMPARE(rootItem->height(), 28.0);

    auto* previewContainer =
        rootItem->findChild<QQuickItem*>(QStringLiteral("widthPreviewContainer"));
    QVERIFY(previewContainer);
    QCOMPARE(previewContainer->width(), 22.0);
    QCOMPARE(previewContainer->height(), 22.0);
    QCOMPARE(previewContainer->scale(), 1.0);

    QVERIFY(rootItem->setProperty("hintActive", true));
    QTRY_VERIFY(previewContainer->scale() > 1.0);

    QVERIFY(rootItem->setProperty("hintActive", false));
    QTRY_COMPARE(previewContainer->scale(), 1.0);
}

void tst_WidthSectionQml::testToolOptionsStripForwardsPreviewHoverAnchor()
{
    if (QGuiApplication::screens().isEmpty())
        QSKIP("Width preview hover test requires a screen");

    PinToolOptionsViewModel viewModel;
    viewModel.showForTool(static_cast<int>(ToolId::Mosaic));

    QQuickView view;
    view.rootContext()->setContextProperty(
        QStringLiteral("pinToolOptionsViewModel"), &viewModel);
    view.setResizeMode(QQuickView::SizeViewToRootObject);
    view.setSource(
        QUrl(QStringLiteral("qrc:/SnapTrayQml/toolbar/ToolOptionsStrip.qml")));

    if (view.status() != QQuickView::Ready) {
        const auto errors = view.errors();
        QStringList messages;
        for (const auto& error : errors)
            messages.append(error.toString());
        QFAIL(qPrintable(messages.join('\n')));
    }

    view.show();
    if (!QTest::qWaitForWindowExposed(&view))
        QSKIP("Width preview hover test window could not be exposed");

    auto* rootItem = view.rootObject();
    QVERIFY(rootItem);
    auto* previewContainer =
        rootItem->findChild<QQuickItem*>(QStringLiteral("widthPreviewContainer"));
    QVERIFY(previewContainer);

    QSignalSpy hoverSpy(
        rootItem, SIGNAL(mosaicBrushPreviewHovered(double,double,double,double)));
    QSignalSpy exitSpy(rootItem, SIGNAL(mosaicBrushPreviewHoverExited()));
    QVERIFY(hoverSpy.isValid());
    QVERIFY(exitSpy.isValid());

    QVERIFY(rootItem->setProperty("mosaicBrushHintActive", true));
    QTRY_VERIFY(previewContainer->scale() > 1.0);
    QVERIFY(rootItem->setProperty("mosaicBrushHintActive", false));

    const QRectF previewRect = previewContainer->mapRectToScene(
        QRectF(0.0, 0.0, previewContainer->width(), previewContainer->height()));
    QTest::mouseMove(&view, QPoint(view.width() - 1, view.height() - 1));
    QCoreApplication::processEvents();
    hoverSpy.clear();
    exitSpy.clear();

    QTest::mouseMove(&view, previewRect.center().toPoint());
    QTRY_COMPARE(hoverSpy.count(), 1);

    const auto arguments = hoverSpy.takeFirst();
    const QPointF expectedAnchor = previewContainer->mapToGlobal(QPointF(0.0, 0.0));
    QVERIFY(qAbs(arguments.at(0).toDouble() - expectedAnchor.x()) < 1.0);
    QVERIFY(qAbs(arguments.at(1).toDouble() - expectedAnchor.y()) < 1.0);
    QCOMPARE(arguments.at(2).toDouble(), 22.0);
    QCOMPARE(arguments.at(3).toDouble(), 22.0);

    QTest::mouseMove(&view, QPoint(view.width() - 1, view.height() - 1));
    QTRY_COMPARE(exitSpy.count(), 1);

    viewModel.showForTool(static_cast<int>(ToolId::Pencil));
    QCoreApplication::processEvents();
    hoverSpy.clear();
    exitSpy.clear();
    QTRY_COMPARE(previewContainer->scale(), 1.0);

    QTest::mouseMove(&view, previewRect.center().toPoint());
    QTest::qWait(150);
    QCOMPARE(hoverSpy.count(), 0);
    QCOMPARE(previewContainer->scale(), 1.0);
}

void tst_WidthSectionQml::testToolOptionsStripForwardsAutoBlurHoverAnchor()
{
    if (QGuiApplication::screens().isEmpty())
        QSKIP("Auto-blur hover test requires a screen");

    PinToolOptionsViewModel viewModel;
    viewModel.showForTool(static_cast<int>(ToolId::Mosaic));

    QQuickView view;
    view.rootContext()->setContextProperty(
        QStringLiteral("pinToolOptionsViewModel"), &viewModel);
    view.setResizeMode(QQuickView::SizeViewToRootObject);
    view.setSource(
        QUrl(QStringLiteral("qrc:/SnapTrayQml/toolbar/ToolOptionsStrip.qml")));

    if (view.status() != QQuickView::Ready) {
        const auto errors = view.errors();
        QStringList messages;
        for (const auto& error : errors)
            messages.append(error.toString());
        QFAIL(qPrintable(messages.join('\n')));
    }

    view.show();
    if (!QTest::qWaitForWindowExposed(&view))
        QSKIP("Auto-blur hover test window could not be exposed");

    auto* rootItem = view.rootObject();
    QVERIFY(rootItem);
    auto* autoBlurButton =
        rootItem->findChild<QQuickItem*>(QStringLiteral("autoBlurButton"));
    auto* previewContainer =
        rootItem->findChild<QQuickItem*>(QStringLiteral("widthPreviewContainer"));
    QVERIFY(autoBlurButton);
    QVERIFY(previewContainer);

    QSignalSpy hoverSpy(
        rootItem, SIGNAL(autoBlurButtonHovered(double,double,double,double)));
    QSignalSpy exitSpy(rootItem, SIGNAL(autoBlurButtonHoverExited()));
    QVERIFY(hoverSpy.isValid());
    QVERIFY(exitSpy.isValid());

    QVERIFY(rootItem->setProperty("autoBlurHintActive", true));
    QTRY_VERIFY(autoBlurButton->scale() > 1.0);
    QVERIFY(rootItem->setProperty("autoBlurHintActive", false));
    QTRY_COMPARE(autoBlurButton->scale(), 1.0);

    const QRectF previewRect = previewContainer->mapRectToScene(
        QRectF(0.0, 0.0, previewContainer->width(), previewContainer->height()));
    const QRectF autoBlurRect = autoBlurButton->mapRectToScene(
        QRectF(0.0, 0.0, autoBlurButton->width(), autoBlurButton->height()));

    QTest::mouseMove(&view, previewRect.center().toPoint());
    QCoreApplication::processEvents();
    hoverSpy.clear();
    exitSpy.clear();

    QTest::mouseMove(&view, autoBlurRect.center().toPoint());
    QTRY_COMPARE(hoverSpy.count(), 1);

    const auto arguments = hoverSpy.takeFirst();
    const QPointF expectedAnchor = autoBlurButton->mapToGlobal(QPointF(0.0, 0.0));
    QVERIFY(qAbs(arguments.at(0).toDouble() - expectedAnchor.x()) < 1.0);
    QVERIFY(qAbs(arguments.at(1).toDouble() - expectedAnchor.y()) < 1.0);
    QCOMPARE(arguments.at(2).toDouble(), 22.0);
    QCOMPARE(arguments.at(3).toDouble(), 22.0);

    QTest::mouseMove(&view, previewRect.center().toPoint());
    QTRY_COMPARE(exitSpy.count(), 1);
}

QTEST_MAIN(tst_WidthSectionQml)
#include "tst_WidthSectionQml.moc"
