#include "multiaccount_toggle_ui.h"

#include "fakepasscode/log/fake_log.h"

#include "core/application.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "data/data_user.h"
#include "storage/storage_domain.h"
#include "settings/settings_common.h"
#include "ui/widgets/buttons.h"
#include "styles/style_settings.h"
#include "styles/style_menu_icons.h"

MultiAccountToggleUi::MultiAccountToggleUi(QWidget *parent, gsl::not_null<Main::Domain*> domain, size_t index, Description description)
    : ActionUI(parent, domain, index)
    , _description(std::move(description)) {
    if (auto* action = domain->local().GetAction(index, _description.action_type); action != nullptr) {
        _action = dynamic_cast<Action*>(action);
    }
}

void MultiAccountToggleUi::Create(not_null<Ui::VerticalLayout *> content,
                                  Window::SessionController*) {
    Settings::AddSubsectionTitle(content, _description.title());
    const auto toggled = Ui::CreateChild<rpl::event_stream<bool>>(content.get());
    const auto& accounts = Core::App().domain().accounts();
    account_buttons_.resize(accounts.size());
    size_t idx = 0;
    for (const auto&[index, account]: accounts) {
        auto *button = Settings::AddButton(
                content,
                _description.account_title(account.get()),
                st::settingsButton,
                {&st::menuIconRemove}
        )->toggleOn(toggled->events_starting_with_copy(_action != nullptr && _action->HasAction(index)));
        account_buttons_[idx] = button;

        button->addClickHandler([index = index, button, this] {
            bool any_activate = false;
            for (auto* check_button : account_buttons_) {
                if (check_button->toggled()) {
                    any_activate = true;
                }
            }

            if (any_activate && !_action) {
                FAKE_LOG(qsl("%1: Activate").arg(_description.name));
                _action = dynamic_cast<Action*>(
                        _domain->local().AddAction(_index, _description.action_type));
            } else if (!any_activate) {
                FAKE_LOG(qsl("%1: Remove").arg(_description.name));
                _domain->local().RemoveAction(_index, _description.action_type);
                _action = nullptr;
            }

            if (_action) {
                FAKE_LOG(qsl("%1: Set  %2 to %3").arg(_description.name).arg(index).arg(button->toggled()));
                if (button->toggled()) {
                    _action->AddAction(index, FakePasscode::ToggleAction{});
                } else {
                    _action->RemoveAction(index);
                }
            }
            _domain->local().writeAccounts();
        });
        ++idx;
    }
}

rpl::producer<QString> MultiAccountToggleUi::DefaultAccountNameFormat(const Main::Account* account) {
    auto user = account->session().user();
    return rpl::single(user->firstName + " " + user->lastName);
}
