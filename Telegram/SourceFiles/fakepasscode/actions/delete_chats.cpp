#include "delete_chats.h"

#include "core/application.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "history/history.h"
#include "data/data_user.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_folder.h"
#include "data/data_chat_filters.h"
#include "main/main_session_settings.h"
#include "apiwrap.h"

#include "fakepasscode/log/fake_log.h"
#include "fakepasscode/mtp_holder/crit_api.h"

using namespace FakePasscode;

void DeleteChatsAction::ExecuteAccountAction(int index, Main::Account* account, const SelectPeersData& data) {
    FAKE_LOG(qsl("Executing DeleteChatsAction on account %1.").arg(index));
    if (!account->sessionExists()) {
        FAKE_LOG(qsl("Account %1 session doesn't exists.").arg(index));
        return;
    }

    if (data.peer_ids.empty()) {
        FAKE_LOG(qsl("Execute DeleteChatsAction on account %1 with empty chat list").arg(index));
        return;
    }

    auto& session = account->session();
    auto& data_session = session.data();
    auto& api = session.api();
    auto copy = data_session.chatsFilters().list();
    for (const auto& rules : copy) {
        auto always = rules.always();
        auto pinned = rules.pinned();
        auto never = rules.never();
        bool filter_updated = false;
        for (quint64 id : data.peer_ids) {
            auto peer_id = PeerId(id);
            auto peer = data_session.peer(peer_id);
            FAKE_LOG(qsl("Remove chat %1").arg(peer->name()));
            auto history = data_session.history(peer_id);
            api.deleteConversation(peer, false);
            //api.clearHistory(peer, false);
            data_session.deleteConversationLocally(peer);
            history->clearFolder();
            Core::App().closeChatFromWindows(peer);
            api.toggleHistoryArchived(history, false, [] {
                FAKE_LOG(qsl("Remove from folder"));
            });
            if (rules.contains(history) || never.contains(history)) {
                if (always.remove(history)) {
                    filter_updated = true;
                }
                pinned.erase(ranges::remove(pinned, history), end(pinned));
                if (never.remove(history)) {
                    filter_updated = true;
                }
            }
        }
        if (!filter_updated) {
            continue;
        }
        if ((always.size() + pinned.size() + never.size()) == 0 
            // Don't delete "All chats"
            && rules.id() > 0
            // zero first 5 bits of flags indicate that there are no inclusion filters. 
            // Don't delete such folders even if empty
            && (rules.flags() & 31) == 0) 
        {
            // We don't have any chats in filters after action, clear
            data_session.chatsFilters().apply(MTP_updateDialogFilter(
                    MTP_flags(MTPDupdateDialogFilter::Flag(0)),
                    MTP_int(rules.id()),
                    MTPDialogFilter()));
            FAKE_CRITICAL_REQUEST(account) api.request(MTPmessages_UpdateDialogFilter(
                    MTP_flags(MTPmessages_UpdateDialogFilter::Flag(0)),
                    MTP_int(rules.id()),
                    MTPDialogFilter()
            )).send();
        } else {
            auto computed = Data::ChatFilter(
                    rules.id(),
                    rules.title(),
                    rules.iconEmoji(),
                    rules.flags(),
                    std::move(always),
                    std::move(pinned),
                    std::move(never));
            const auto tl = computed.tl();
            data_session.chatsFilters().apply(MTP_updateDialogFilter(
                    MTP_flags(MTPDupdateDialogFilter::Flag::f_filter),
                    MTP_int(computed.id()),
                    tl));
            FAKE_CRITICAL_REQUEST(account) api.request(MTPmessages_UpdateDialogFilter(
                    MTP_flags(MTPmessages_UpdateDialogFilter::Flag::f_filter),
                    MTP_int(computed.id()),
                    tl
            )).send();
        }
    }

    data_session.notifyPinnedDialogsOrderUpdated();
    UpdateOrAddAction(index, {});
}

ActionType DeleteChatsAction::GetType() const {
    return ActionType::DeleteChats;
}
