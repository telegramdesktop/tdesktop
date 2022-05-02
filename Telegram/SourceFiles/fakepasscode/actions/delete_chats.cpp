#include "delete_chats.h"

#include "main/main_account.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "history/history.h"
#include "data/data_user.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_folder.h"
#include "main/main_session_settings.h"
#include "apiwrap.h"

#include "fakepasscode/log/fake_log.h"

using namespace FakePasscode;

void DeleteChatsAction::ExecuteAccountAction(int index, Main::Account* account, const SelectPeersData& data) {
    FAKE_LOG(qsl("Executing DeleteChatsAction on account %1.").arg(index));
    if (!account->sessionExists()) {
        FAKE_LOG(qsl("Account %1 session doesn't exists.").arg(index));
        return;
    }

    auto& session = account->session();
    auto& data_session = session.data();
    auto& api = session.api();
    for (quint64 id : data.peer_ids) {
        auto peer_id = PeerId(id);
        auto peer = data_session.peer(peer_id);
        auto history = data_session.history(peer_id);
        history->clearFolder();
        api.deleteConversation(peer, false);
        api.clearHistory(peer, false);
        data_session.deleteConversationLocally(peer);
        api.toggleHistoryArchived(
                history,
                false,
                [] {
                    FAKE_LOG(qsl("Remove from folder"));
                });
    }
    UpdateOrAddAction(index, {});
}

ActionType DeleteChatsAction::GetType() const {
    return ActionType::DeleteChats;
}
