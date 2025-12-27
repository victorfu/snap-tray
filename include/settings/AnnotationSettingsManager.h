#ifndef ANNOTATIONSETTINGSMANAGER_H
#define ANNOTATIONSETTINGSMANAGER_H

#include <QColor>
#include "annotations/StepBadgeAnnotation.h"

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

    // Step Badge size settings
    StepBadgeSize loadStepBadgeSize() const;
    void saveStepBadgeSize(StepBadgeSize size);

    // Default values
    static constexpr int kDefaultWidth = 3;
    static QColor defaultColor() { return Qt::red; }
    static constexpr StepBadgeSize kDefaultStepBadgeSize = StepBadgeSize::Medium;

private:
    AnnotationSettingsManager() = default;
    AnnotationSettingsManager(const AnnotationSettingsManager&) = delete;
    AnnotationSettingsManager& operator=(const AnnotationSettingsManager&) = delete;

    static constexpr const char* kSettingsKeyColor = "annotationColor";
    static constexpr const char* kSettingsKeyWidth = "annotationWidth";
    static constexpr const char* kSettingsKeyStepBadgeSize = "stepBadgeSize";
};

#endif // ANNOTATIONSETTINGSMANAGER_H
