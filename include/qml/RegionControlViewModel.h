#pragma once

#include <QObject>
#include <QString>

class RegionControlViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool radiusEnabled READ isRadiusEnabled WRITE setRadiusEnabled NOTIFY radiusEnabledChanged)
    Q_PROPERTY(int currentRadius READ currentRadius WRITE setCurrentRadius NOTIFY currentRadiusChanged)
    Q_PROPERTY(int maxRadius READ maxRadius CONSTANT)
    Q_PROPERTY(bool ratioLocked READ isRatioLocked NOTIFY ratioLockChanged)
    Q_PROPERTY(int ratioWidth READ ratioWidth NOTIFY ratioLockChanged)
    Q_PROPERTY(int ratioHeight READ ratioHeight NOTIFY ratioLockChanged)
    Q_PROPERTY(QString ratioText READ ratioText NOTIFY ratioLockChanged)

public:
    explicit RegionControlViewModel(QObject* parent = nullptr);

    bool isRadiusEnabled() const { return m_radiusEnabled; }
    void setRadiusEnabled(bool enabled);

    int currentRadius() const { return m_currentRadius; }
    void setCurrentRadius(int radius);

    int maxRadius() const { return 100; }

    bool isRatioLocked() const { return m_ratioLocked; }
    int ratioWidth() const { return m_ratioWidth; }
    int ratioHeight() const { return m_ratioHeight; }
    QString ratioText() const;

    void setLockedRatio(int w, int h);

    Q_INVOKABLE void toggleRadius();
    Q_INVOKABLE void toggleLock();
    Q_INVOKABLE void incrementRadius();
    Q_INVOKABLE void decrementRadius();

signals:
    void radiusChanged(int radius);
    void radiusEnabledChanged(bool enabled);
    void lockChanged(bool locked);
    void currentRadiusChanged();
    void ratioLockChanged();

private:
    int m_currentRadius = 0;
    bool m_radiusEnabled = false;
    bool m_ratioLocked = false;
    int m_ratioWidth = 1;
    int m_ratioHeight = 1;
};
