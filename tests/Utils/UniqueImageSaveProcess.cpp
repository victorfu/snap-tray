#include "ImageSaveUtilsTestAccess.h"
#include <QCoreApplication>
#include <iostream>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 3)
        return 2;
    ImageSaveUtils::UniqueSaveSpec spec;
    spec.outputDir = QString::fromLocal8Bit(argv[1]);
    spec.filenameTemplate = QStringLiteral("same.png");
    QImage image(24, 24, QImage::Format_ARGB32);
    image.fill(QColor(QString::fromLatin1(argv[2])));
    bool first = true;
    const auto result = ImageSaveUtilsTestAccess::save(image, spec,
        [&](const QString& source, const QString& target) {
            if (first) {
                first = false;
                std::cout << "ready" << std::endl;
                std::string line;
                if (!std::getline(std::cin, line) || line != "go")
                    return SnapTray::FilePublishResult{SnapTray::FilePublishStatus::Failed, "barrier failed"};
            }
            return SnapTray::publishFileNoReplace(source, target);
        });
    std::cout << result.filePath.toStdString() << std::endl;
    return result.success ? 0 : 1;
}
