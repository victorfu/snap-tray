#ifndef REGIONEXPORTMANAGER_H
#define REGIONEXPORTMANAGER_H

#include <QObject>
#include <QImage>
#include <QPointer>
#include <QPixmap>
#include <QRect>
#include <QPoint>
#include <QString>

class AnnotationLayer;
class QFutureWatcherBase;
class QScreen;
class QWidget;

/**
 * @brief Manages screenshot export operations (copy, save, finish)
 *
 * Extracted from RegionSelector to:
 * - Isolate export logic for testability
 * - Handle device pixel ratio conversions
 * - Manage annotation rendering onto screenshots
 * - Apply rounded corner masks
 */
class RegionExportManager : public QObject
{
    Q_OBJECT

public:
    struct PreparedExport {
        QPixmap pixmap;
        QImage image;

        bool isValid() const
        {
            return !pixmap.isNull() && !image.isNull();
        }
    };

    struct SaveRequest {
        QString filePath;
        QString renderWarning;
        QString error;
        bool autoSave = false;
        bool cancelled = false;

        bool isValid() const
        {
            return error.isEmpty() && !cancelled && !filePath.isEmpty();
        }
    };

    explicit RegionExportManager(QObject *parent = nullptr);
    ~RegionExportManager() override;

    /**
     * @brief Set the background pixmap to crop from
     */
    void setBackgroundPixmap(const QPixmap &pixmap);

    /**
     * @brief Set device pixel ratio for proper scaling
     */
    void setDevicePixelRatio(qreal ratio);

    /**
     * @brief Set annotation layer for rendering onto screenshot
     */
    void setAnnotationLayer(AnnotationLayer *layer);

    /**
     * @brief Get the processed selected region
     * @param selectionRect The selection rectangle in logical coordinates
     * @param cornerRadius Corner radius for rounded corners (0 = no rounding)
     * @return Processed pixmap with annotations and optional rounded corners
     */
    QPixmap getSelectedRegion(const QRect &selectionRect, int cornerRadius = 0);

    /**
     * @brief Prepare a normalized export payload for clipboard/save operations
     */
    PreparedExport prepareExport(const QRect &selectionRect, int cornerRadius = 0);

    /**
     * @brief Resolve the output target for a save request
     * @param selectionRect The selection rectangle in logical coordinates
     * @param parentWidget Widget to use as dialog parent (nullptr for no parent)
     * @return Save target metadata for async save execution
     */
    SaveRequest createSaveRequest(const QRect &selectionRect, QWidget *parentWidget = nullptr) const;

    /**
     * @brief Save a prepared export asynchronously
     */
    void savePreparedExportAsync(PreparedExport prepared,
                                 const QString& filePath,
                                 const QString& renderWarning = QString());

    void setMonitorIdentifier(const QString& monitor);
    void setWindowMetadata(const QString& windowTitle, const QString& appName = QString());
    void setRegionIndex(int regionIndex);
    void setSourceScreen(QScreen* screen);

signals:
    /**
     * @brief Emitted when screenshot is saved to file
     */
    void saveCompleted(const QPixmap &pixmap, const QImage& image, const QString &filePath);

    /**
     * @brief Emitted when auto-save fails
     */
    void saveFailed(const QString &filePath, const QString &error);

private:
    /**
     * @brief Apply rounded corner mask to pixmap
     */
    QPixmap applyRoundedCorners(const QPixmap &source, int radius, const QRect &logicalRect);

    QPixmap m_backgroundPixmap;
    qreal m_devicePixelRatio = 1.0;
    AnnotationLayer *m_annotationLayer = nullptr;
    QString m_monitorIdentifier;
    QString m_windowTitle;
    QString m_appName;
    int m_regionIndex = -1;
    QPointer<QScreen> m_sourceScreen;
    QPointer<QFutureWatcherBase> m_saveWatcher;
};

#endif // REGIONEXPORTMANAGER_H
