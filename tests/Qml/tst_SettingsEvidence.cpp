// Screenshots of the task-grouped Settings window and the Beautify panel
// (#227), for docs/developer/ui-design-guidelines.md. Runs only when its
// variables are set:
//   SNAPTRAY_SETTINGS_DESIGN_DIR    screenshots
//   SNAPTRAY_SETTINGS_TRANSLATION   a .qm to load (language added to the names)
#include <QtTest>
#include <QFileInfo>
#include <QPainter>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QScopeGuard>
#include <QTranslator>
#include <QtQml/qqmlextensionplugin.h>

#include "ToolbarStyle.h"
#include "qml/BeautifyPanelBackend.h"
#include "qml/QmlBeautifyPanel.h"
#include "qml/QmlOverlayManager.h"
#include "qml/SettingsBackend.h"
#include "settings/Settings.h"
#include "ui/DesignSystem.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

namespace {

QPixmap demoScreenshot()
{
    QImage image(QSize(960, 600), QImage::Format_ARGB32_Premultiplied);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(image.rect(), QColor("#fbfbfd"));
    p.fillRect(QRect(0, 0, image.width(), 48), QColor("#5b6cff"));
    p.setPen(Qt::white);
    QFont font = p.font();
    font.setPixelSize(22);
    font.setBold(true);
    p.setFont(font);
    p.drawText(QRect(24, 0, image.width(), 48), Qt::AlignVCenter, QStringLiteral("Release checklist"));
    p.setPen(Qt::NoPen);
    for (int i = 0; i < 9; ++i) {
        p.setBrush(QColor("#d1d4dc"));
        p.drawRoundedRect(QRectF(24, 80 + i * 52, image.width() * (0.4 + 0.07 * (i % 4)), 18), 9, 9);
    }
    p.setBrush(QColor("#c7cdfd"));
    p.drawRoundedRect(QRectF(600, 96, 320, 260), 14, 14);
    return QPixmap::fromImage(image);
}

void setTheme(bool dark)
{
    ToolbarStyleConfig::saveStyle(dark ? ToolbarStyleType::Dark : ToolbarStyleType::Light);
    DesignSystem::instance().refreshTheme();
}

} // namespace

class tst_SettingsEvidence : public QObject
{
    Q_OBJECT

private slots:
    void screenshots_data();
    void screenshots();
};

void tst_SettingsEvidence::screenshots_data()
{
    QTest::addColumn<bool>("dark");
    QTest::newRow("light") << false;
    QTest::newRow("dark") << true;
}

void tst_SettingsEvidence::screenshots()
{
    const QString out = qEnvironmentVariable("SNAPTRAY_SETTINGS_DESIGN_DIR");
    if (out.isEmpty()) QSKIP("Set SNAPTRAY_SETTINGS_DESIGN_DIR to capture Settings and Beautify screenshots");
    QFETCH(bool, dark);
    QString suffix = dark ? QStringLiteral("dark") : QStringLiteral("light");
    QTranslator translator;
    const QString qm = qEnvironmentVariable("SNAPTRAY_SETTINGS_TRANSLATION");
    if (!qm.isEmpty()) {
        QVERIFY2(translator.load(qm), qPrintable(qm));
        QCoreApplication::installTranslator(&translator);
        suffix += QLatin1Char('-') + QFileInfo(qm).completeBaseName().section(QLatin1Char('_'), 1);
    }
    auto removeTranslator = qScopeGuard([&translator] { QCoreApplication::removeTranslator(&translator); });
    const auto savedStyle = ToolbarStyleConfig::loadStyle();
    auto restoreStyle = qScopeGuard([savedStyle] {
        ToolbarStyleConfig::saveStyle(savedStyle);
        DesignSystem::instance().refreshTheme();
    });
    setTheme(dark);
    const auto save = [&](QWindow* window, const QString& name) {
        auto* view = qobject_cast<QQuickView*>(window);
        QVERIFY(view);
        // Keep the real pointer off the sidebar so no item looks hovered.
        QTest::mouseMove(view, QPoint(view->width() - 4, view->height() - 4));
        QTest::qWait(700); // sidebar and switch animations settle
        const QImage image = view->grabWindow();
        QVERIFY(!image.isNull());
        QVERIFY(image.save(QDir(out).filePath(name + QLatin1Char('-') + suffix + QStringLiteral(".png"))));
    };

    // Settings: the same view setup as QmlSettingsWindow, without its window chrome.
    {
        SnapTray::SettingsBackend backend;
        QQuickView view(SnapTray::QmlOverlayManager::instance().engine(), nullptr);
        view.rootContext()->setContextProperty(QStringLiteral("settingsBackend"), &backend);
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.setSource(QUrl(QStringLiteral("qrc:/SnapTrayQml/settings/SettingsWindow.qml")));
        QCOMPARE(view.status(), QQuickView::Ready);
        view.resize(1000, 680);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QObject* root = view.rootObject();
        const auto page = [&](const QString& key, QSize size, const QString& name, bool toEnd = false) {
            QVariant opened;
            QVERIFY(QMetaObject::invokeMethod(root, "selectPage", Q_RETURN_ARG(QVariant, opened),
                                              Q_ARG(QVariant, key)));
            QVERIFY(opened.toBool());
            view.resize(size);
            QTRY_COMPARE(view.size(), size);
            if (toEnd) {
                auto* loader = root->findChild<QObject*>(QStringLiteral("settingsPageLoader_") + key);
                QVERIFY(loader);
                QObject* flickable = nullptr;
                QTRY_VERIFY((flickable = loader->property("item").value<QObject*>()) != nullptr);
                QTRY_VERIFY(flickable->property("contentHeight").toReal() > flickable->property("height").toReal());
                QTest::qWait(400); // the page finishes laying out before scrolling to its end
                flickable->setProperty("contentY", flickable->property("contentHeight").toReal()
                                                       - flickable->property("height").toReal());
            }
            save(&view, name);
        };
        page(QStringLiteral("capture"), QSize(1000, 680), QStringLiteral("01-settings-capture"));
        page(QStringLiteral("saving"), QSize(1000, 680), QStringLiteral("02-settings-saving"), true);
        page(QStringLiteral("pins"), QSize(860, 560), QStringLiteral("03-settings-pins-minimum"));

        // Search: "watermark", the Opacity result opened, changed from its default.
        const int savedOpacity = backend.watermarkOpacity();
        auto restoreOpacity = qScopeGuard([&backend, savedOpacity] { backend.setWatermarkOpacity(savedOpacity); });
        backend.setWatermarkOpacity(70);
        view.resize(1000, 680);
        QTRY_COMPARE(view.size(), QSize(1000, 680));
        auto* input = root->findChild<QObject*>(QStringLiteral("settingsSearchInput"));
        QVERIFY(input);
        input->setProperty("text", QStringLiteral("watermark"));
        QVariant revealed;
        QVERIFY(QMetaObject::invokeMethod(root, "revealSetting", Q_RETURN_ARG(QVariant, revealed),
                                          Q_ARG(QVariant, QStringLiteral("saving")),
                                          Q_ARG(QVariant, QStringLiteral("watermarkOpacity"))));
        QVERIFY(revealed.toBool());
        QTest::qWait(250); // scrolled, highlight still on
        auto* viewPtr = &view;
        QTest::mouseMove(viewPtr, QPoint(view.width() - 4, view.height() - 4));
        const QImage searchShot = view.grabWindow();
        QVERIFY(searchShot.save(QDir(out).filePath(QStringLiteral("06-settings-search-") + suffix + QStringLiteral(".png"))));
        input->setProperty("text", QString());
    }

    // Beautify: collapsed, then with Customize open.
    {
        SnapTray::QmlBeautifyPanel panel;
        panel.setSettings(BeautifySettings{});
        panel.setSourcePixmap(demoScreenshot());
        panel.showNear(QRect(80, 80, 300, 200));
        QVERIFY(QTest::qWaitForWindowExposed(panel.window()));
        auto* view = qobject_cast<QQuickView*>(panel.window());
        QVERIFY(view && view->rootObject());
        save(view, QStringLiteral("04-beautify-styles"));

        auto* backend = panel.findChild<SnapTray::BeautifyPanelBackend*>();
        QVERIFY(backend);
        backend->applyStyle(2);
        backend->applyPreset(2);
        view->rootObject()->setProperty("customizeExpanded", true);
        QTRY_COMPARE(qreal(view->height()), view->rootObject()->height());
        save(view, QStringLiteral("05-beautify-customize"));
        view->rootObject()->setProperty("customizeExpanded", false);

        // Saved styles (kept only for the screenshot).
        auto settings = SnapTray::getSettings();
        const QVariant savedStyles = settings.value(QStringLiteral("beautify/customStyles"));
        auto restoreStyles = qScopeGuard([savedStyles] {
            auto s = SnapTray::getSettings();
            if (savedStyles.isValid())
                s.setValue(QStringLiteral("beautify/customStyles"), savedStyles);
            else
                s.remove(QStringLiteral("beautify/customStyles"));
        });
        backend->applyPreset(4);
        backend->applyStyle(0);
        QVERIFY(backend->saveCustomStyle(QStringLiteral("Release notes")));
        backend->applyPreset(1);
        backend->applyStyle(3);
        backend->setAspectRatio(static_cast<int>(BeautifyAspectRatio::Wide_16_9));
        QVERIFY(backend->saveCustomStyle(QStringLiteral("Slides")));
        backend->applyPreset(0);
        backend->applyStyle(1);
        save(view, QStringLiteral("07-beautify-saved"));
        backend->deleteCustomStyle(1);
        backend->deleteCustomStyle(0);
    }
}

QTEST_MAIN(tst_SettingsEvidence)
#include "tst_SettingsEvidence.moc"
