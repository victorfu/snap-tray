#ifndef DISKSPACEGUARD_H
#define DISKSPACEGUARD_H

#include <QtGlobal>

#include <optional>

namespace SnapTray {

/**
 * Decides, from the free space of the volume a recording is written to,
 * when to warn and when to stop recording safely.
 *
 * Thresholds follow the recording instead of fixed numbers:
 *   safe stop = floor + finalize reserve (1 % of what is recorded, for the
 *               MP4 index and sidecar) + kSafeStopSeconds at the recent
 *               write rate
 *   warning   = safe stop + kWarningSeconds at the recent write rate
 *               + the recording's size again when an export is likely
 *               (preview on: trimming, cropping or converting writes a
 *               second file next to it)
 * A warning clears only after free space climbs clearly above its
 * threshold, so it does not flap. A safe stop happens once per session.
 * An unknown free space (the probe failed) is reported as Unknown and never
 * treated as unlimited; actual write errors are still handled by the
 * encoder error path.
 */
class DiskSpaceGuard
{
public:
    enum class Level { Ok, Warning, SafeStop, Unknown };

    struct Input {
        std::optional<qint64> freeBytes;  // nullopt when the probe failed
        qint64 recordedBytes = 0;          // written by this recording so far
        qint64 bytesPerSecond = 0;         // recent write rate; 0 if not known yet
        bool exportLikely = false;
    };

    struct Result {
        Level level = Level::Ok;
        qint64 safeStopBytes = 0;
        qint64 warningBytes = 0;
        // Estimated recording time left before the safe stop; -1 when the
        // write rate is unknown (then show the free space, not minutes).
        qint64 secondsLeft = -1;
    };

    static constexpr qint64 kFloorBytes = 64LL * 1024 * 1024;
    static constexpr qint64 kFinalizePercent = 1;
    static constexpr qint64 kSafeStopSeconds = 10;
    static constexpr qint64 kWarningSeconds = 120;
    static constexpr qint64 kHysteresisBytes = 64LL * 1024 * 1024;

    static qint64 safeStopThreshold(const Input& input);
    static qint64 warningThreshold(const Input& input);

    Result evaluate(const Input& input);
    Level level() const { return m_level; }
    void reset() { m_level = Level::Ok; }

private:
    Level m_level = Level::Ok;
};

/**
 * Recent write rate of a growing file, from (size, time) samples. Uses an
 * exponential moving average so a burst (for example the encoder flushing)
 * does not swing the estimate.
 */
class WriteRateEstimator
{
public:
    void addSample(qint64 fileBytes, qint64 timeMs);
    qint64 bytesPerSecond() const { return m_rate; }
    void reset();

private:
    qint64 m_lastBytes = -1;
    qint64 m_lastTimeMs = -1;
    qint64 m_rate = 0;
};

} // namespace SnapTray

#endif // DISKSPACEGUARD_H
