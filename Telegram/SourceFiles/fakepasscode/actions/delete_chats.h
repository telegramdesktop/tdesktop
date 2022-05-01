#ifndef TDESKTOP_DELETE_CHATS_H
#define TDESKTOP_DELETE_CHATS_H

#include "fakepasscode/multiaccount_action.h"

namespace FakePasscode {
    class DeleteChatsAction final : public MultiAccountAction<SelectPeersData> {
    public:
        using MultiAccountAction::MultiAccountAction;
        void ExecuteAccountAction(int index, Main::Account* account, const SelectPeersData& action) override;
        ActionType GetType() const override;
    };
}

#endif //TDESKTOP_DELETE_CHATS_H
