#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "PinWindow.h"
#include "PinWindowManager.h"
#include "pinwindow/RegionLayoutManager.h"
#include "pinwindow/PinHistoryWindow.h"

namespace {
constexpr auto kEnvPinHistoryPath = "SNAPTRAY_PIN_HISTORY_DIR";
}

class TestPinHistoryWindow : public QObject
{
    Q_OBJECT

private:
    bool m_hadOriginalEnv = false;
    QByteArray m_originalEnvValue;

    static bool createImageFile(const QString& path, const QSize& size, const QColor& color)
    {
        QImage image(size, QImage::Format_ARGB32);
        image.fill(color);
        return image.save(path);
    }

    static void setHistoryPath(const QString& path)
    {
        qputenv(kEnvPinHistoryPath, path.toLocal8Bit());
    }

    static int countCachedImages(const QString& folderPath)
    {
        QDir dir(folderPath);
        return dir.entryList(QStringList() << "cache_*.png", QDir::Files).size();
    }

private slots:
    void init()
    {
        m_hadOriginalEnv = qEnvironmentVariableIsSet(kEnvPinHistoryPath);
        if (m_hadOriginalEnv) {
            m_originalEnvValue = qgetenv(kEnvPinHistoryPath);
        }
    }

    void cleanup()
    {
        if (m_hadOriginalEnv) {
            qputenv(kEnvPinHistoryPath, m_originalEnvValue);
        }
        else {
            qunsetenv(kEnvPinHistoryPath);
        }
    }

    void testEmptyStateShownWhenNoCache()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        setHistoryPath(tempDir.path());

        PinWindowManager manager;
        QPointer<PinHistoryWindow> window = new PinHistoryWindow(&manager);
        window->show();
        QCoreApplication::processEvents();

        auto* listWidget = window->findChild<QListWidget*>("pinHistoryListWidget");
        auto* emptyLabel = window->findChild<QLabel*>("pinHistoryEmptyLabel");
        QVERIFY(listWidget);
        QVERIFY(emptyLabel);
        QCOMPARE(listWidget->count(), 0);
        QVERIFY(emptyLabel->isVisible());

        window->close();
        QTRY_VERIFY(window.isNull());
    }

    void testClickItemRepinsAndClosesWindow()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        setHistoryPath(tempDir.path());

        const QString imagePath = QDir(tempDir.path()).filePath("cache_20260101_120001_000.png");
        QVERIFY(createImageFile(imagePath, QSize(320, 180), Qt::green));

        PinWindowManager manager;
        QSignalSpy createdSpy(&manager, &PinWindowManager::windowCreated);

        QPointer<PinHistoryWindow> window = new PinHistoryWindow(&manager);
        window->show();
        QCoreApplication::processEvents();

        auto* listWidget = window->findChild<QListWidget*>("pinHistoryListWidget");
        QVERIFY(listWidget);
        QCOMPARE(listWidget->count(), 1);

        QListWidgetItem* firstItem = listWidget->item(0);
        QVERIFY(firstItem);
        const QPoint clickPos = listWidget->visualItemRect(firstItem).center();
        QTest::mouseClick(listWidget->viewport(), Qt::LeftButton, Qt::NoModifier, clickPos);

        QTRY_COMPARE(createdSpy.count(), 1);
        QTRY_VERIFY(window.isNull());
        manager.closeAllWindows();
    }

    void testDeleteKeepsWindowOpen()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        setHistoryPath(tempDir.path());

        const QString firstImage = QDir(tempDir.path()).filePath("cache_20260101_120000_000.png");
        const QString secondImage = QDir(tempDir.path()).filePath("cache_20260101_120001_000.png");
        QVERIFY(createImageFile(firstImage, QSize(160, 90), Qt::red));
        QVERIFY(createImageFile(secondImage, QSize(200, 120), Qt::blue));

        PinWindowManager manager;
        QPointer<PinHistoryWindow> window = new PinHistoryWindow(&manager);
        window->show();
        QCoreApplication::processEvents();

        auto* listWidget = window->findChild<QListWidget*>("pinHistoryListWidget");
        QVERIFY(listWidget);
        QCOMPARE(listWidget->count(), 2);

        listWidget->setCurrentRow(0);
        QVERIFY(QMetaObject::invokeMethod(window.data(), "deleteSelectedEntry", Qt::DirectConnection));

        QVERIFY(window);
        QVERIFY(window->isVisible());
        QCOMPARE(listWidget->count(), 1);
        QCOMPARE(countCachedImages(tempDir.path()), 1);

        window->close();
        QTRY_VERIFY(window.isNull());
    }

    void testRepinRestoresDprAndMultiRegionData()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        setHistoryPath(tempDir.path());

        const QString basePath = QDir(tempDir.path()).filePath("cache_20260101_120001_000");
        const QString imagePath = basePath + ".png";
        const QString metadataPath = basePath + ".snpr";
        const QString dprPath = basePath + ".snpd";

        QVERIFY(createImageFile(imagePath, QSize(320, 180), Qt::green));

        LayoutRegion region;
        region.rect = QRect(0, 0, 100, 80);
        region.originalRect = region.rect;
        region.image = QImage(100, 80, QImage::Format_ARGB32);
        region.image.fill(Qt::green);
        region.color = Qt::green;
        region.index = 1;

        const QByteArray regionData = RegionLayoutManager::serializeRegions(QVector<LayoutRegion>{region});
        QFile metadataFile(metadataPath);
        QVERIFY(metadataFile.open(QIODevice::WriteOnly));
        metadataFile.write(regionData);
        metadataFile.close();

        QFile dprFile(dprPath);
        QVERIFY(dprFile.open(QIODevice::WriteOnly | QIODevice::Text));
        dprFile.write("2.0");
        dprFile.close();

        PinWindowManager manager;
        PinWindow* createdWindow = nullptr;
        connect(&manager, &PinWindowManager::windowCreated, this, [&createdWindow](PinWindow* window) {
            createdWindow = window;
        });

        QPointer<PinHistoryWindow> historyWindow = new PinHistoryWindow(&manager);
        historyWindow->show();
        QCoreApplication::processEvents();

        auto* listWidget = historyWindow->findChild<QListWidget*>("pinHistoryListWidget");
        QVERIFY(listWidget);
        QCOMPARE(listWidget->count(), 1);

        QListWidgetItem* firstItem = listWidget->item(0);
        QVERIFY(firstItem);
        const QPoint clickPos = listWidget->visualItemRect(firstItem).center();
        QTest::mouseClick(listWidget->viewport(), Qt::LeftButton, Qt::NoModifier, clickPos);

        QTRY_VERIFY(createdWindow != nullptr);
        QCOMPARE(createdWindow->size(), QSize(160, 90));
        QVERIFY(createdWindow->hasMultiRegionData());
        QTRY_VERIFY(historyWindow.isNull());

        manager.closeAllWindows();
    }

    void testOpenCacheFolderButtonKeepsWindowOpen()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        setHistoryPath(tempDir.path());

        PinWindowManager manager;
        QPointer<PinHistoryWindow> window = new PinHistoryWindow(&manager);
        window->show();
        QCoreApplication::processEvents();

        auto* openCacheFolderButton = window->findChild<QPushButton*>("openCacheFolderButton");
        QVERIFY(openCacheFolderButton);

        QTest::mouseClick(openCacheFolderButton, Qt::LeftButton);
        QCoreApplication::processEvents();

        QVERIFY(window);
        QVERIFY(window->isVisible());

        window->close();
        QTRY_VERIFY(window.isNull());
    }

    void testEscapeClosesWindow()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        setHistoryPath(tempDir.path());

        PinWindowManager manager;
        QPointer<PinHistoryWindow> window = new PinHistoryWindow(&manager);
        window->show();
        QCoreApplication::processEvents();

        QTest::keyClick(window.data(), Qt::Key_Escape);
        QTRY_VERIFY(window.isNull());
    }
};

QTEST_MAIN(TestPinHistoryWindow)
#include "tst_PinHistoryWindow.moc"
