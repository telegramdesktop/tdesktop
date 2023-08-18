#include "clear_proxy_ui.h"
#include "lang/lang_keys.h"
#include "settings/settings_common.h"
#include "ui/widgets/buttons.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "main/main_domain.h"
#include "storage/storage_domain.h"
#include "fakepasscode/log/fake_log.h"
#include "styles/style_menu_icons.h"

void ClearProxyUI::Create(not_null<Ui::VerticalLayout*> content,
                          Window::SessionController*) {
    Settings::AddSubsectionTitle(content, tr::lng_clear_proxy());
    const auto toggled = Ui::CreateChild<rpl::event_stream<bool>>(content.get());
    auto *button = Settings::AddButton(content, tr::lng_clear_proxy(), st::settingsButton,
                                       {&st::menuIconForward})
            ->toggleOn(toggled->events_starting_with_copy(
                    _domain->local().ContainsAction(_index, FakePasscode::ActionType::ClearProxy)));
    button->addClickHandler([=] {
        if (button->toggled()) {
            FAKE_LOG(qsl("Add action ClearProxy to %1").arg(_index));
            _domain->local().AddAction(_index, FakePasscode::ActionType::ClearProxy);
        } else {
            FAKE_LOG(qsl("Remove action ClearProxy from %1").arg(_index));
            _domain->local().RemoveAction(_index, FakePasscode::ActionType::ClearProxy);
        }
        _domain->local().writeAccounts();
    });
}

ClearProxyUI::ClearProxyUI(QWidget * parent, gsl::not_null<Main::Domain*> domain, size_t index)
: ActionUI(parent, domain, index) {
}
