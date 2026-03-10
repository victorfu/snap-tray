#include "qml/RegionControlViewModel.h"

RegionControlViewModel::RegionControlViewModel(QObject* parent)
    : QObject(parent)
{
}

void RegionControlViewModel::setRadiusEnabled(bool enabled)
{
    if (m_radiusEnabled == enabled)
        return;

    m_radiusEnabled = enabled;
    emit radiusEnabledChanged(m_radiusEnabled);

    if (!m_radiusEnabled && m_currentRadius != 0) {
        m_currentRadius = 0;
        emit currentRadiusChanged();
        emit radiusChanged(0);
    }
}

void RegionControlViewModel::setCurrentRadius(int radius)
{
    radius = qBound(0, radius, maxRadius());
    if (m_currentRadius == radius)
        return;

    m_currentRadius = radius;
    emit currentRadiusChanged();
    emit radiusChanged(m_currentRadius);
}

QString RegionControlViewModel::ratioText() const
{
    return QStringLiteral("%1:%2").arg(m_ratioWidth).arg(m_ratioHeight);
}

void RegionControlViewModel::setLockedRatio(int w, int h)
{
    if (w > 0 && h > 0) {
        m_ratioWidth = w;
        m_ratioHeight = h;
        emit ratioLockChanged();
    }
}

void RegionControlViewModel::toggleRadius()
{
    m_radiusEnabled = !m_radiusEnabled;
    emit radiusEnabledChanged(m_radiusEnabled);

    if (!m_radiusEnabled && m_currentRadius != 0) {
        m_currentRadius = 0;
        emit currentRadiusChanged();
        emit radiusChanged(0);
    }
}

void RegionControlViewModel::toggleLock()
{
    m_ratioLocked = !m_ratioLocked;
    emit ratioLockChanged();
    emit lockChanged(m_ratioLocked);
}

void RegionControlViewModel::incrementRadius()
{
    int newRadius = qMin(maxRadius(), m_currentRadius + 5);
    if (newRadius != m_currentRadius) {
        m_currentRadius = newRadius;
        emit currentRadiusChanged();
        emit radiusChanged(m_currentRadius);
    }
}

void RegionControlViewModel::decrementRadius()
{
    int newRadius = qMax(0, m_currentRadius - 5);
    if (newRadius != m_currentRadius) {
        m_currentRadius = newRadius;
        emit currentRadiusChanged();
        emit radiusChanged(m_currentRadius);
    }
}
