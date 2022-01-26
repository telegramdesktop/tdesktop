#ifndef TELEGRAM_CLEAR_CACHE_H
#define TELEGRAM_CLEAR_CACHE_H

#include "fakepasscode/action.h"

namespace FakePasscode {
    class ClearCache : public Action {
    public:
        void Execute() override;

        QByteArray Serialize() const override;

        ActionType GetType() const override;
    };
}


#endif //TELEGRAM_CLEAR_CACHE_H
