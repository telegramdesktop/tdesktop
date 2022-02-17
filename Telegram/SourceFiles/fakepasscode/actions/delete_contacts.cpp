#include "delete_contacts.h"

#include "fakepasscode/log/fake_log.h"

using namespace FakePasscode;

void DeleteContactsAction::ExecuteAccountAction(int index, const std::unique_ptr<Main::Account> &account, const ToggleAction &action) {
    FAKE_LOG(qsl("Executing DeleteContactsAction on account %1").arg(index));
    //TODO implement
}

ActionType DeleteContactsAction::GetType() const {
    return ActionType::DeleteContacts;
}
