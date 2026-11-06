// Screenshot and operation-video evidence for the capture toolbar (#224).
//
// Every slot skips unless its environment variable is set:
//   SNAPTRAY_TOOLBAR_DESIGN_DIR   screenshots (light/dark, normal/narrow/selected)
//   SNAPTRAY_TOOLBAR_TRANSLATION  a .qm to render a long translation as well
//   SNAPTRAY_TOOLBAR_VIDEO_DIR    frames for the select → restyle → undo video, and
//                                 <dir>/geometry-undo for the move/rotate undo video (#237)
// The real toolbar, More menu, and style strip QML render over a sample
// screenshot; style edits run through AnnotationStyleEditor and AnnotationLayer.

#include <QtTest>
#include <QtMath>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTranslator>
#include <QtQml/qqmlextensionplugin.h>

#include "ToolbarStyle.h"
#include "annotation/AnnotationStyleEditor.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/ShapeAnnotation.h"
#include "region/ShapeAnnotationEditor.h"
#include "qml/PinToolOptionsViewModel.h"
#include "qml/RegionToolbarViewModel.h"
#include "qml/ToolbarOverflowLayout.h"
#include "tools/ToolId.h"
#include "ui/DesignSystem.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

using namespace SnapTray;

namespace {

constexpr int kCaption = 40;
constexpr QSize kNormalSize(980, 520);
constexpr QSize kNarrowSize(380, 820);
// A grab's width is rounded to whole device pixels, so the ratio Scene::grab()
// derives from it can be off by up to 1/width of the window's real ratio.
constexpr qreal kDprTolerance = 0.01;
const QRect kNormalSelection(60, 70, 560, 300);
const QRect kNarrowSelection(40, 70, 300, 300);
const QPoint kArrowStart(150, 300);
const QPoint kArrowEnd(420, 160);

const char* kSceneQml = R"(
import QtQuick
import SnapTrayQml
import "qrc:/SnapTrayQml/toolbar"

Item {
    id: scene
    property url background
    property var toolbarModel
    property var optionsModel
    property var displayButtons: []
    property var overflowButtons: []
    property int toolbarWidth: -1
    property bool menuOpen: false
    property int toolbarX: 0
    property int toolbarY: 0

    Image { anchors.fill: parent; source: scene.background; cache: false }

    FloatingToolbar {
        id: bar
        objectName: "evidenceToolbar"
        viewModel: scene.toolbarModel
        displayButtons: scene.displayButtons
        overflowButtons: scene.overflowButtons
        constrainedWidth: scene.toolbarWidth
        showDragHandle: true
        x: scene.toolbarX
        y: scene.toolbarY
    }
    ToolOptionsStrip {
        objectName: "evidenceStrip"
        viewModel: scene.optionsModel
        x: bar.x
        y: bar.y + bar.height + 6
    }
    // The More menu as the app shows it (its own window there; inline here).
    Loader {
        id: menu
        visible: scene.menuOpen
        x: Math.max(4, bar.x + bar.width - width)
        y: bar.y + bar.height + 6
        Component.onCompleted: setSource("qrc:/SnapTrayQml/toolbar/ToolbarOverflowMenu.qml",
                                         { viewModel: scene.toolbarModel, maximumWidth: 240 })
        Binding { target: menu.item; property: "buttons"; value: scene.overflowButtons; when: menu.item !== null }
    }
}
)";

class Scene
{
public:
    Scene(const QString& dir, QSize size)
        : m_size(size)
        , m_selection(size.width() < kNormalSize.width() ? kNarrowSelection : kNormalSelection)
    {
        const bool narrow = m_selection == kNarrowSelection;
        m_arrowStart = narrow ? QPoint(80, 320) : kArrowStart;
        m_arrowEnd = narrow ? QPoint(280, 150) : kArrowEnd;
        m_layer.addItem(std::make_unique<ArrowAnnotation>(m_arrowStart, m_arrowEnd,
                                                          QColor(0xFF, 0x3B, 0x30), 4));
        m_layer.addItem(std::make_unique<ShapeAnnotation>(narrow ? QRect(200, 250, 110, 80)
                                                                 : QRect(440, 110, 140, 90),
                                                          ShapeType::Rectangle,
                                                          QColor(0xFF, 0x3B, 0x30), 3));
        m_options.showForTool(static_cast<int>(ToolId::Arrow));
        m_toolbar.setActiveTool(static_cast<int>(ToolId::Arrow));
        // Undo/Redo follow the layer, as in Region Capture.
        QObject::connect(&m_layer, &AnnotationLayer::changed, &m_layer, [this] {
            m_toolbar.setCanUndo(m_layer.canUndo());
            m_toolbar.setCanRedo(m_layer.canRedo());
        });
        m_toolbar.setCanUndo(m_layer.canUndo());
        m_qmlPath = QDir(dir).filePath("scene.qml");
        QFile file(m_qmlPath);
        if (file.open(QIODevice::WriteOnly)) file.write(kSceneQml);
        m_bgBase = QDir(dir).filePath("bg");
    }

    bool open()
    {
        renderBackground();
        m_view.setResizeMode(QQuickView::SizeRootObjectToView);
        m_view.setInitialProperties({
            {"toolbarModel", QVariant::fromValue(static_cast<QObject*>(&m_toolbar))},
            {"optionsModel", QVariant::fromValue(static_cast<QObject*>(&m_options))},
            {"background", QUrl::fromLocalFile(m_bgPath)},
        });
        m_view.setSource(QUrl::fromLocalFile(m_qmlPath));
        if (m_view.status() != QQuickView::Ready) {
            qWarning() << m_view.errors();
            return false;
        }
        m_view.resize(m_size);
        layoutToolbar(m_size.width() - 20);
        m_view.show();
        return QTest::qWaitForWindowExposed(&m_view);
    }

    void layoutToolbar(int maximumWidth)
    {
        QObject* root = m_view.rootObject();
        auto* bar = root->findChild<QQuickItem*>("evidenceToolbar");
        const ToolbarLayoutMetrics metrics{
            bar->property("buttonWidth").toInt(), bar->property("buttonSpacing").toInt(),
            bar->property("separatorWidth").toInt(), bar->property("margin").toInt(),
            bar->property("dragHandleWidth").toInt(), bar->property("contentSpacing").toInt(),
            bar->property("groupGap").toInt()};
        const auto layout = computeToolbarOverflow(m_toolbar.buttons(), maximumWidth,
                                                   m_toolbar.activeTool(), metrics);
        root->setProperty("displayButtons", layout.visibleButtons);
        root->setProperty("overflowButtons", layout.overflowButtons);
        root->setProperty("toolbarWidth", layout.width);
        // Below the selection, right-aligned to it, kept on screen (as the app places it).
        const int x = qBound(10, m_selection.right() - layout.width, m_size.width() - layout.width - 10);
        root->setProperty("toolbarX", x);
        root->setProperty("toolbarY", m_selection.bottom() + 10);
    }

    void renderBackground(const QPoint& cursor = QPoint(-1, -1), bool pressed = false)
    {
        QImage image(m_size, QImage::Format_ARGB32_Premultiplied);
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        QLinearGradient sky(0, 0, m_size.width(), m_size.height());
        sky.setColorAt(0, QColor("#3a6ea5"));
        sky.setColorAt(1, QColor("#c96f53"));
        p.fillRect(image.rect(), sky);
        // A stand-in application window under the capture.
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(250, 250, 252));
        p.drawRoundedRect(QRect(30, 40, m_size.width() - 60, 380), 10, 10);
        p.setBrush(QColor(225, 228, 234));
        p.drawRoundedRect(QRect(30, 40, m_size.width() - 60, 34), 10, 10);
        p.setBrush(QColor(200, 205, 214));
        for (int i = 0; i < 6; ++i)
            p.drawRoundedRect(QRect(60, 100 + i * 42, qMin(500, m_size.width() - 120) - i * 40, 14), 4, 4);
        // Dim outside the selection, like Region Capture.
        QPainterPath outside;
        outside.addRect(image.rect());
        QPainterPath hole;
        hole.addRect(m_selection);
        p.fillPath(outside.subtracted(hole), QColor(0, 0, 0, 110));
        p.setPen(QPen(QColor(0x3D, 0x8B, 0xFF), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(m_selection);
        m_layer.draw(p);
        if (AnnotationItem* selected = m_layer.selectedItem()) {
            p.setPen(QPen(QColor(0x3D, 0x8B, 0xFF), 1.5, Qt::DashLine));
            p.drawRect(selected->boundingRect().adjusted(-6, -6, 6, 6));
            p.setBrush(Qt::white);
            p.setPen(QPen(QColor(0x3D, 0x8B, 0xFF), 2));
            p.drawEllipse(QPointF(m_arrowStart), 5, 5);
            p.drawEllipse(QPointF(m_arrowEnd), 5, 5);
        }
        if (cursor.x() >= 0) drawCursor(p, cursor, pressed);
        p.end();
        // A fresh file name per frame; Image caches by URL.
        m_bgPath = QStringLiteral("%1-%2.png").arg(m_bgBase).arg(m_bgSerial++);
        image.save(m_bgPath);
        if (QObject* root = m_view.rootObject())
            root->setProperty("background", QUrl::fromLocalFile(m_bgPath));
    }

    static void drawCursor(QPainter& p, const QPoint& at, bool pressed)
    {
        QPainterPath arrow;
        const QPointF c = at;
        arrow.moveTo(c);
        arrow.lineTo(c + QPointF(0, 17));
        arrow.lineTo(c + QPointF(4.5, 13));
        arrow.lineTo(c + QPointF(8, 20));
        arrow.lineTo(c + QPointF(10.5, 19));
        arrow.lineTo(c + QPointF(7, 12));
        arrow.lineTo(c + QPointF(12.5, 12));
        arrow.closeSubpath();
        if (pressed) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 196, 0, 110));
            p.drawEllipse(c, 14, 14);
        }
        p.setPen(QPen(Qt::white, 1.5));
        p.setBrush(Qt::black);
        p.drawPath(arrow);
    }

    QImage grab()
    {
        for (int i = 0; i < 3; ++i) {
            QSignalSpy rendered(&m_view, &QQuickWindow::afterAnimating);
            m_view.requestUpdate();
            rendered.wait(300);
        }
        QTest::qWait(80);
        QImage image = m_view.grabWindow();
        // The software scene graph returns a device-pixel grab tagged with
        // ratio 1 (seen at 1.5x and 2x on X11), so take the ratio from the
        // grab's size instead, as tst_RecordingPreviewMasks does (#254).
        if (m_view.width() > 0) image.setDevicePixelRatio(qreal(image.width()) / m_view.width());
        return image;
    }

    QQuickItem* find(const QString& name) const
    {
        QList<QQuickItem*> pending{m_view.rootObject() ? qobject_cast<QQuickItem*>(m_view.rootObject()) : nullptr};
        while (!pending.isEmpty()) {
            QQuickItem* item = pending.takeLast();
            if (!item) continue;
            if (item->objectName() == name) return item;
            pending.append(item->childItems());
        }
        return nullptr;
    }

    QSize m_size;
    QRect m_selection;
    QPoint m_arrowStart;
    QPoint m_arrowEnd;
    QQuickView m_view;
    AnnotationLayer m_layer;
    PinToolOptionsViewModel m_options;
    RegionToolbarViewModel m_toolbar;
    AnnotationStyleEditor m_editor{&m_layer, &m_options};
    QString m_qmlPath;
    QString m_bgBase;
    QString m_bgPath;
    int m_bgSerial = 0;
};

void setTheme(bool dark)
{
    ToolbarStyleConfig::saveStyle(dark ? ToolbarStyleType::Dark : ToolbarStyleType::Light);
    DesignSystem::instance().refreshTheme();
}

} // namespace

class tst_ToolbarEvidence : public QObject
{
    Q_OBJECT
private slots:
    void toolbarStates_data();
    void toolbarStates();
    void operationVideo();
    void geometryUndoVideo();
};

void tst_ToolbarEvidence::toolbarStates_data()
{
    QTest::addColumn<bool>("dark");
    QTest::newRow("light") << false;
    QTest::newRow("dark") << true;
}

void tst_ToolbarEvidence::toolbarStates()
{
    const QString out = qEnvironmentVariable("SNAPTRAY_TOOLBAR_DESIGN_DIR");
    if (out.isEmpty()) QSKIP("Set SNAPTRAY_TOOLBAR_DESIGN_DIR to capture toolbar screenshots");
    QFETCH(bool, dark);
    QString suffix = dark ? QStringLiteral("dark") : QStringLiteral("light");
    QTranslator translator;
    const QString qm = qEnvironmentVariable("SNAPTRAY_TOOLBAR_TRANSLATION");
    if (!qm.isEmpty()) {
        QVERIFY2(translator.load(qm), qPrintable(qm));
        QCoreApplication::installTranslator(&translator);
        suffix += QLatin1Char('-') + QFileInfo(qm).completeBaseName().section(QLatin1Char('_'), 1);
    }
    auto removeTranslator = qScopeGuard([&translator] { QCoreApplication::removeTranslator(&translator); });
    const auto originalStyle = ToolbarStyleConfig::loadStyle();
    auto restoreTheme = qScopeGuard([originalStyle] {
        ToolbarStyleConfig::saveStyle(originalStyle);
        DesignSystem::instance().refreshTheme();
    });
    setTheme(dark);
    QDir().mkpath(out);
    QTemporaryDir tmp;

    const auto save = [&](Scene& scene, const QString& name) {
        const QImage image = scene.grab();
        const QString file = QDir(out).filePath(QStringLiteral("%1-%2.png").arg(name, suffix));
        QVERIFY2(image.save(file), qPrintable(file));
    };

    {   // Normal width: three groups, nothing collapsed, "New object".
        Scene scene(tmp.path(), kNormalSize);
        QVERIFY(scene.open());
        QVERIFY(scene.m_view.rootObject()->property("overflowButtons").toList().isEmpty());
        save(scene, QStringLiteral("01-normal"));
        // Arrow selected: the strip edits it and says so.
        scene.m_layer.setSelectedIndex(0);
        QCOMPARE(scene.m_options.editingSelection(), true);
        scene.renderBackground();
        save(scene, QStringLiteral("02-selected"));
        // Screenshots are saved at the window's device pixel ratio (2x on Retina).
        const qreal grabRatio = scene.grab().devicePixelRatio();
        QVERIFY2(qAbs(grabRatio - scene.m_view.devicePixelRatio()) < kDprTolerance,
                 qPrintable(QStringLiteral("grab ratio %1, window ratio %2")
                                .arg(grabRatio)
                                .arg(scene.m_view.devicePixelRatio())));
    }
    {   // Narrow screen: low-priority buttons in More, menu open.
        Scene scene(tmp.path(), kNarrowSize);
        QVERIFY(scene.open());
        const QVariantList overflow = scene.m_view.rootObject()->property("overflowButtons").toList();
        QVERIFY(!overflow.isEmpty());
        scene.m_view.rootObject()->setProperty("menuOpen", true);
        save(scene, QStringLiteral("03-narrow-more"));
        scene.m_view.rootObject()->setProperty("menuOpen", false);
        scene.m_layer.setSelectedIndex(0);
        scene.renderBackground();
        save(scene, QStringLiteral("04-narrow-selected"));
    }
}

void tst_ToolbarEvidence::operationVideo()
{
    const QString out = qEnvironmentVariable("SNAPTRAY_TOOLBAR_VIDEO_DIR");
    if (out.isEmpty()) QSKIP("Set SNAPTRAY_TOOLBAR_VIDEO_DIR to record the operation video frames");
    const auto originalStyle = ToolbarStyleConfig::loadStyle();
    auto restoreTheme = qScopeGuard([originalStyle] {
        ToolbarStyleConfig::saveStyle(originalStyle);
        DesignSystem::instance().refreshTheme();
    });
    setTheme(false);
    QDir().mkpath(out);
    QTemporaryDir tmp;
    Scene scene(tmp.path(), kNormalSize);
    QVERIFY(scene.open());

    int count = 0;
    QPoint cursor(700, 450);
    QString caption;
    const auto frame = [&](int hold = 1, bool pressed = false) {
        scene.renderBackground();
        QImage image = scene.grab();
        const qreal dpr = image.devicePixelRatio();
        image = image.scaledToWidth(qRound(image.width() / dpr), Qt::SmoothTransformation);
        image.setDevicePixelRatio(1.0);
        QImage framed(image.width(), image.height() + kCaption, QImage::Format_RGB32);
        framed.fill(QColor("#101114"));
        QPainter painter(&framed);
        painter.drawImage(0, 0, image);
        painter.setRenderHint(QPainter::Antialiasing);
        Scene::drawCursor(painter, cursor, pressed);
        QFont font = painter.font();
        font.setPixelSize(18);
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(QColor("#f2f2f2"));
        painter.drawText(QRect(16, image.height(), image.width() - 32, kCaption), Qt::AlignVCenter, caption);
        painter.end();
        for (int i = 0; i < hold; ++i)
            framed.save(QStringLiteral("%1/frame-%2.png").arg(out).arg(count++, 5, 10, QLatin1Char('0')));
    };
    const auto moveTo = [&](const QPoint& to, int steps = 8) {
        const QPoint from = cursor;
        for (int i = 1; i <= steps; ++i) {
            cursor = from + (to - from) * i / steps;
            frame();
        }
    };
    const auto centerOf = [&](const QString& name) {
        QQuickItem* item = scene.find(name);
        return item ? item->mapToScene(item->boundingRect().center()).toPoint() : QPoint(-1, -1);
    };

    caption = QStringLiteral("Nothing selected: the style bar sets up the next new object");
    frame(12);
    caption = QStringLiteral("Click the arrow you already drew");
    moveTo((kArrowStart + kArrowEnd) / 2);
    frame(2, true);
    scene.m_layer.setSelectedIndex(0);
    caption = QStringLiteral("The style bar now says Selected: Arrow");
    frame(14);

    caption = QStringLiteral("Pick a colour: it changes this arrow");
    const QPoint swatch = centerOf(QStringLiteral("colorSwatch_1"));
    QVERIFY(swatch.x() >= 0);
    moveTo(swatch);
    frame(2, true);
    QTest::mouseClick(&scene.m_view, Qt::LeftButton, Qt::NoModifier, swatch);
    QTRY_COMPARE(static_cast<ArrowAnnotation*>(scene.m_layer.itemAt(0))->color(),
                 scene.m_options.colorPalette().at(1).value<QColor>());
    frame(10);

    caption = QStringLiteral("Make it thicker: one undo step, however far you drag");
    for (int w : {6, 8, 10, 12}) {
        scene.m_options.handleWidthChanged(w);
        frame(2);
    }
    QTest::qWait(AnnotationStyleEditor::kCoalesceIdleMs + 50);
    frame(8);

    caption = QStringLiteral("Ctrl/Cmd+Z undoes the width, then the colour");
    scene.m_layer.undo();
    frame(10);
    scene.m_layer.undo();
    frame(10);
    caption = QStringLiteral("Redo brings the colour back");
    scene.m_layer.redo();
    frame(10);
    caption = QStringLiteral("Click empty space: back to New object; tool settings unchanged");
    moveTo(QPoint(820, 120));
    frame(2, true);
    scene.m_layer.clearSelection();
    frame(16);
    QVERIFY(count > 60);
}

// #237: moving and rotating an annotation are undo steps of their own,
// independent of style; Esc during a drag puts it back. The drags go through
// the real ShapeAnnotationEditor, the same code all three surfaces use.
void tst_ToolbarEvidence::geometryUndoVideo()
{
    const QString base = qEnvironmentVariable("SNAPTRAY_TOOLBAR_VIDEO_DIR");
    if (base.isEmpty()) QSKIP("Set SNAPTRAY_TOOLBAR_VIDEO_DIR to record the operation video frames");
    const QString out = base + QStringLiteral("/geometry-undo");
    const auto originalStyle = ToolbarStyleConfig::loadStyle();
    auto restoreTheme = qScopeGuard([originalStyle] {
        ToolbarStyleConfig::saveStyle(originalStyle);
        DesignSystem::instance().refreshTheme();
    });
    setTheme(false);
    QDir().mkpath(out);
    QTemporaryDir tmp;
    Scene scene(tmp.path(), kNormalSize);
    QVERIFY(scene.open());
    ShapeAnnotationEditor editor;
    editor.setAnnotationLayer(&scene.m_layer);
    auto* shape = static_cast<ShapeAnnotation*>(scene.m_layer.itemAt(1));
    const QRectF startRect = shape->rectF();

    int count = 0;
    QPoint cursor(700, 450);
    bool pressed = false;
    QString caption;
    const auto frame = [&](int hold = 1) {
        scene.renderBackground();
        QImage image = scene.grab();
        const qreal dpr = image.devicePixelRatio();
        image = image.scaledToWidth(qRound(image.width() / dpr), Qt::SmoothTransformation);
        image.setDevicePixelRatio(1.0);
        QImage framed(image.width(), image.height() + kCaption, QImage::Format_RGB32);
        framed.fill(QColor("#101114"));
        QPainter painter(&framed);
        painter.drawImage(0, 0, image);
        painter.setRenderHint(QPainter::Antialiasing);
        Scene::drawCursor(painter, cursor, pressed);
        QFont font = painter.font();
        font.setPixelSize(18);
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(QColor("#f2f2f2"));
        painter.drawText(QRect(16, image.height(), image.width() - 32, kCaption), Qt::AlignVCenter, caption);
        painter.end();
        for (int i = 0; i < hold; ++i)
            framed.save(QStringLiteral("%1/frame-%2.png").arg(out).arg(count++, 5, 10, QLatin1Char('0')));
    };
    const auto moveTo = [&](const QPoint& to, int steps = 8) {
        const QPoint from = cursor;
        for (int i = 1; i <= steps; ++i) {
            cursor = from + (to - from) * i / steps;
            frame();
        }
    };
    // A drag of the selected shape's body through the editor.
    const auto drag = [&](const QPoint& to, bool escape) {
        pressed = true;
        editor.startDragging(cursor);
        frame(2);
        const QPoint from = cursor;
        for (int i = 1; i <= 12; ++i) {
            cursor = from + (to - from) * i / 12;
            editor.updateDragging(cursor);
            frame();
        }
        if (escape) {
            caption = QStringLiteral("Esc during the drag: the rectangle goes back");
            editor.cancelInteraction();
            frame(14);
        } else {
            editor.finishDragging();
        }
        pressed = false;
        frame(8);
    };

    caption = QStringLiteral("#237 · Select the rectangle");
    frame(10);
    moveTo(startRect.center().toPoint());
    scene.m_layer.setSelectedIndex(1);
    frame(10);
    caption = QStringLiteral("Drag it: one undo step");
    drag(startRect.center().toPoint() + QPoint(-150, 120), false);
    caption = QStringLiteral("Rotate it: another step");
    const QPoint center = shape->center().toPoint();
    pressed = true;
    editor.startTransformation(center + QPoint(90, 0), GizmoHandle::Rotation);
    for (int i = 1; i <= 12; ++i) {
        const qreal a = qDegreesToRadians(2.5 * i);
        cursor = center + QPoint(qRound(90 * qCos(a)), qRound(90 * qSin(a)));
        editor.updateTransformation(cursor);
        frame();
    }
    editor.finishTransformation();
    pressed = false;
    frame(8);
    caption = QStringLiteral("Pick a colour: a style step");
    const QPoint swatch = [&] {
        QQuickItem* item = scene.find(QStringLiteral("colorSwatch_3"));
        return item ? item->mapToScene(item->boundingRect().center()).toPoint() : QPoint(-1, -1);
    }();
    QVERIFY(swatch.x() >= 0);
    moveTo(swatch);
    QTest::mouseClick(&scene.m_view, Qt::LeftButton, Qt::NoModifier, swatch);
    QTRY_VERIFY(shape->color() == scene.m_options.colorPalette().at(3).value<QColor>());
    QTest::qWait(AnnotationStyleEditor::kCoalesceIdleMs + 50);
    frame(10);
    caption = QStringLiteral("Undo: the colour comes back, the rotation stays");
    scene.m_layer.undo();
    frame(14);
    caption = QStringLiteral("Undo: the rotation comes back, the position stays");
    scene.m_layer.undo();
    QCOMPARE(shape->rotation(), 0.0);
    frame(14);
    caption = QStringLiteral("Undo: back to where it was drawn");
    scene.m_layer.undo();
    QCOMPARE(shape->rectF(), startRect);
    frame(14);
    caption = QStringLiteral("Redo: moved again");
    scene.m_layer.redo();
    frame(14);
    caption = QStringLiteral("Start another drag…");
    moveTo(shape->center().toPoint());
    drag(shape->center().toPoint() + QPoint(200, -40), true);
    QCOMPARE(shape->rectF(), startRect.translated(-150, 120));
    QVERIFY(count > 80);
}

QTEST_MAIN(tst_ToolbarEvidence)
#include "tst_ToolbarEvidence.moc"
