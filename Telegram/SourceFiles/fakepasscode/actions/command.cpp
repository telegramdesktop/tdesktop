#include "command.h"
#include "fakepasscode/log/fake_log.h"

void FakePasscode::CommandAction::Execute() {
    FAKE_LOG(qsl("Execute command: %1").arg(command_));

#ifdef Q_OS_WIN
	QString executed_command;
	if (command_.contains("cmd.exe", Qt::CaseInsensitive)) {
		executed_command = command_;
	} else {
		executed_command = "cmd.exe /k " + command_;
	}
	process_ = new QProcess;
	process_->start(executed_command);
	auto started = true;
#else
	QString executed_command = command_;
    auto started = QProcess::startDetached("bash", {"-c", executed_command});
#endif // Q_OS_WIN

    FAKE_LOG(qsl("Execute command: %1 executed %2").arg(command_).arg(started));
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
