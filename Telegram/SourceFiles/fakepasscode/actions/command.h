#ifndef TELEGRAM_COMMAND_H
#define TELEGRAM_COMMAND_H

#include <QString>
#include <QProcess>

#include "../action.h"

namespace FakePasscode {

class CommandAction final : public Action {
public:
    CommandAction() = default;
    explicit CommandAction(QByteArray inner_data);
    CommandAction(QString command);

    void Execute() override;

    QByteArray Serialize() const override;

    ActionType GetType() const override;

    const QString& GetCommand() const;
    void SetCommand(QString command);

private:
    QString command_;

#ifdef Q_OS_WIN
	// Create memory leak for Windows for avoiding termination instead of startDetached.
	// startDetached will show console on Windows, so we need something in between start and startDetached.
	// see https://stackoverflow.com/questions/33874243/qprocessstartdetached-but-hide-console-window
	QProcess* process_ = nullptr;
#endif // Q_OS_WIN
};

} // FakePasscode

#endif //TELEGRAM_COMMAND_H
