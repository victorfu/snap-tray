#include "region/CaptureShortcutHintsOverlay.h"

#include <QCoreApplication>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPen>

namespace {
constexpr int kPanelMargin = 12;
constexpr int kPanelCornerRadius = 14;
constexpr int kPanelPaddingX = 14;
constexpr int kPanelPaddingY = 12;
constexpr int kColumnGap = 16;
constexpr int kRowGap = 8;
constexpr int kKeyCapPaddingX = 9;
constexpr int kKeyCapHeight = 28;
constexpr int kKeyCapRadius = 7;
constexpr int kKeyCapMinWidth = 30;
constexpr int kPlusSpacing = 6;

constexpr QRgb kPanelBg = qRgba(52, 54, 60, 220);
constexpr QRgb kPanelBorder = qRgba(255, 255, 255, 58);
constexpr QRgb kKeyCapBg = qRgba(75, 77, 84, 228);
constexpr QRgb kKeyCapBorder = qRgba(235, 235, 235, 165);
constexpr QRgb kKeyText = qRgba(250, 250, 250, 255);
constexpr QRgb kDescriptionText = qRgba(244, 244, 244, 255);
constexpr QRgb kPlusText = qRgba(210, 210, 210, 230);

QString trOverlay(const char* text)
{
    return QCoreApplication::translate("CaptureShortcutHintsOverlay", text);
}
} // namespace

QVector<QPair<QStringList, QString>> CaptureShortcutHintsOverlay::hintRows() const
{
    return {
        {QStringList{QStringLiteral("Esc")}, trOverlay("Cancel capture")},
        {QStringList{QStringLiteral("Enter")}, trOverlay("Confirm selection (after selection)")},
        {QStringList{QStringLiteral("M")}, trOverlay("Toggle multi-region mode")},
        {QStringList{QStringLiteral("Shift")}, trOverlay("Switch RGB/HEX (when magnifier visible)")},
        {QStringList{QStringLiteral("C")}, trOverlay("Copy color value (before selection)")},
        {QStringList{QStringLiteral("Arrow")}, trOverlay("Move selection by 1 pixel (after selection)")},
        {QStringList{QStringLiteral("Shift"), QStringLiteral("Arrow")},
            trOverlay("Resize selection by 1 pixel (after selection)")}
    };
}

int CaptureShortcutHintsOverlay::rowCount() const
{
    return hintRows().size();
}

int CaptureShortcutHintsOverlay::keyCapWidth(const QString& keyText, const QFontMetrics& fm) const
{
    return qMax(kKeyCapMinWidth, fm.horizontalAdvance(keyText) + kKeyCapPaddingX * 2);
}

int CaptureShortcutHintsOverlay::keyGroupWidth(const QStringList& keys, const QFontMetrics& fm) const
{
    if (keys.isEmpty()) {
        return 0;
    }

    int width = 0;
    for (int i = 0; i < keys.size(); ++i) {
        width += keyCapWidth(keys.at(i), fm);
        if (i + 1 < keys.size()) {
            width += fm.horizontalAdvance(QStringLiteral("+")) + (kPlusSpacing * 2);
        }
    }
    return width;
}

CaptureShortcutHintsOverlay::LayoutMetrics CaptureShortcutHintsOverlay::layoutMetrics() const
{
    QFont keyFont;
    keyFont.setPointSize(11);
    keyFont.setBold(true);
    QFont textFont;
    textFont.setPointSize(11);

    const QFontMetrics keyFm(keyFont);
    const QFontMetrics textFm(textFont);

    LayoutMetrics metrics;
    const auto rows = hintRows();
    for (const auto& row : rows) {
        metrics.keyColumnWidth = qMax(metrics.keyColumnWidth, keyGroupWidth(row.first, keyFm));
        metrics.textColumnWidth = qMax(metrics.textColumnWidth, textFm.horizontalAdvance(row.second));
    }

    metrics.rowHeight = qMax(kKeyCapHeight, textFm.height());
    metrics.panelWidth = kPanelPaddingX * 2 + metrics.keyColumnWidth + kColumnGap + metrics.textColumnWidth;
    metrics.panelHeight = kPanelPaddingY * 2
        + (metrics.rowHeight * rows.size())
        + (kRowGap * qMax(0, rows.size() - 1));
    return metrics;
}

QRect CaptureShortcutHintsOverlay::panelRectForViewport(const QSize& viewportSize) const
{
    const LayoutMetrics metrics = layoutMetrics();
    if (viewportSize.isEmpty() || metrics.panelWidth <= 0 || metrics.panelHeight <= 0) {
        return QRect();
    }

    const int width = qMin(metrics.panelWidth, qMax(1, viewportSize.width() - kPanelMargin * 2));
    const int height = qMin(metrics.panelHeight, qMax(1, viewportSize.height() - kPanelMargin * 2));

    const int x = qMax(0, qMin(kPanelMargin, viewportSize.width() - width));
    const int y = qMax(0, viewportSize.height() - height - kPanelMargin);

    return QRect(x, y, width, height);
}

void CaptureShortcutHintsOverlay::drawKeyGroup(QPainter& painter,
                                               int x,
                                               int centerY,
                                               const QStringList& keys,
                                               const QFontMetrics& keyFm,
                                               int keyCapHeight) const
{
    int cursorX = x;
    for (int i = 0; i < keys.size(); ++i) {
        const QString keyText = keys.at(i);
        const int width = keyCapWidth(keyText, keyFm);
        const QRect keyRect(cursorX, centerY - keyCapHeight / 2, width, keyCapHeight);

        painter.setPen(QPen(QColor::fromRgba(kKeyCapBorder), 1));
        painter.setBrush(QColor::fromRgba(kKeyCapBg));
        painter.drawRoundedRect(keyRect, kKeyCapRadius, kKeyCapRadius);

        painter.setPen(QColor::fromRgba(kKeyText));
        painter.drawText(keyRect, Qt::AlignCenter, keyText);

        cursorX += width;
        if (i + 1 < keys.size()) {
            cursorX += kPlusSpacing;
            const int plusWidth = keyFm.horizontalAdvance(QStringLiteral("+"));
            const QRect plusRect(cursorX, centerY - keyFm.height() / 2, plusWidth, keyFm.height());
            painter.setPen(QColor::fromRgba(kPlusText));
            painter.drawText(plusRect, Qt::AlignCenter, QStringLiteral("+"));
            cursorX += plusWidth + kPlusSpacing;
        }
    }
}

void CaptureShortcutHintsOverlay::draw(QPainter& painter, const QSize& viewportSize) const
{
    if (viewportSize.isEmpty()) {
        return;
    }

    const QRect panelRect = panelRectForViewport(viewportSize);
    if (!panelRect.isValid()) {
        return;
    }

    QFont keyFont = painter.font();
    keyFont.setPointSize(11);
    keyFont.setBold(true);
    QFont textFont = painter.font();
    textFont.setPointSize(11);
    textFont.setBold(false);

    const QFontMetrics keyFm(keyFont);
    const QFontMetrics textFm(textFont);
    const LayoutMetrics metrics = layoutMetrics();
    const auto rows = hintRows();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.setPen(QPen(QColor::fromRgba(kPanelBorder), 1));
    painter.setBrush(QColor::fromRgba(kPanelBg));
    painter.drawRoundedRect(panelRect, kPanelCornerRadius, kPanelCornerRadius);

    int rowY = panelRect.top() + kPanelPaddingY;
    for (const auto& row : rows) {
        const int rowCenterY = rowY + metrics.rowHeight / 2;
        const int keyStartX = panelRect.left() + kPanelPaddingX;
        painter.setFont(keyFont);
        drawKeyGroup(painter, keyStartX, rowCenterY, row.first, keyFm, kKeyCapHeight);

        const int descX = keyStartX + metrics.keyColumnWidth + kColumnGap;
        const QRect descRect(descX,
            rowY + (metrics.rowHeight - textFm.height()) / 2,
            panelRect.right() - descX - kPanelPaddingX + 1,
            textFm.height());
        painter.setFont(textFont);
        painter.setPen(QColor::fromRgba(kDescriptionText));
        painter.drawText(descRect, Qt::AlignVCenter | Qt::AlignLeft, row.second);

        rowY += metrics.rowHeight + kRowGap;
    }

    painter.restore();
}
