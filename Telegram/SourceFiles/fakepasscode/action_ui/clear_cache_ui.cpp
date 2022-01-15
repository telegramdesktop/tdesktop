#include "clear_cache_ui.h"
#include "lang/lang_keys.h"
#include "settings/settings_common.h"
#include "ui/widgets/buttons.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "main/main_domain.h"
#include "storage/storage_domain.h"

void ClearCacheUI::Create(not_null<Ui::VerticalLayout*> content) {
    Settings::AddSubsectionTitle(content, tr::lng_clear_cache());
    const auto toggled = Ui::CreateChild<rpl::event_stream<bool>>(content.get());
    auto *button = Settings::AddButton(content, tr::lng_clear_cache(), st::settingsButton)
            ->toggleOn(toggled->events_starting_with_copy(
                    _domain->local().ContainsAction(_index, FakePasscode::ActionType::ClearCache)));
    button->addClickHandler([=] {
        if (button->toggled()) {
            _domain->local().AddAction(_index, _action);
        } else {
            _domain->local().RemoveAction(_index, _action);
        }
    });
}

ClearCacheUI::ClearCacheUI(QWidget * parent, std::shared_ptr<FakePasscode::Action> action,
                           gsl::not_null<Main::Domain*> domain, size_t index)
        : ActionUI(parent, std::move(action), domain, index) {
}
