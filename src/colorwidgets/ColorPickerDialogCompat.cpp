#include "colorwidgets/ColorPickerDialogCompat.h"

#include "colorwidgets/StyledColorDialog.h"

#include <QKeyEvent>
#include <QScreen>
#include <QGuiApplication>

namespace snaptray {
namespace colorwidgets {

QRect ColorPickerDialogCompat::preferredPlacementBounds(const QRect& parentBounds,
                                                        const QRect& anchorBounds,
                                                        const QRect& primaryBounds)
{
    if (parentBounds.isValid()) {
        return parentBounds;
    }
    if (anchorBounds.isValid()) {
        return anchorBounds;
    }
    return primaryBounds;
}

QPoint ColorPickerDialogCompat::centeredTopLeftForBounds(const QRect& bounds, const QSize& dialogSize)
{
    if (!bounds.isValid() || !dialogSize.isValid()) {
        return {};
    }

    return {
        bounds.x() + (bounds.width() - dialogSize.width()) / 2,
        bounds.y() + (bounds.height() - dialogSize.height()) / 2
    };
}

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

void ColorPickerDialogCompat::setPlacementAnchor(const QPoint& globalPoint)
{
    m_placementAnchor = globalPoint;
    m_hasPlacementAnchor = true;
}

void ColorPickerDialogCompat::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    // Recreate dialog if it was destroyed (fixes Windows z-order corruption)
    if (!m_dialog) {
        setupInternalDialog();
    }

    if (m_dialog) {
        m_dialog->setColor(m_currentColor);  // Restore last color
        positionDialog();
        m_dialog->show();
        m_dialog->raise();
        m_dialog->activateWindow();
    }
}

void ColorPickerDialogCompat::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);

    // Destroy dialog instead of hiding to avoid Windows HWND_TOPMOST z-order corruption
    if (m_dialog) {
        m_dialog->deleteLater();
        m_dialog = nullptr;
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

    QRect parentBounds;
    if (parentWidget() && parentWidget()->screen()) {
        parentBounds = parentWidget()->screen()->availableGeometry();
    }

    QRect anchorBounds;
    if (m_hasPlacementAnchor) {
        if (QScreen* anchorScreen = QGuiApplication::screenAt(m_placementAnchor)) {
            anchorBounds = anchorScreen->availableGeometry();
        }
    }

    QRect primaryBounds;
    if (QScreen* primaryScreen = QGuiApplication::primaryScreen()) {
        primaryBounds = primaryScreen->availableGeometry();
    }

    const QRect placementBounds = preferredPlacementBounds(parentBounds, anchorBounds, primaryBounds);
    if (placementBounds.isValid()) {
        m_dialog->move(centeredTopLeftForBounds(placementBounds, m_dialog->sizeHint()));
    }
}

}  // namespace colorwidgets
}  // namespace snaptray
