#include "delete_contacts_ui.h"

#include "lang/lang_keys.h"

static auto description = MultiAccountToggleUi::Description{
    .name = qsl("DeleteContactsUi"),
    .action_type = FakePasscode::ActionType::DeleteContacts,
    .title = tr::lng_delete_contacts,
    .account_title = [](auto&& account) {
        return tr::lng_delete_contacts_account(lt_caption, MultiAccountToggleUi::DefaultAccountNameFormat(account));
    },
};

DeleteContactsUi::DeleteContactsUi(QWidget *parent, gsl::not_null<Main::Domain*> domain, size_t index)
    : MultiAccountToggleUi(parent, domain, index, description) {}
