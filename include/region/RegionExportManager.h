#ifndef REGIONEXPORTMANAGER_H
#define REGIONEXPORTMANAGER_H

#include <QObject>
#include <QPixmap>
#include <QRect>
#include <QPoint>

class AnnotationLayer;

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
    explicit RegionExportManager(QObject *parent = nullptr);

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
     * @brief Copy selection to clipboard
     * @param selectionRect The selection rectangle in logical coordinates
     * @param cornerRadius Corner radius for rounded corners
     */
    void copyToClipboard(const QRect &selectionRect, int cornerRadius = 0);

    /**
     * @brief Show save dialog and save to file
     * @param selectionRect The selection rectangle in logical coordinates
     * @param cornerRadius Corner radius for rounded corners
     * @param parentWidget Widget to use as dialog parent (nullptr for no parent)
     * @return true if saved successfully, false if cancelled or failed
     */
    bool saveToFile(const QRect &selectionRect, int cornerRadius = 0, QWidget *parentWidget = nullptr);

signals:
    /**
     * @brief Emitted when screenshot is copied to clipboard
     */
    void copyCompleted(const QPixmap &pixmap);

    /**
     * @brief Emitted when screenshot is saved to file
     */
    void saveCompleted(const QPixmap &pixmap, const QString &filePath);

    /**
     * @brief Emitted before showing save dialog (so parent can hide)
     */
    void saveDialogOpening();

    /**
     * @brief Emitted after save dialog closes (so parent can restore)
     * @param saved true if file was saved, false if cancelled
     */
    void saveDialogClosed(bool saved);

private:
    /**
     * @brief Apply rounded corner mask to pixmap
     */
    QPixmap applyRoundedCorners(const QPixmap &source, int radius, const QRect &logicalRect);

    QPixmap m_backgroundPixmap;
    qreal m_devicePixelRatio = 1.0;
    AnnotationLayer *m_annotationLayer = nullptr;
};

#endif // REGIONEXPORTMANAGER_H
