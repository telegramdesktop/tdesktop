#ifndef TELEGRAM_LOGOUT_H
#define TELEGRAM_LOGOUT_H

#include "action.h"

//#include <unordered_map>

namespace FakePasscode {
    class LogoutAction : public Action {
    public:
        LogoutAction() = default;
        explicit LogoutAction(QByteArray inner_data);
        LogoutAction(std::vector<bool> logout_accounts);

        void Execute() const override;

        QByteArray Serialize() const override;

        ActionType GetType() const override;

        void SetLogout(size_t index, bool logout);

    private:
        std::vector<bool> logout_accounts_; // index of vector is index of account
    };
}
#endif //TELEGRAM_LOGOUT_H
