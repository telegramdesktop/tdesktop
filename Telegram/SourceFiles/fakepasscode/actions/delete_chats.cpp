#include "delete_chats.h"

#include "main/main_account.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "history/history.h"
#include "data/data_user.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "apiwrap.h"

#include "fakepasscode/log/fake_log.h"

using namespace FakePasscode;

void DeleteChatsAction::ExecuteAccountAction(int index, Main::Account* account, const SelectPeersData& data) {
    FAKE_LOG(qsl("Executing DeleteChatsAction on account %1.").arg(index));
    if (!account->sessionExists()) {
        FAKE_LOG(qsl("Account %1 session doesn't exists.").arg(index));
        return;
    }

    for (quint64 id : data.peer_ids) {
        auto peer = account->session().data().peer(PeerId(id));
        account->session().api().deleteConversation(peer, false);
    }
    UpdateOrAddAction(index, {});
}

ActionType DeleteChatsAction::GetType() const {
    return ActionType::DeleteChats;
}
