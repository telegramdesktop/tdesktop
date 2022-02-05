#ifndef TELEGRAM_COMMAND_H
#define TELEGRAM_COMMAND_H

#include <QString>

#include "../action.h"

namespace FakePasscode {

class CommandAction : public Action {
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
};

}

#endif //TELEGRAM_COMMAND_H
