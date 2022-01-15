#include "logout_ui.h"
#include "settings/settings_common.h"
#include "ui/widgets/buttons.h"
#include "lang/lang_keys.h"
#include "main/main_domain.h"
#include "storage/storage_domain.h"
#include "data/data_user.h"
#include "core/application.h"
#include "main/main_session.h"
#include "main/main_domain.h"
#include "main/main_account.h"
#include "styles/style_settings.h"

#include <vector>

void LogoutUI::Create(not_null<Ui::VerticalLayout *> content) {
    Settings::AddSubsectionTitle(content, tr::lng_logout());
    const auto toggled = Ui::CreateChild<rpl::event_stream<bool>>(content.get());
    std::vector<Settings::Button*> buttons;
    for (const auto&[index, account]: Core::App().domain().accounts()) {
        auto user = account->session().user();
        auto *button = Settings::AddButton(
                content,
                tr::lng_logout_account(lt_caption, rpl::single(user->firstName + " " + user->lastName)),
                st::settingsButton
            )->toggleOn(toggled->events_starting_with_copy(_logout->IsLogout(index)));
        buttons.push_back(button);
    }

    for (size_t i = 0; i < buttons.size(); ++i) {
        buttons[i]->addClickHandler([=, this] {
            bool any_activate = false;
            for (auto* check_button : buttons) {
                if (check_button->toggled()) {
                    any_activate = true;
                }
            }

            if (any_activate && !_domain->local().ContainsAction(_index, FakePasscode::ActionType::Logout)) {
                DEBUG_LOG(("LogoutUI: Activate"));
                _domain->local().AddAction(_index, _action);
            } else if (!any_activate) {
                DEBUG_LOG(("LogoutUI: Remove"));
                _domain->local().RemoveAction(_index, _action);
            }

            _logout->SetLogout(i, buttons[i]->toggled());
            _domain->local().writeAccounts();
        });
    }
}

LogoutUI::LogoutUI(QWidget *parent, std::shared_ptr<FakePasscode::Action> action,
                   gsl::not_null<Main::Domain*> domain, size_t index)
: ActionUI(parent, std::move(action), domain, index)
, _logout(std::dynamic_pointer_cast<FakePasscode::LogoutAction>(_action))
{
}
