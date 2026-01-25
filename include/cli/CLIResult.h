#ifndef CLI_RESULT_H
#define CLI_RESULT_H

#include <QByteArray>
#include <QString>

namespace SnapTray {
namespace CLI {

/**
 * @brief CLI execution result
 */
struct CLIResult
{
    enum class Code {
        Success = 0,
        GeneralError = 1,
        InvalidArguments = 2,
        FileError = 3,
        InstanceError = 4,
        RecordingError = 5,
    };

    Code code = Code::Success;
    QString message;
    QByteArray data; // For --raw output

    bool isSuccess() const { return code == Code::Success; }

    static CLIResult success(const QString& msg = QString())
    {
        return {Code::Success, msg, {}};
    }

    static CLIResult error(Code code, const QString& msg) { return {code, msg, {}}; }

    static CLIResult withData(const QByteArray& data) { return {Code::Success, {}, data}; }
};

} // namespace CLI
} // namespace SnapTray

#endif // CLI_RESULT_H
