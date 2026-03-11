#ifndef CAPTURESHORTCUTHINTSOVERLAY_H
#define CAPTURESHORTCUTHINTSOVERLAY_H

#include <QFontMetrics>
#include <QPair>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

class QPainter;

class CaptureShortcutHintsOverlay
{
public:
    struct LayoutMetrics {
        int keyColumnWidth = 0;
        int textColumnWidth = 0;
        int rowHeight = 0;
        int panelWidth = 0;
        int panelHeight = 0;
    };

    CaptureShortcutHintsOverlay() = default;

    void draw(QPainter& painter, const QSize& viewportSize) const;

    QRect panelRectForViewport(const QSize& viewportSize) const;
    int rowCount() const;
    LayoutMetrics layoutMetrics() const;

private:
    int keyCapWidth(const QString& keyText, const QFontMetrics& fm) const;
    int keyGroupWidth(const QStringList& keys, const QFontMetrics& fm) const;
    void drawKeyGroup(QPainter& painter,
                      int x,
                      int centerY,
                      const QStringList& keys,
                      const QFontMetrics& keyFm,
                      int keyCapHeight) const;
    QVector<QPair<QStringList, QString>> hintRows() const;
};

#endif // CAPTURESHORTCUTHINTSOVERLAY_H
