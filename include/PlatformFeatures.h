#ifndef PLATFORMFEATURES_H
#define PLATFORMFEATURES_H

#include <QObject>
#include <QString>
#include <QIcon>

class OCRManager;
class WindowDetector;

class PlatformFeatures
{
public:
    static PlatformFeatures& instance();

    bool isOCRAvailable() const;
    bool isWindowDetectionAvailable() const;

    OCRManager* createOCRManager(QObject* parent = nullptr) const;
    WindowDetector* createWindowDetector(QObject* parent = nullptr) const;

    QString platformName() const;
    QIcon createTrayIcon() const;

private:
    PlatformFeatures();
    ~PlatformFeatures();

    PlatformFeatures(const PlatformFeatures&) = delete;
    PlatformFeatures& operator=(const PlatformFeatures&) = delete;

    bool m_ocrAvailable;
    bool m_windowDetectionAvailable;
};

#endif // PLATFORMFEATURES_H
