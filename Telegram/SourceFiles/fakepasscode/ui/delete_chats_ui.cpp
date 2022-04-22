#include "delete_chats_ui.h"

#include "dialogs/dialogs_row.h"
#include "settings/settings_common.h"
#include "ui/widgets/buttons.h"
#include "data/data_peer.h"
#include "lang/lang_keys.h"
#include "fakepasscode/log/fake_log.h"

static auto description = MultiAccountSelectChatsUi::Description{
        .name = qsl("DeleteChatsUi"),
        .action_type = FakePasscode::ActionType::DeleteChats,
        .title = tr::lng_remove_chats,
        .popup_window_title = tr::lng_remove_chats_popup,
        .account_title = [](auto&& account) {
            return tr::lng_remove_chats_account(lt_caption, MultiAccountSelectChatsUi::DefaultAccountNameFormat(account));
        },
        .button_handler = [](not_null<Ui::SettingsButton *> button,
                             not_null<Dialogs::Row*> chat, FakePasscode::SelectPeersData data) {
            auto id = chat->key().peer()->id.value;
            if (button->toggled()) {
                FAKE_LOG(qsl("Add new id to delete: %1").arg(id));
                data.peer_ids.insert(id);
            } else {
                FAKE_LOG(qsl("Remove id to delete: %1").arg(id));
                data.peer_ids.remove(id);
            }
            return data;
        }
};

DeleteChatsUI::DeleteChatsUI(QWidget *parent, gsl::not_null<Main::Domain*> domain, size_t index)
        : MultiAccountSelectChatsUi(parent, domain, index, description) {}
