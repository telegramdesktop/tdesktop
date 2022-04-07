#ifndef TELEGRAM_DELETE_ACTIONS_H
#define TELEGRAM_DELETE_ACTIONS_H

#include "fakepasscode/action.h"

namespace FakePasscode {
    class DeleteActions : public Action {
    public:
        void Execute() override;

        QByteArray Serialize() const override;

        ActionType GetType() const override;
    };
}


#endif //TELEGRAM_DELETE_ACTIONS_H
