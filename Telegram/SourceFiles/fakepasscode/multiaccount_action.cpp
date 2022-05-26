#include "multiaccount_action.hpp"

#include "log/fake_log.h"

#include "core/application.h"
#include "main/main_domain.h"
#include "main/main_account.h"

using namespace FakePasscode;

void LogoutSubscribedAction::Prepare() {
    SubscribeOnLoggingOut();
    Core::App().domain().accountsChanges() | rpl::start_with_next([this] {
        SubscribeOnLoggingOut();
    }, lifetime_);
}

void LogoutSubscribedAction::SubscribeOnLoggingOut() {
    sub_lifetime_.destroy();
    for (const auto&[index, account] : Core::App().domain().accounts()) {
        FAKE_LOG(qsl("Action %1 subscribes on logout for account %2.").arg(int(GetType())).arg(index));
        account->sessionChanges()
                | rpl::filter([](const Main::Session* session) -> bool {
                    return session == nullptr;
                })
                | rpl::take(1)
                | rpl::start_with_next([index = index, this] (const Main::Session*) {
                    FAKE_LOG(qsl("Account %1 logged out, calling OnAccountLoggedOut for action %2.").arg(index).arg(int(GetType())));
                    OnAccountLoggedOut(index);
                }, sub_lifetime_);
    }
}

namespace FakePasscode {
    template class MultiAccountAction<ToggleAction>;
    template class MultiAccountAction<SelectPeersData>;
}
