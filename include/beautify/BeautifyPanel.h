#ifndef BEAUTIFYPANEL_H
#define BEAUTIFYPANEL_H

#include <QWidget>
#include <QPixmap>
#include "beautify/BeautifySettings.h"

class QSlider;
class QComboBox;
class QPushButton;
class QLabel;
class QCheckBox;
class QTimer;
struct ToolbarStyleConfig;

class BeautifyPanel : public QWidget {
    Q_OBJECT

public:
    explicit BeautifyPanel(QWidget* parent = nullptr);

    void setSourcePixmap(const QPixmap& pixmap);
    BeautifySettings settings() const { return m_settings; }
    void setSettings(const BeautifySettings& settings);

signals:
    void settingsChanged(const BeautifySettings& settings);
    void copyRequested(const BeautifySettings& settings);
    void saveRequested(const BeautifySettings& settings);
    void closeRequested();

protected:
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void setupUI();
    void applyThemeStyles();
    void updatePreview();
    void loadPreset(int index);
    void onSettingChanged();
    void pickColor(QColor& target);

    // State
    BeautifySettings m_settings;
    QPixmap m_sourcePixmap;
    QPixmap m_previewPixmap;

    // Preview area
    QLabel* m_previewLabel = nullptr;

    // Controls
    QComboBox* m_backgroundTypeCombo = nullptr;
    QPushButton* m_gradientStartBtn = nullptr;
    QPushButton* m_gradientEndBtn = nullptr;
    QSlider* m_paddingSlider = nullptr;
    QLabel* m_paddingLabel = nullptr;
    QSlider* m_cornerRadiusSlider = nullptr;
    QLabel* m_cornerRadiusLabel = nullptr;
    QCheckBox* m_shadowCheckbox = nullptr;
    QSlider* m_shadowBlurSlider = nullptr;
    QLabel* m_shadowBlurLabel = nullptr;
    QComboBox* m_aspectRatioCombo = nullptr;

    // Preset buttons
    QVector<QPushButton*> m_presetButtons;

    // Action buttons
    QPushButton* m_copyBtn = nullptr;
    QPushButton* m_saveBtn = nullptr;
    QPushButton* m_closeBtn = nullptr;

    // Preview debounce
    QTimer* m_previewTimer = nullptr;

    // Dragging
    bool m_isDragging = false;
    QPoint m_dragStartPos;

    // Panel dimensions
    static constexpr int PANEL_WIDTH = 320;
    static constexpr int PREVIEW_HEIGHT = 200;
};

#endif // BEAUTIFYPANEL_H
