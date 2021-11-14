#ifndef TELEGRAM_ACTION_H
#define TELEGRAM_ACTION_H

#include <QByteArray>
#include <memory>

namespace FakePasscode {
    enum class ActionType {
        ClearProxy = 0
    };

    constexpr std::array<ActionType, 1> kAvailableActions = {{
        ActionType::ClearProxy
    }};

    class Action {
    public:
        virtual ~Action() = default;

        virtual void Execute() const = 0;

        virtual QByteArray Serialize() const = 0;

        virtual ActionType GetType() const = 0;
    };

    std::shared_ptr<Action> DeSerialize(QByteArray serialized);

    std::shared_ptr<Action> CreateAction(ActionType type);
}

#endif //TELEGRAM_ACTION_H
