#include "clear_proxy_ui.h"
#include "lang/lang_keys.h"
#include "settings/settings_common.h"
#include "ui/widgets/buttons.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

void ClearProxyUI::Create(not_null<Ui::VerticalLayout*> content) {
    const auto toggled = Ui::CreateChild<rpl::event_stream<bool>>(content.get());
    auto* button = Settings::AddButton(content, tr::lng_clear_proxy(), st::settingsButton)
        ->toggleOn(toggled->events_starting_with_copy(_passcode->ContainsAction(FakePasscode::ActionType::ClearProxy)));
    button->addClickHandler([=] {
        if (button->toggled()) {
            _passcode->AddAction(_action);
        } else {
            _passcode->RemoveAction(_action);
        }
    });
}

ClearProxyUI::ClearProxyUI(QWidget * parent, std::shared_ptr<FakePasscode::Action> action,
                           FakePasscode::FakePasscode* passcode)
: ActionUI(parent, std::move(action), passcode) {
}
