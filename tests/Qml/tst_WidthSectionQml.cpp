#include <QtTest/QtTest>

#include <QQuickItem>
#include <QQuickView>
#include <QWheelEvent>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QtQml/qqmlextensionplugin.h>

#include "qml/PinToolOptionsViewModel.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

namespace {
QQuickItem* findVisualItem(QQuickItem* root, const QString& objectName)
{
    if (!root)
        return nullptr;
    if (root->objectName() == objectName)
        return root;

    const auto children = root->childItems();
    for (auto* child : children) {
        if (auto* result = findVisualItem(child, objectName))
            return result;
    }
    return nullptr;
}
}

class FakeWidthViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int currentWidth READ currentWidth WRITE setCurrentWidth NOTIFY currentWidthChanged)
    Q_PROPERTY(int minWidth READ minWidth CONSTANT)
    Q_PROPERTY(int maxWidth READ maxWidth CONSTANT)
    Q_PROPERTY(bool mosaicActive READ mosaicActive WRITE setMosaicActive NOTIFY mosaicActiveChanged)
    Q_PROPERTY(QVariantList mosaicWidthPresetOptions READ mosaicWidthPresetOptions CONSTANT)

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
    bool mosaicActive() const { return m_mosaicActive; }
    void setMosaicActive(bool active)
    {
        if (m_mosaicActive == active)
            return;

        m_mosaicActive = active;
        emit mosaicActiveChanged();
    }

    QVariantList mosaicWidthPresetOptions() const
    {
        return {
            QVariantMap{{QStringLiteral("value"), 10},
                        {QStringLiteral("previewDiameter"), 6}},
            QVariantMap{{QStringLiteral("value"), 18},
                        {QStringLiteral("previewDiameter"), 10}},
            QVariantMap{{QStringLiteral("value"), 30},
                        {QStringLiteral("previewDiameter"), 14}},
        };
    }

    Q_INVOKABLE void handleMosaicWidthPresetSelected(int width)
    {
        setCurrentWidth(width);
        emit mosaicWidthPresetSelected(width);
    }

signals:
    void currentWidthChanged();
    void mosaicActiveChanged();
    void mosaicWidthPresetSelected(int width);

private:
    int m_currentWidth = 1;
    bool m_mosaicActive = false;
};

class tst_WidthSectionQml : public QObject
{
    Q_OBJECT

private slots:
    void testPreviewDotCenterStaysFixedAcrossWidthChanges();
    void testPreviewRemainsCompactAndSupportsHintEmphasis();
    void testMosaicPresetsFollowToolAndHaveExpectedGeometry();
    void testMosaicPresetActiveStateRequiresExactWidth();
    void testMosaicPresetClickUpdatesWidthAndMarksAdjustmentLearned();
    void testMosaicPresetAllowsStripWideWheelAdjustment();
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

void tst_WidthSectionQml::testMosaicPresetsFollowToolAndHaveExpectedGeometry()
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

    PinToolOptionsViewModel viewModel;
    viewModel.showForTool(static_cast<int>(ToolId::Mosaic));

    QVariantMap initialProperties;
    initialProperties.insert(
        QStringLiteral("viewModel"),
        QVariant::fromValue(static_cast<QObject*>(&viewModel)));
    const QScopedPointer<QObject> created(
        component.createWithInitialProperties(initialProperties));
    auto* rootItem = qobject_cast<QQuickItem*>(created.get());
    QVERIFY(rootItem);

    auto* previewSlot =
        rootItem->findChild<QQuickItem*>(QStringLiteral("widthPreviewSlot"));
    auto* previewContainer =
        rootItem->findChild<QQuickItem*>(QStringLiteral("widthPreviewContainer"));
    auto* presetRow =
        rootItem->findChild<QQuickItem*>(QStringLiteral("mosaicWidthPresetRow"));
    QVERIFY(previewSlot);
    QVERIFY(previewContainer);
    QVERIFY(presetRow);

    QCOMPARE(rootItem->width(), 100.0);
    QCOMPARE(rootItem->height(), 28.0);
    QCOMPARE(previewSlot->width(), 28.0);
    QCOMPARE(previewSlot->height(), 28.0);
    QCOMPARE(previewContainer->width(), 22.0);
    QCOMPARE(previewContainer->height(), 22.0);
    QVERIFY(presetRow->isVisible());
    QCOMPARE(presetRow->width(), 70.0);

    const int presetWidths[] = {10, 18, 30};
    const int previewDiameters[] = {6, 10, 14};
    for (int i = 0; i < 3; ++i) {
        auto* button = findVisualItem(
            rootItem,
            QStringLiteral("mosaicWidthPresetButton_%1").arg(presetWidths[i]));
        auto* dot = findVisualItem(
            rootItem,
            QStringLiteral("mosaicWidthPresetDot_%1").arg(presetWidths[i]));
        auto* mouseArea = findVisualItem(
            rootItem,
            QStringLiteral("mosaicWidthPresetMouseArea_%1").arg(presetWidths[i]));
        QVERIFY(button);
        QVERIFY(dot);
        QVERIFY(mouseArea);
        QCOMPARE(button->width(), 22.0);
        QCOMPARE(button->height(), 22.0);
        QCOMPARE(dot->width(), static_cast<qreal>(previewDiameters[i]));
        QCOMPARE(dot->height(), static_cast<qreal>(previewDiameters[i]));
    }

    const ToolId nonMosaicTools[] = {
        ToolId::Pencil,
        ToolId::Arrow,
        ToolId::Shape,
        ToolId::Polyline,
    };
    for (const ToolId tool : nonMosaicTools) {
        viewModel.showForTool(static_cast<int>(tool));
        QCoreApplication::processEvents();

        QVERIFY(viewModel.showWidthSection());
        QCOMPARE(rootItem->width(), 28.0);
        QCOMPARE(rootItem->height(), 28.0);
        QVERIFY(!presetRow->isVisible());

        const QPointF expectedCenter = rootItem->mapToScene(QPointF(14.0, 14.0));
        const QPointF previewCenter = previewContainer->mapToScene(
            QPointF(previewContainer->width() / 2.0,
                    previewContainer->height() / 2.0));
        QCOMPARE(previewCenter, expectedCenter);
    }
}

void tst_WidthSectionQml::testMosaicPresetActiveStateRequiresExactWidth()
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

    PinToolOptionsViewModel viewModel;
    viewModel.showForTool(static_cast<int>(ToolId::Mosaic));

    QVariantMap initialProperties;
    initialProperties.insert(
        QStringLiteral("viewModel"),
        QVariant::fromValue(static_cast<QObject*>(&viewModel)));
    const QScopedPointer<QObject> created(
        component.createWithInitialProperties(initialProperties));
    auto* rootItem = qobject_cast<QQuickItem*>(created.get());
    QVERIFY(rootItem);

    auto* smallButton = findVisualItem(
        rootItem, QStringLiteral("mosaicWidthPresetButton_10"));
    auto* mediumButton = findVisualItem(
        rootItem, QStringLiteral("mosaicWidthPresetButton_18"));
    auto* largeButton = findVisualItem(
        rootItem, QStringLiteral("mosaicWidthPresetButton_30"));
    QVERIFY(smallButton);
    QVERIFY(mediumButton);
    QVERIFY(largeButton);

    const auto verifySelection = [&](bool small, bool medium, bool large) {
        QCOMPARE(smallButton->property("selected").toBool(), small);
        QCOMPARE(mediumButton->property("selected").toBool(), medium);
        QCOMPARE(largeButton->property("selected").toBool(), large);
    };

    viewModel.setCurrentWidth(10);
    QCoreApplication::processEvents();
    verifySelection(true, false, false);

    viewModel.setCurrentWidth(18);
    QCoreApplication::processEvents();
    verifySelection(false, true, false);

    viewModel.setCurrentWidth(30);
    QCoreApplication::processEvents();
    verifySelection(false, false, true);

    viewModel.setCurrentWidth(17);
    QCoreApplication::processEvents();
    verifySelection(false, false, false);
}

void tst_WidthSectionQml::testMosaicPresetClickUpdatesWidthAndMarksAdjustmentLearned()
{
    if (QGuiApplication::screens().isEmpty())
        QSKIP("Mosaic preset click test requires a screen");

    PinToolOptionsViewModel viewModel;
    viewModel.showForTool(static_cast<int>(ToolId::Mosaic));
    viewModel.setCurrentWidth(3);

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

    auto* rootItem = view.rootObject();
    QVERIFY(rootItem);

    view.show();
    if (!QTest::qWaitForWindowExposed(&view))
        QSKIP("Mosaic preset click test window could not be exposed");

    auto* mediumButton = findVisualItem(
        rootItem, QStringLiteral("mosaicWidthPresetButton_18"));
    QVERIFY(mediumButton);
    QSignalSpy learnedSpy(
        &viewModel, &PinToolOptionsViewModel::mosaicBrushAdjustmentLearned);

    const QPoint clickPosition = mediumButton->mapToScene(
        QPointF(mediumButton->width() / 2.0,
                mediumButton->height() / 2.0)).toPoint();
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, clickPosition);

    QTRY_COMPARE(viewModel.currentWidth(), 18);
    QTRY_COMPARE(learnedSpy.count(), 1);
    QVERIFY(mediumButton->property("selected").toBool());
}

void tst_WidthSectionQml::testMosaicPresetAllowsStripWideWheelAdjustment()
{
    if (QGuiApplication::screens().isEmpty())
        QSKIP("Mosaic preset wheel test requires a screen");

    PinToolOptionsViewModel viewModel;
    viewModel.showForTool(static_cast<int>(ToolId::Mosaic));
    viewModel.setCurrentWidth(18);

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
        QSKIP("Mosaic preset wheel test window could not be exposed");

    auto* rootItem = view.rootObject();
    QVERIFY(rootItem);
    auto* mediumButton = findVisualItem(
        rootItem, QStringLiteral("mosaicWidthPresetButton_18"));
    QVERIFY(mediumButton);

    const QPointF localPosition = mediumButton->mapToScene(
        QPointF(mediumButton->width() / 2.0,
                mediumButton->height() / 2.0));
    const QPointF globalPosition = view.mapToGlobal(localPosition.toPoint());
    QWheelEvent wheelEvent(localPosition,
                           globalPosition,
                           QPoint(),
                           QPoint(0, 120),
                           Qt::NoButton,
                           Qt::NoModifier,
                           Qt::NoScrollPhase,
                           false);
    QCoreApplication::sendEvent(&view, &wheelEvent);

    QTRY_COMPARE(viewModel.currentWidth(), 19);
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
