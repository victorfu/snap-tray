#ifndef ANNOTATIONSETTINGSMANAGER_H
#define ANNOTATIONSETTINGSMANAGER_H

#include <QColor>

/**
 * @brief Singleton class for managing annotation settings.
 *
 * This class provides centralized access to annotation-related settings
 * such as color and width, reducing code duplication across
 * RegionSelector and ScreenCanvas.
 */
class AnnotationSettingsManager
{
public:
    static AnnotationSettingsManager& instance();

    // Color settings
    QColor loadColor() const;
    void saveColor(const QColor& color);

    // Width settings
    int loadWidth() const;
    void saveWidth(int width);

    // Default values
    static constexpr int kDefaultWidth = 3;
    static QColor defaultColor() { return Qt::red; }

private:
    AnnotationSettingsManager() = default;
    AnnotationSettingsManager(const AnnotationSettingsManager&) = delete;
    AnnotationSettingsManager& operator=(const AnnotationSettingsManager&) = delete;

    static constexpr const char* kSettingsKeyColor = "annotationColor";
    static constexpr const char* kSettingsKeyWidth = "annotationWidth";
};

#endif // ANNOTATIONSETTINGSMANAGER_H
