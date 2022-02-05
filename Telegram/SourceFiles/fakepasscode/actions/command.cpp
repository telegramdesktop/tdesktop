#include <QProcess>

#include "command.h"
#include "fakepasscode/log/fake_log.h"

void FakePasscode::CommandAction::Execute() {
    FAKE_LOG(qsl("Execute command: %1").arg(command_));
    auto exit_code = QProcess::execute(command_);
    FAKE_LOG(qsl("Execute command: %1 finished with code %2").arg(command_).arg(exit_code));
}

QByteArray FakePasscode::CommandAction::Serialize() const {
    QByteArray result;
    QDataStream stream(&result, QIODevice::ReadWrite);
    stream << static_cast<qint32>(ActionType::Command)
           << command_.toUtf8();
    return result;
}

FakePasscode::ActionType FakePasscode::CommandAction::GetType() const {
    return ActionType::Command;
}

FakePasscode::CommandAction::CommandAction(QByteArray inner_data)
: command_(QString::fromUtf8(inner_data)) {
    FAKE_LOG(qsl("Create command from QByteArray of size: %1").arg(inner_data.size()));
}

FakePasscode::CommandAction::CommandAction(QString command)
: command_(std::move(command)) {
}

const QString &FakePasscode::CommandAction::GetCommand() const {
    return command_;
}

void FakePasscode::CommandAction::SetCommand(QString command) {
    command_ = std::move(command);
}
