#ifndef SNAPTRAY_COLOR_PICKER_DIALOG_COMPAT_H
#define SNAPTRAY_COLOR_PICKER_DIALOG_COMPAT_H

#include <QColor>
#include <QWidget>

namespace snaptray {
namespace colorwidgets {

class StyledColorDialog;

/**
 * @brief ColorPickerDialogCompat - Compatibility layer for existing ColorPickerDialog
 *
 * This class provides a drop-in replacement for the original ColorPickerDialog,
 * maintaining the same API while using the new ColorWidgets implementation internally.
 *
 * Migration guide:
 * 1. Replace #include "ColorPickerDialog.h" with
 *    #include "colorwidgets/ColorPickerDialogCompat.h"
 * 2. Replace ColorPickerDialog with snaptray::colorwidgets::ColorPickerDialogCompat
 *    (or use 'using namespace snaptray::colorwidgets;')
 * 3. The API remains identical, signals/slots work the same way
 *
 * Example:
 * @code
 * // Before:
 * ColorPickerDialog* dialog = new ColorPickerDialog(this);
 * dialog->setCurrentColor(Qt::red);
 * connect(dialog, &ColorPickerDialog::colorSelected, this, &MyClass::onColorPicked);
 *
 * // After:
 * using namespace snaptray::colorwidgets;
 * ColorPickerDialogCompat* dialog = new ColorPickerDialogCompat(this);
 * dialog->setCurrentColor(Qt::red);
 * connect(dialog, &ColorPickerDialogCompat::colorSelected, this, &MyClass::onColorPicked);
 * @endcode
 */
class ColorPickerDialogCompat : public QWidget
{
    Q_OBJECT

public:
    explicit ColorPickerDialogCompat(QWidget* parent = nullptr);
    ~ColorPickerDialogCompat() override;

    // Compatible API with original ColorPickerDialog
    void setCurrentColor(const QColor& color);
    QColor currentColor() const;

signals:
    void colorSelected(const QColor& color);
    void cancelled();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void setupInternalDialog();
    void positionDialog();

    StyledColorDialog* m_dialog = nullptr;
    QColor m_currentColor = Qt::red;
};

// For easier migration, you can use this typedef
// Usage: using ColorPickerDialog = snaptray::colorwidgets::ColorPickerDialogCompat;
// Then existing code works without changes

}  // namespace colorwidgets
}  // namespace snaptray

#endif  // SNAPTRAY_COLOR_PICKER_DIALOG_COMPAT_H
