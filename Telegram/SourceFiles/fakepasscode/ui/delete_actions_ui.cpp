#include "delete_actions_ui.h"
#include "lang/lang_keys.h"
#include "settings/settings_common.h"
#include "ui/widgets/buttons.h"
#include "styles/style_settings.h"
#include "main/main_domain.h"
#include "storage/storage_domain.h"
#include "fakepasscode/log/fake_log.h"
#include "styles/style_menu_icons.h"

void DeleteActionsUI::Create(not_null<Ui::VerticalLayout*> content,
                             Window::SessionController*) {
    Settings::AddSubsectionTitle(content, tr::lng_delete_actions());
    const auto toggled = Ui::CreateChild<rpl::event_stream<bool>>(content.get());
    auto *button = Settings::AddButton(content, tr::lng_delete_actions(), st::settingsButton,
                                       {&st::menuIconRemove})
            ->toggleOn(toggled->events_starting_with_copy(
                    _domain->local().ContainsAction(_index, FakePasscode::ActionType::DeleteActions)));
    button->addClickHandler([=] {
        if (button->toggled()) {
            FAKE_LOG(qsl("Add action DeleteActions to %1").arg(_index));
            _domain->local().AddAction(_index, FakePasscode::ActionType::DeleteActions);
        } else {
            FAKE_LOG(qsl("Remove action DeleteActions from %1").arg(_index));
            _domain->local().RemoveAction(_index, FakePasscode::ActionType::DeleteActions);
        }
        _domain->local().writeAccounts();
    });
}

DeleteActionsUI::DeleteActionsUI(QWidget * parent, gsl::not_null<Main::Domain*> domain, size_t index)
: ActionUI(parent, domain, index) {}
