#include "AudioFileWriter.h"
#include <QDebug>
#include <QtEndian>

AudioFileWriter::AudioFileWriter(QObject *parent)
    : QObject(parent)
{
}

AudioFileWriter::~AudioFileWriter()
{
    if (m_file.isOpen()) {
        finish();
    }
}

bool AudioFileWriter::start(const QString &filePath, const AudioFormat &format)
{
    QMutexLocker locker(&m_mutex);

    if (m_file.isOpen()) {
        qWarning() << "AudioFileWriter: Already writing to a file";
        return false;
    }

    m_filePath = filePath;
    m_format = format;
    m_dataSize = 0;

    m_file.setFileName(filePath);
    if (!m_file.open(QIODevice::WriteOnly)) {
        emit error(QString("Failed to open audio file: %1").arg(m_file.errorString()));
        return false;
    }

    // Write placeholder header (will be updated in finish())
    writeHeader();

    qDebug() << "AudioFileWriter: Started writing to" << filePath
             << "- Format:" << m_format.sampleRate << "Hz,"
             << m_format.channels << "ch,"
             << m_format.bitsPerSample << "bit";

    return true;
}

void AudioFileWriter::writeAudioData(const QByteArray &data)
{
    QMutexLocker locker(&m_mutex);

    if (!m_file.isOpen()) {
        return;
    }

    qint64 written = m_file.write(data);
    if (written != data.size()) {
        emit error(QString("Failed to write audio data: %1").arg(m_file.errorString()));
        return;
    }

    m_dataSize += written;
}

bool AudioFileWriter::finish()
{
    QMutexLocker locker(&m_mutex);

    if (!m_file.isOpen()) {
        qWarning() << "AudioFileWriter::finish called but file is not open";
        return false;
    }

    // Update the header with final sizes
    bool headerUpdated = updateHeader();

    // Always close the file, regardless of header update result
    m_file.close();

    if (!headerUpdated) {
        QString warningMsg = tr("WAV header update failed - audio file may be incomplete");
        qWarning() << "AudioFileWriter:" << warningMsg;
        emit warning(warningMsg);
    }

    // Calculate duration locally to avoid deadlock (durationMs() also takes mutex)
    int bytesPerSample = m_format.bitsPerSample / 8;
    int bytesPerSecond = m_format.sampleRate * m_format.channels * bytesPerSample;
    qint64 durationMillis = (bytesPerSecond > 0) ? (m_dataSize * 1000) / bytesPerSecond : 0;

    qDebug() << "AudioFileWriter: Finished writing"
             << m_dataSize << "bytes,"
             << durationMillis << "ms";

    return headerUpdated;
}

qint64 AudioFileWriter::durationMs() const
{
    QMutexLocker locker(&m_mutex);

    int bytesPerSample = m_format.bitsPerSample / 8;
    int bytesPerSecond = m_format.sampleRate * m_format.channels * bytesPerSample;

    if (bytesPerSecond == 0) return 0;

    return (m_dataSize * 1000) / bytesPerSecond;
}

void AudioFileWriter::writeHeader()
{
    // WAV file format:
    // RIFF chunk descriptor
    // fmt sub-chunk
    // data sub-chunk

    // We'll write a placeholder header with size 0
    // and update it in finish()

    char header[44];
    memset(header, 0, sizeof(header));

    int bytesPerSample = m_format.bitsPerSample / 8;
    int blockAlign = m_format.channels * bytesPerSample;
    int byteRate = m_format.sampleRate * blockAlign;

    // RIFF chunk descriptor
    memcpy(header + 0, "RIFF", 4);
    // ChunkSize = 36 + SubChunk2Size (placeholder, will be updated)
    qToLittleEndian<quint32>(36, reinterpret_cast<uchar*>(header + 4));
    memcpy(header + 8, "WAVE", 4);

    // fmt sub-chunk
    memcpy(header + 12, "fmt ", 4);
    qToLittleEndian<quint32>(16, reinterpret_cast<uchar*>(header + 16));  // Subchunk1Size (16 for PCM)
    qToLittleEndian<quint16>(1, reinterpret_cast<uchar*>(header + 20));   // AudioFormat (1 = PCM)
    qToLittleEndian<quint16>(m_format.channels, reinterpret_cast<uchar*>(header + 22));
    qToLittleEndian<quint32>(m_format.sampleRate, reinterpret_cast<uchar*>(header + 24));
    qToLittleEndian<quint32>(byteRate, reinterpret_cast<uchar*>(header + 28));
    qToLittleEndian<quint16>(blockAlign, reinterpret_cast<uchar*>(header + 32));
    qToLittleEndian<quint16>(m_format.bitsPerSample, reinterpret_cast<uchar*>(header + 34));

    // data sub-chunk
    memcpy(header + 36, "data", 4);
    // Subchunk2Size = NumSamples * NumChannels * BitsPerSample/8 (placeholder)
    qToLittleEndian<quint32>(0, reinterpret_cast<uchar*>(header + 40));

    m_file.write(header, sizeof(header));
}

bool AudioFileWriter::updateHeader()
{
    // Seek back to update the sizes in the header

    // Update RIFF chunk size (file size - 8)
    if (!m_file.seek(4)) {
        qWarning() << "AudioFileWriter: Failed to seek to RIFF size position -"
                   << m_file.errorString();
        return false;
    }
    quint32 riffSize = static_cast<quint32>(36 + m_dataSize);
    char riffSizeBytes[4];
    qToLittleEndian<quint32>(riffSize, reinterpret_cast<uchar*>(riffSizeBytes));
    if (m_file.write(riffSizeBytes, 4) != 4) {
        qWarning() << "AudioFileWriter: Failed to write RIFF size -"
                   << m_file.errorString();
        return false;
    }

    // Update data chunk size
    if (!m_file.seek(40)) {
        qWarning() << "AudioFileWriter: Failed to seek to data size position -"
                   << m_file.errorString();
        return false;
    }
    quint32 dataSize = static_cast<quint32>(m_dataSize);
    char dataSizeBytes[4];
    qToLittleEndian<quint32>(dataSize, reinterpret_cast<uchar*>(dataSizeBytes));
    if (m_file.write(dataSizeBytes, 4) != 4) {
        qWarning() << "AudioFileWriter: Failed to write data size -"
                   << m_file.errorString();
        return false;
    }

    // Seek to end (non-fatal if this fails, header is already updated)
    if (!m_file.seek(m_file.size())) {
        qDebug() << "AudioFileWriter: Could not seek to end (non-fatal) -"
                 << m_file.errorString();
    }

    return true;
}
