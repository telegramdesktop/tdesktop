#ifndef TELEGRAM_DELETE_CONTACTS_H
#define TELEGRAM_DELETE_CONTACTS_H

#include "fakepasscode/multiaccount_action.h"

namespace FakePasscode {
    class DeleteContactsAction final : public MultiAccountAction<ToggleAction> {
    public:
        using MultiAccountAction::MultiAccountAction;
        void ExecuteAccountAction(int index, Main::Account* account, const ToggleAction& action) override;
        ActionType GetType() const override;
    };
}


#endif //TELEGRAM_DELETE_CONTACTS_H
