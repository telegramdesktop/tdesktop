#ifndef TELEGRAM_CLEAR_CACHE_H
#define TELEGRAM_CLEAR_CACHE_H

#include "action.h"

namespace FakePasscode {
    class ClearCache : public Action {
    public:
        void Execute() const override;

        QByteArray Serialize() const override;

        ActionType GetType() const override;
    };
}


#endif //TELEGRAM_CLEAR_CACHE_H
