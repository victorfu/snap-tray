#include "colorwidgets/ColorPickerDialogCompat.h"

#include "colorwidgets/StyledColorDialog.h"

#include <QKeyEvent>
#include <QScreen>
#include <QGuiApplication>

namespace snaptray {
namespace colorwidgets {

ColorPickerDialogCompat::ColorPickerDialogCompat(QWidget* parent) : QWidget(parent)
{
    // Make this widget invisible - it's just a wrapper
    setFixedSize(0, 0);
    setupInternalDialog();
}

ColorPickerDialogCompat::~ColorPickerDialogCompat()
{
    if (m_dialog) {
        m_dialog->deleteLater();
    }
}

void ColorPickerDialogCompat::setupInternalDialog()
{
    m_dialog = new StyledColorDialog(m_currentColor, nullptr);
    m_dialog->setWindowTitle(tr("Select Color"));
    m_dialog->setShowAlpha(false);  // Match original behavior

    // Forward signals
    connect(m_dialog, &StyledColorDialog::colorSelected, this, [this](const QColor& color) {
        m_currentColor = color;
        emit colorSelected(color);
        hide();
    });

    connect(m_dialog, &QDialog::rejected, this, [this]() {
        emit cancelled();
        hide();
    });
}

void ColorPickerDialogCompat::setCurrentColor(const QColor& color)
{
    m_currentColor = color;
    if (m_dialog) {
        m_dialog->setColor(color);
    }
}

QColor ColorPickerDialogCompat::currentColor() const
{
    return m_dialog ? m_dialog->color() : m_currentColor;
}

void ColorPickerDialogCompat::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    if (m_dialog) {
        positionDialog();
        m_dialog->show();
        m_dialog->raise();
        m_dialog->activateWindow();
    }
}

void ColorPickerDialogCompat::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);

    if (m_dialog) {
        m_dialog->hide();
    }
}

void ColorPickerDialogCompat::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        emit cancelled();
        hide();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void ColorPickerDialogCompat::positionDialog()
{
    if (!m_dialog)
        return;

    // Position the dialog centered on the primary screen or parent
    QScreen* screen = QGuiApplication::primaryScreen();
    if (parentWidget()) {
        screen = parentWidget()->screen();
    }

    if (screen) {
        QRect screenGeometry = screen->availableGeometry();
        QSize dialogSize = m_dialog->sizeHint();

        int x = screenGeometry.x() + (screenGeometry.width() - dialogSize.width()) / 2;
        int y = screenGeometry.y() + (screenGeometry.height() - dialogSize.height()) / 2;

        m_dialog->move(x, y);
    }
}

}  // namespace colorwidgets
}  // namespace snaptray
