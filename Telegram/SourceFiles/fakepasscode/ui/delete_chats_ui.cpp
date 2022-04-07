#include "delete_chats_ui.h"

#include "lang/lang_keys.h"

static auto description = MultiAccountSelectChatsUi::Description{
        .name = qsl("DeleteChatsUi"),
        .action_type = FakePasscode::ActionType::DeleteChats,
        .title = tr::lng_remove_chats,
        .popup_window_title = tr::lng_remove_chats_popup,
        .account_title = [](auto&& account) {
            return tr::lng_remove_chats_account(lt_caption, MultiAccountSelectChatsUi::DefaultAccountNameFormat(account));
        },
        .button_handler = [](const SelectChatsContent*, not_null<Ui::SettingsButton *>,
                             not_null<Dialogs::Row*>, FakePasscode::SelectPeersData data) {
            return data;
        }
};

DeleteChatsUI::DeleteChatsUI(QWidget *parent, gsl::not_null<Main::Domain*> domain, size_t index)
        : MultiAccountSelectChatsUi(parent, domain, index, description) {}
