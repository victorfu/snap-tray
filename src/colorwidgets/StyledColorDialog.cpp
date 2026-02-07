#include "colorwidgets/StyledColorDialog.h"

#include "platform/WindowLevel.h"
#include "utils/DialogThemeUtils.h"

#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace snaptray {
namespace colorwidgets {

StyledColorDialog::StyledColorDialog(QWidget* parent) : ColorDialog(parent)
{
    applySnapTrayStyle();
    if (m_customTitleBar) {
        setupCustomTitleBar();
    }
}

StyledColorDialog::StyledColorDialog(const QColor& initial, QWidget* parent)
    : ColorDialog(initial, parent)
{
    applySnapTrayStyle();
    if (m_customTitleBar) {
        setupCustomTitleBar();
    }
}

StyledColorDialog::~StyledColorDialog() = default;

void StyledColorDialog::applySnapTrayStyle()
{
    const SnapTray::DialogTheme::Palette palette = SnapTray::DialogTheme::paletteForToolbarStyle();

    // Note: Use border-radius: 0 for widgets that don't clip properly.
    setStyleSheet(QStringLiteral(R"(
        StyledColorDialog {
            background-color: transparent;
        }
        QWidget {
            background-color: transparent;
        }
        QLabel {
            color: %1;
            font-size: 12px;
            background-color: transparent;
        }
        QSpinBox {
            background-color: %2;
            border: 1px solid %3;
            border-radius: 0px;
            color: %4;
            padding: 4px;
        }
        QSpinBox::up-button, QSpinBox::down-button {
            background-color: %5;
            border: none;
            width: 16px;
        }
        QSpinBox::up-button:hover, QSpinBox::down-button:hover {
            background-color: %6;
        }
        QLineEdit {
            background-color: %2;
            border: 1px solid %3;
            border-radius: 0px;
            color: %4;
            padding: 4px 8px;
        }
        QPushButton {
            background-color: %7;
            border: 1px solid %8;
            border-radius: 0px;
            color: %9;
            padding: 6px 16px;
            min-width: 60px;
        }
        QPushButton:hover {
            background-color: %10;
        }
        QPushButton:pressed {
            background-color: %11;
        }
        QPushButton:disabled {
            background-color: %12;
            border-color: %13;
            color: %14;
        }
        QPushButton#okButton {
            background-color: %15;
            border: 1px solid %15;
            color: %16;
        }
        QPushButton#okButton:hover {
            background-color: %17;
            border-color: %17;
        }
        QPushButton#okButton:pressed {
            background-color: %18;
            border-color: %18;
        }
    )")
        .arg(SnapTray::DialogTheme::toCssColor(palette.textPrimary))
        .arg(SnapTray::DialogTheme::toCssColor(palette.inputBackground))
        .arg(SnapTray::DialogTheme::toCssColor(palette.inputBorder))
        .arg(SnapTray::DialogTheme::toCssColor(palette.textPrimary))
        .arg(SnapTray::DialogTheme::toCssColor(palette.buttonBackground))
        .arg(SnapTray::DialogTheme::toCssColor(palette.buttonHoverBackground))
        .arg(SnapTray::DialogTheme::toCssColor(palette.buttonBackground))
        .arg(SnapTray::DialogTheme::toCssColor(palette.controlBorder))
        .arg(SnapTray::DialogTheme::toCssColor(palette.buttonText))
        .arg(SnapTray::DialogTheme::toCssColor(palette.buttonHoverBackground))
        .arg(SnapTray::DialogTheme::toCssColor(palette.buttonPressedBackground))
        .arg(SnapTray::DialogTheme::toCssColor(palette.buttonDisabledBackground))
        .arg(SnapTray::DialogTheme::toCssColor(palette.buttonDisabledBorder))
        .arg(SnapTray::DialogTheme::toCssColor(palette.buttonDisabledText))
        .arg(SnapTray::DialogTheme::toCssColor(palette.accentBackground))
        .arg(SnapTray::DialogTheme::toCssColor(palette.successText))
        .arg(SnapTray::DialogTheme::toCssColor(palette.accentHoverBackground))
        .arg(SnapTray::DialogTheme::toCssColor(palette.accentPressedBackground)));

    // Make frameless if using custom title bar, and stay on top
    if (m_customTitleBar) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog | Qt::WindowStaysOnTopHint);
        setAttribute(Qt::WA_TranslucentBackground, true);
    }
}

void StyledColorDialog::setupCustomTitleBar()
{
    const SnapTray::DialogTheme::Palette palette = SnapTray::DialogTheme::paletteForToolbarStyle();

    // Create a custom title bar widget
    auto* titleBar = new QWidget(this);
    titleBar->setFixedHeight(32);
    titleBar->setStyleSheet(QStringLiteral(
        "background-color: %1; border-top-left-radius: 8px; border-top-right-radius: 8px;")
        .arg(SnapTray::DialogTheme::toCssColor(palette.titleBarBackground)));

    auto* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(12, 0, 8, 0);
    titleLayout->setSpacing(0);

    m_titleLabel = new QLabel(windowTitle(), titleBar);
    m_titleLabel->setStyleSheet(QStringLiteral("color: %1; font-weight: bold;")
        .arg(SnapTray::DialogTheme::toCssColor(palette.textPrimary)));
    titleLayout->addWidget(m_titleLabel);

    titleLayout->addStretch();

    m_closeButton = new QPushButton(titleBar);
    m_closeButton->setFixedSize(20, 20);
    m_closeButton->setStyleSheet(QStringLiteral(R"(
        QPushButton {
            background-color: transparent;
            border: none;
            border-radius: 10px;
            color: %1;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: %2;
        }
    )")
        .arg(SnapTray::DialogTheme::toCssColor(palette.textPrimary))
        .arg(SnapTray::DialogTheme::toCssColor(palette.closeButtonHoverBackground)));
    m_closeButton->setText("\u2715");  // Unicode X
    titleLayout->addWidget(m_closeButton);

    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::reject);

    // Insert title bar at the top
    auto* mainLayout = qobject_cast<QVBoxLayout*>(layout());
    if (mainLayout) {
        mainLayout->insertWidget(0, titleBar);
    }
}

void StyledColorDialog::showEvent(QShowEvent* event)
{
    ColorDialog::showEvent(event);

    // Set window level above RegionSelector/ScreenCanvas overlays
    raiseWindowAboveOverlays(this);
}

void StyledColorDialog::paintEvent(QPaintEvent* event)
{
    const SnapTray::DialogTheme::Palette palette = SnapTray::DialogTheme::paletteForToolbarStyle();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Draw rounded rectangle background
    painter.setPen(QPen(palette.border, 1));
    painter.setBrush(palette.windowBackground);
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 8, 8);

    // Don't call base class - we're fully custom painting
    // ColorDialog::paintEvent(event);
    Q_UNUSED(event);
}

void StyledColorDialog::mousePressEvent(QMouseEvent* event)
{
    if (m_customTitleBar && event->button() == Qt::LeftButton) {
        // Check if in title bar area (top 32 pixels)
        if (event->position().y() < 32) {
            m_dragging = true;
            m_dragStartPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
            event->accept();
            return;
        }
    }
    ColorDialog::mousePressEvent(event);
}

void StyledColorDialog::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging) {
        move(event->globalPosition().toPoint() - m_dragStartPos);
        event->accept();
        return;
    }
    ColorDialog::mouseMoveEvent(event);
}

void StyledColorDialog::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_dragging) {
        m_dragging = false;
        event->accept();
        return;
    }
    ColorDialog::mouseReleaseEvent(event);
}

bool StyledColorDialog::hasCustomTitleBar() const
{
    return m_customTitleBar;
}

void StyledColorDialog::setCustomTitleBar(bool custom)
{
    if (m_customTitleBar != custom) {
        m_customTitleBar = custom;
        // Would need to rebuild UI to change this at runtime
    }
}

QColor StyledColorDialog::getColor(const QColor& initial, QWidget* parent, const QString& title)
{
    StyledColorDialog dialog(initial, parent);
    if (!title.isEmpty()) {
        dialog.setWindowTitle(title);
        if (dialog.m_titleLabel) {
            dialog.m_titleLabel->setText(title);
        }
    }

    if (dialog.exec() == QDialog::Accepted) {
        return dialog.color();
    }
    return initial;
}

}  // namespace colorwidgets
}  // namespace snaptray
