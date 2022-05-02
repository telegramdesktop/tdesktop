#include "delete_contacts.h"

#include "fakepasscode/log/fake_log.h"

#include "main/main_account.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "apiwrap.h"

using namespace FakePasscode;

void DeleteContactsAction::ExecuteAccountAction(int index, Main::Account* account, const ToggleAction&) {
    //TODO check with logout
    FAKE_LOG(qsl("Executing DeleteContactsAction on account %1.").arg(index));
    if (!account->sessionExists()) {
        FAKE_LOG(qsl("Account %1 session doesn't exists.").arg(index));
        return;
    }

    QVector<MTPInputUser> contacts;
    auto& sessionData = account->session().data();
    for (auto row : sessionData.contactsList()->all()) {
        if (auto history = row->history()) {
            if (auto userData = history->peer->asUser()) {
                contacts.push_back(userData->inputUser);
            }
        }
    }

    sessionData.clearContacts();
    
    /*auto onFail = [](const MTP::Error& error){
        FAKE_LOG(qsl("DeleteContactsAction: error(%1):%2 %3").arg(error.code()).arg(error.type()).arg(error.description()));
    };*/

    //auto r = account->session().api().request(MTPcontacts_ResetSaved());
    /*account->session().api().request(MTPcontacts_ResetSaved())
        .fail(onFail)
        .send();

    auto weak_acc = base::make_weak(account);
    account->session().api().request(MTPcontacts_DeleteContacts(
        MTP_vector<MTPInputUser>(std::move(contacts))
    )).done([weak_acc](const MTPUpdates &result){
        if (weak_acc && weak_acc->sessionExists()) {
            auto& session = weak_acc->session();
            session.data().clearContacts();
            session.api().applyUpdates(result);
            session.api().requestContacts();
        }
    }).fail(onFail)
      .send();*/
    auto onFail = [](const MTP::Error& error, const MTP::Response& response) -> bool {
        FAKE_LOG(qsl("DeleteContactsAction: error(%1):%2 %3").arg(error.code()).arg(error.type()).arg(error.description()));
        return true;
    };
    account->mtp().send(MTPcontacts_ResetSaved(), nullptr, onFail);
    auto weak_acc = base::make_weak(account);
    account->mtp().send(MTPcontacts_DeleteContacts(
        MTP_vector<MTPInputUser>(std::move(contacts))
    ), [=](const MTP::Response& response) -> bool {
        if (weak_acc && weak_acc->sessionExists()) {
            auto& session = weak_acc->session();
            session.data().clearContacts();
            //session.api().applyUpdates(result);
            session.api().requestContacts();
        }
        return true;
    }, onFail);
}

ActionType DeleteContactsAction::GetType() const {
    return ActionType::DeleteContacts;
}
