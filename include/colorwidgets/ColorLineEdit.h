#ifndef SNAPTRAY_COLOR_LINE_EDIT_H
#define SNAPTRAY_COLOR_LINE_EDIT_H

#include <QLineEdit>

namespace snaptray {
namespace colorwidgets {

class ColorLineEdit : public QLineEdit
{
    Q_OBJECT
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(bool showAlpha READ showAlpha WRITE setShowAlpha)
    Q_PROPERTY(bool previewColor READ previewColor WRITE setPreviewColor)

public:
    explicit ColorLineEdit(QWidget* parent = nullptr);

    QColor color() const;
    bool showAlpha() const;
    bool previewColor() const;

public slots:
    void setColor(const QColor& color);
    void setShowAlpha(bool show);
    void setPreviewColor(bool preview);

signals:
    void colorChanged(const QColor& color);
    void colorEdited(const QColor& color);  // Only emitted when user edits

protected:
    void paintEvent(QPaintEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void onTextEdited(const QString& text);
    void updateFromColor();

    QColor m_color = Qt::red;
    bool m_showAlpha = false;
    bool m_previewColor = true;
    bool m_updating = false;
};

}  // namespace colorwidgets
}  // namespace snaptray

#endif  // SNAPTRAY_COLOR_LINE_EDIT_H
