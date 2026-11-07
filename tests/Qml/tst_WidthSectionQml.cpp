#include "annotations/AnnotationStyle.h"
#include <QtTest/QtTest>

#include <QColor>
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
    void testStripLabelNamesTarget();
    void testPreviewDotCenterStaysFixedAcrossWidthChanges();
    void testNonMosaicPreviewRemainsCompact();
    void testMosaicPresetsFollowToolAndHaveExpectedGeometry();
    void testMosaicPresetActiveStateRequiresExactWidth();
    void testMosaicPresetClickUpdatesWidth();
    void testMosaicPresetConsumesWheelWithoutChangingWidth();
    void testNonMosaicWidthPreviewAllowsStripWideWheelAdjustment();
    void testToolOptionsStripForwardsAutoBlurHover();
    void testMarkerTextSnapToggleHoverAndStates();
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

    // Odd diameters land the dot on half-pixel offsets, which the software
    // scene graph (Windows 10) rasterizes as a lopsided circle.
    for (int width = 1; width <= 30; ++width) {
        viewModel.setCurrentWidth(width);
        QCoreApplication::processEvents();

        QCOMPARE(qRound(previewDot->x()), previewDot->x());
        QCOMPARE(qRound(previewDot->y()), previewDot->y());
    }
}

void tst_WidthSectionQml::testNonMosaicPreviewRemainsCompact()
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

    QCOMPARE(rootItem->width(), 74.0);
    QCOMPARE(rootItem->height(), 28.0);
    QCOMPARE(previewSlot->width(), 28.0);
    QCOMPARE(previewSlot->height(), 28.0);
    QVERIFY(!previewSlot->isVisible());
    QCOMPARE(previewContainer->width(), 22.0);
    QCOMPARE(previewContainer->height(), 22.0);
    QVERIFY(presetRow->isVisible());
    QCOMPARE(presetRow->width(), 70.0);
    const QPointF presetOrigin = presetRow->mapToItem(rootItem, QPointF(0.0, 0.0));
    QCOMPARE(presetOrigin.x(), 3.0);
    QCOMPARE(rootItem->width() - presetOrigin.x() - presetRow->width(), 1.0);

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
        QVERIFY(previewSlot->isVisible());
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

void tst_WidthSectionQml::testMosaicPresetClickUpdatesWidth()
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

    const QPoint clickPosition = mediumButton->mapToScene(
        QPointF(mediumButton->width() / 2.0,
                mediumButton->height() / 2.0)).toPoint();
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, clickPosition);

    QTRY_COMPARE(viewModel.currentWidth(), 18);
    QVERIFY(mediumButton->property("selected").toBool());
}

void tst_WidthSectionQml::testMosaicPresetConsumesWheelWithoutChangingWidth()
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
    wheelEvent.setAccepted(false);
    QCoreApplication::sendEvent(&view, &wheelEvent);

    QCOMPARE(viewModel.currentWidth(), 18);
    QVERIFY(wheelEvent.isAccepted());
}

void tst_WidthSectionQml::testNonMosaicWidthPreviewAllowsStripWideWheelAdjustment()
{
    if (QGuiApplication::screens().isEmpty())
        QSKIP("Non-Mosaic width wheel test requires a screen");

    PinToolOptionsViewModel viewModel;
    viewModel.showForTool(static_cast<int>(ToolId::Pencil));
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
        QSKIP("Non-Mosaic width wheel test window could not be exposed");

    auto* rootItem = view.rootObject();
    QVERIFY(rootItem);
    auto* previewContainer =
        rootItem->findChild<QQuickItem*>(QStringLiteral("widthPreviewContainer"));
    QVERIFY(previewContainer);

    const QPointF localPosition = previewContainer->mapToScene(
        QPointF(previewContainer->width() / 2.0,
                previewContainer->height() / 2.0));
    const QPointF globalPosition = view.mapToGlobal(localPosition.toPoint());
    QWheelEvent wheelEvent(localPosition,
                           globalPosition,
                           QPoint(),
                           QPoint(0, 120),
                           Qt::NoButton,
                           Qt::NoModifier,
                           Qt::NoScrollPhase,
                           false);
    wheelEvent.setAccepted(false);
    QCoreApplication::sendEvent(&view, &wheelEvent);

    QTRY_COMPARE(viewModel.currentWidth(), 19);
    QVERIFY(wheelEvent.isAccepted());
}

void tst_WidthSectionQml::testToolOptionsStripForwardsAutoBlurHover()
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
    auto* presetRow =
        rootItem->findChild<QQuickItem*>(QStringLiteral("mosaicWidthPresetRow"));
    QVERIFY(autoBlurButton);
    QVERIFY(presetRow);

    QSignalSpy hoverSpy(rootItem, SIGNAL(controlTooltipRequested(QString,QString)));
    QSignalSpy exitSpy(rootItem, SIGNAL(controlTooltipDismissed(QString)));
    QVERIFY(hoverSpy.isValid());
    QVERIFY(exitSpy.isValid());

    QVERIFY(rootItem->setProperty("autoBlurHintActive", true));
    QTRY_VERIFY(autoBlurButton->scale() > 1.0);
    QVERIFY(rootItem->setProperty("autoBlurHintActive", false));
    QTRY_COMPARE(autoBlurButton->scale(), 1.0);

    const QRectF presetRect = presetRow->mapRectToScene(
        QRectF(0.0, 0.0, presetRow->width(), presetRow->height()));
    const QRectF autoBlurRect = autoBlurButton->mapRectToScene(
        QRectF(0.0, 0.0, autoBlurButton->width(), autoBlurButton->height()));

    QTest::mouseMove(&view, presetRect.center().toPoint());
    QCoreApplication::processEvents();
    hoverSpy.clear();
    exitSpy.clear();

    // The sub-toolbar anchors the tooltip to the control it names.
    QTest::mouseMove(&view, autoBlurRect.center().toPoint());
    QTRY_COMPARE(hoverSpy.count(), 1);
    const auto arguments = hoverSpy.takeFirst();
    QCOMPARE(arguments.at(0).toString(), QStringLiteral("autoBlurButton"));
    QCOMPARE(arguments.at(1).toString(), viewModel.autoBlurHintText());

    QTest::mouseMove(&view, presetRect.center().toPoint());
    QTRY_COMPARE(exitSpy.count(), 1);
    QCOMPARE(exitSpy.takeFirst().at(0).toString(), QStringLiteral("autoBlurButton"));
}

// #304: Snap to text is a 22x22 icon toggle styled like Shape's Filled toggle,
// with its explanation in the sub-toolbar tooltip.
void tst_WidthSectionQml::testMarkerTextSnapToggleHoverAndStates()
{
    if (QGuiApplication::screens().isEmpty())
        QSKIP("Snap to text hover test requires a screen");

    PinToolOptionsViewModel viewModel;
    viewModel.setMarkerTextSnapAvailable(true);
    viewModel.showForTool(static_cast<int>(ToolId::Marker));

    QQuickView view;
    view.rootContext()->setContextProperty(
        QStringLiteral("pinToolOptionsViewModel"), &viewModel);
    view.setResizeMode(QQuickView::SizeViewToRootObject);
    view.setSource(
        QUrl(QStringLiteral("qrc:/SnapTrayQml/toolbar/ToolOptionsStrip.qml")));
    QCOMPARE(view.status(), QQuickView::Ready);

    view.show();
    if (!QTest::qWaitForWindowExposed(&view))
        QSKIP("Snap to text hover test window could not be exposed");

    auto* rootItem = view.rootObject();
    QVERIFY(rootItem);
    auto* toggle = rootItem->findChild<QQuickItem*>(QStringLiteral("markerTextSnapToggle"));
    QVERIFY(toggle);
    QVERIFY(toggle->isVisible());
    QCOMPARE(toggle->width(), 22.0);
    QCOMPARE(toggle->height(), 22.0);

    // Same 28px content row and 36px glass as before: the toggle adds no height.
    QCOMPARE(rootItem->height(), 36.0);

    const QRectF toggleRect = toggle->mapRectToScene(
        QRectF(0.0, 0.0, toggle->width(), toggle->height()));
    QCOMPARE(toggleRect.center().y(), rootItem->height() / 2.0);

    QSignalSpy hoverSpy(rootItem, SIGNAL(controlTooltipRequested(QString,QString)));
    QSignalSpy exitSpy(rootItem, SIGNAL(controlTooltipDismissed(QString)));
    QVERIFY(hoverSpy.isValid());
    QVERIFY(exitSpy.isValid());

    const QPoint away(2, 2);
    QTest::mouseMove(&view, away);
    QCoreApplication::processEvents();
    hoverSpy.clear();
    exitSpy.clear();

    // Off: transparent, then the hover background under the pointer.
    QCOMPARE(toggle->property("color").value<QColor>().alpha(), 0);
    QTest::mouseMove(&view, toggleRect.center().toPoint());
    QTRY_COMPARE(hoverSpy.count(), 1);
    const auto arguments = hoverSpy.takeFirst();
    QCOMPARE(arguments.at(0).toString(), QStringLiteral("markerTextSnapToggle"));
    QCOMPARE(arguments.at(1).toString(),
             QStringLiteral("Snap to text: marker strokes align to detected text lines"));
    QCOMPARE(toggle->property("color").value<QColor>(),
             rootItem->property("controlHoverBg").value<QColor>());

    // On: a solid accent block, also while hovered.
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, toggleRect.center().toPoint());
    QTRY_VERIFY(viewModel.markerTextSnapEnabled());
    const QColor onColor = toggle->property("color").value<QColor>();
    QCOMPARE(onColor.alpha(), 255);
    QVERIFY(onColor != rootItem->property("controlHoverBg").value<QColor>());

    QTest::mouseMove(&view, away);
    QTRY_COMPARE(exitSpy.count(), 1);
    QCOMPARE(exitSpy.takeFirst().at(0).toString(), QStringLiteral("markerTextSnapToggle"));
    QCOMPARE(toggle->property("color").value<QColor>(), onColor);

    // The strip's width does not depend on the toggle's state.
    const qreal widthOn = rootItem->width();
    viewModel.setMarkerTextSnapEnabled(false);
    QTRY_COMPARE(toggle->property("color").value<QColor>().alpha(), 0);
    QCOMPARE(rootItem->width(), widthOn);
}

void tst_WidthSectionQml::testStripLabelNamesTarget()
{
    PinToolOptionsViewModel viewModel;
    viewModel.showForTool(static_cast<int>(ToolId::Arrow));
    QQuickView view;
    view.rootContext()->setContextProperty(QStringLiteral("pinToolOptionsViewModel"), &viewModel);
    view.setResizeMode(QQuickView::SizeViewToRootObject);
    view.setSource(QUrl(QStringLiteral("qrc:/SnapTrayQml/toolbar/ToolOptionsStrip.qml")));
    QCOMPARE(view.status(), QQuickView::Ready);
    auto* rootItem = view.rootObject();
    auto* label = findVisualItem(rootItem, QStringLiteral("toolOptionsTargetLabel"));
    QVERIFY(label);
    QVERIFY(label->isVisible());
    auto labelText = [label]() {
        return label->childItems().first()->property("text").toString();
    };
    QCOMPARE(labelText(), QStringLiteral("New object"));
    const qreal widthBefore = rootItem->width();
    AnnotationStyleValues arrow;
    arrow.kind = StyleTargetKind::Arrow;
    arrow.color = Qt::blue;
    arrow.width = 4;
    viewModel.showForSelection(arrow);
    QTRY_COMPARE(labelText(), QStringLiteral("Selected: Arrow"));
    QTRY_VERIFY(rootItem->width() > widthBefore); // the strip grows to fit the label
}

QTEST_MAIN(tst_WidthSectionQml)
#include "tst_WidthSectionQml.moc"
