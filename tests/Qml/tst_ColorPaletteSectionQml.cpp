#include <QtTest/QtTest>

#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QtQml/qqmlextensionplugin.h>

#include "qml/PinToolOptionsViewModel.h"
#include "tools/ToolId.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

// #305: the eyedropper sat between the custom color and the presets, so a faint
// line icon cut the row of solid swatches in two and read as a disabled button.
// The swatches now run unbroken and the eyedropper ends the row, set apart by
// spacing alone (the strip has no separators between its own sections either).
class tst_ColorPaletteSectionQml : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void swatchesRunUnbrokenAndTheEyedropperEndsTheRow();
    void eyedropperStaysCloserToTheSwatchesThanToTheNextControl();
    void eyedropperIsNotDimmedAtRest();
    void hoveringTheEyedropperRequestsItsNameAndShortcut();
    void leavingOrPressingTheEyedropperDismissesItsTooltip();

private:
    std::unique_ptr<QQuickView> load(PinToolOptionsViewModel& viewModel);
    static QQuickItem* item(QQuickView& view, const char* objectName);
    static QRectF rectIn(QQuickItem* item, QQuickItem* root);
    static QPoint centerInWindow(QQuickItem* item);
};

void tst_ColorPaletteSectionQml::initTestCase()
{
    if (QGuiApplication::screens().isEmpty())
        QSKIP("The style strip needs a screen");
}

std::unique_ptr<QQuickView> tst_ColorPaletteSectionQml::load(PinToolOptionsViewModel& viewModel)
{
    auto view = std::make_unique<QQuickView>();
    view->rootContext()->setContextProperty(QStringLiteral("pinToolOptionsViewModel"), &viewModel);
    view->setResizeMode(QQuickView::SizeViewToRootObject);
    view->setSource(QUrl(QStringLiteral("qrc:/SnapTrayQml/toolbar/ToolOptionsStrip.qml")));
    if (view->status() != QQuickView::Ready) return nullptr;
    view->show();
    if (!QTest::qWaitForWindowExposed(view.get())) return nullptr;
    return view;
}

QQuickItem* tst_ColorPaletteSectionQml::item(QQuickView& view, const char* objectName)
{
    // Walk the visual tree: Repeater delegates (the preset swatches) are not
    // QObject children of the strip, so findChild() cannot see them.
    const QString name = QString::fromLatin1(objectName);
    QList<QQuickItem*> pending{view.rootObject()};
    while (!pending.isEmpty()) {
        QQuickItem* node = pending.takeFirst();
        if (node->objectName() == name)
            return node;
        pending.append(node->childItems());
    }
    return nullptr;
}

QRectF tst_ColorPaletteSectionQml::rectIn(QQuickItem* item, QQuickItem* root)
{
    return item->mapRectToItem(root, QRectF(0, 0, item->width(), item->height()));
}

QPoint tst_ColorPaletteSectionQml::centerInWindow(QQuickItem* item)
{
    return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
}

void tst_ColorPaletteSectionQml::swatchesRunUnbrokenAndTheEyedropperEndsTheRow()
{
    PinToolOptionsViewModel viewModel;
    viewModel.showForTool(static_cast<int>(ToolId::Pencil));
    auto view = load(viewModel);
    QVERIFY(view);
    QQuickItem* root = view->rootObject();

    QQuickItem* custom = item(*view, "customColorSwatch");
    QQuickItem* pick = item(*view, "colorPickButton");
    QVERIFY(custom && custom->isVisible());
    QVERIFY(pick && pick->isVisible());

    const qsizetype presetCount = viewModel.colorPaletteOptions().size();
    QVERIFY(presetCount > 0);

    // Custom color, then every preset, 3px apart with nothing in between.
    QRectF previous = rectIn(custom, root);
    for (qsizetype i = 0; i < presetCount; ++i) {
        QQuickItem* swatch = item(*view, qPrintable(QStringLiteral("colorSwatch_%1").arg(i)));
        QVERIFY2(swatch, qPrintable(QStringLiteral("colorSwatch_%1 missing").arg(i)));
        const QRectF current = rectIn(swatch, root);
        QCOMPARE(current.left() - previous.right(), 3.0);
        QCOMPARE(current.top(), previous.top());
        previous = current;
    }

    // The eyedropper comes after the last preset, a wider 6px step away, on
    // the same row and at the same height as the swatches.
    const QRectF pickRect = rectIn(pick, root);
    QCOMPARE(pickRect.left() - previous.right(), 6.0);
    QCOMPARE(pickRect.top(), previous.top());
    QCOMPARE(pickRect.height(), previous.height());
    QVERIFY(pickRect.left() > rectIn(custom, root).right());
}

void tst_ColorPaletteSectionQml::eyedropperStaysCloserToTheSwatchesThanToTheNextControl()
{
    // Marker shows the colors followed by the "Snap to text" toggle: the
    // eyedropper must read as part of the color row, not of that next control.
    PinToolOptionsViewModel viewModel;
    viewModel.showForTool(static_cast<int>(ToolId::Marker));
    viewModel.setMarkerTextSnapAvailable(true);
    auto view = load(viewModel);
    QVERIFY(view);
    QQuickItem* root = view->rootObject();

    QQuickItem* pick = item(*view, "colorPickButton");
    QQuickItem* next = item(*view, "markerTextSnapToggle");
    const qsizetype presetCount = viewModel.colorPaletteOptions().size();
    QQuickItem* lastSwatch = item(*view, qPrintable(QStringLiteral("colorSwatch_%1").arg(presetCount - 1)));
    QVERIFY(pick && next && lastSwatch);
    QTRY_VERIFY(next->isVisible());

    const QRectF pickRect = rectIn(pick, root);
    const qreal gapToSwatches = pickRect.left() - rectIn(lastSwatch, root).right();
    const qreal gapToNext = rectIn(next, root).left() - pickRect.right();
    QVERIFY2(gapToSwatches > 0,
             qPrintable(QStringLiteral("eyedropper is not after the swatches (gap %1)")
                            .arg(gapToSwatches)));
    QVERIFY2(gapToNext >= 2 * gapToSwatches,
             qPrintable(QStringLiteral("swatch gap %1, next-control gap %2")
                            .arg(gapToSwatches).arg(gapToNext)));
}

void tst_ColorPaletteSectionQml::eyedropperIsNotDimmedAtRest()
{
    // At 0.6 the idle icon sat between an enabled toolbar icon (1.0) and a
    // disabled one (0.4) and read as unavailable.
    PinToolOptionsViewModel viewModel;
    viewModel.showForTool(static_cast<int>(ToolId::Pencil));
    auto view = load(viewModel);
    QVERIFY(view);

    QQuickItem* icon = item(*view, "colorPickIcon");
    QVERIFY(icon && icon->isVisible());
    QCOMPARE(icon->opacity(), 1.0);
}

void tst_ColorPaletteSectionQml::hoveringTheEyedropperRequestsItsNameAndShortcut()
{
    PinToolOptionsViewModel viewModel;
    viewModel.showForTool(static_cast<int>(ToolId::Pencil));
    auto view = load(viewModel);
    QVERIFY(view);

    QSignalSpy requested(view->rootObject(), SIGNAL(controlTooltipRequested(QString,QString)));
    QVERIFY(requested.isValid());

    QQuickItem* pick = item(*view, "colorPickButton");
    QQuickItem* custom = item(*view, "customColorSwatch");
    QVERIFY(pick && custom);
    // Start off the eyedropper: a move to the cursor's current position delivers no hover.
    QTest::mouseMove(view.get(), centerInWindow(custom));
    QTest::mouseMove(view.get(), centerInWindow(pick));

    QTRY_COMPARE(requested.count(), 1);
    QCOMPARE(requested.at(0).at(0).toString(), QStringLiteral("colorPickButton"));
    QCOMPARE(requested.at(0).at(1).toString(), QStringLiteral("Pick color from screen (I)"));
}

void tst_ColorPaletteSectionQml::leavingOrPressingTheEyedropperDismissesItsTooltip()
{
    PinToolOptionsViewModel viewModel;
    viewModel.showForTool(static_cast<int>(ToolId::Pencil));
    auto view = load(viewModel);
    QVERIFY(view);

    QSignalSpy requested(view->rootObject(), SIGNAL(controlTooltipRequested(QString,QString)));
    QSignalSpy dismissed(view->rootObject(), SIGNAL(controlTooltipDismissed(QString)));
    QSignalSpy pickRequested(&viewModel, &PinToolOptionsViewModel::colorPickRequested);
    QVERIFY(requested.isValid() && dismissed.isValid());

    QQuickItem* pick = item(*view, "colorPickButton");
    QQuickItem* custom = item(*view, "customColorSwatch");
    QVERIFY(pick && custom);

    // Start off the eyedropper: a move to the cursor's current position (where
    // the previous test left it) delivers no hover.
    QTest::mouseMove(view.get(), centerInWindow(custom));
    QTest::mouseMove(view.get(), centerInWindow(pick));
    QTRY_COMPARE(requested.count(), 1);
    QTest::mouseMove(view.get(), centerInWindow(custom));
    QTRY_COMPARE(dismissed.count(), 1);
    // It names itself, so a late dismissal cannot close another control's tooltip.
    QCOMPARE(dismissed.at(0).at(0).toString(), QStringLiteral("colorPickButton"));

    // Pressing starts picking, so the tooltip must not sit over the session.
    QTest::mouseMove(view.get(), centerInWindow(pick));
    QTRY_COMPARE(requested.count(), 2);
    QTest::mouseClick(view.get(), Qt::LeftButton, Qt::NoModifier, centerInWindow(pick));
    QTRY_COMPARE(pickRequested.count(), 1);
    QCOMPARE(dismissed.count(), 2);
}

QTEST_MAIN(tst_ColorPaletteSectionQml)
#include "tst_ColorPaletteSectionQml.moc"
