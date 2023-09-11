#include "command_ui.h"
#include "settings/settings_common.h"
#include "lang/lang_keys.h"
#include "ui/widgets/fields/input_field.h"
#include "main/main_domain.h"
#include "storage/storage_domain.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

CommandUI::CommandUI(QWidget *parent, gsl::not_null<Main::Domain*> domain, size_t index)
: ActionUI(parent, domain, index)
, _command(nullptr)
, command_field_(nullptr) {
    if (auto* action = domain->local().GetAction(index, FakePasscode::ActionType::Command); action != nullptr) {
        _command = dynamic_cast<FakePasscode::CommandAction*>(action);
    }
}

void CommandUI::Create(not_null<Ui::VerticalLayout *> content,
                       Window::SessionController*) {
    Settings::AddSubsectionTitle(content, tr::lng_command());
    command_field_ = content->add(object_ptr<Ui::InputField>(this, st::defaultInputField, tr::lng_command_prompt()));
    if (_command) {
        command_field_->setText(_command->GetCommand());
    }
    command_field_->submits(
    ) | rpl::start_with_next([=] {
        const bool hasText = command_field_->hasText();
        if (hasText && !_command) {
            _command = dynamic_cast<FakePasscode::CommandAction*>(_domain->local().AddOrGetIfExistsAction(_index, FakePasscode::ActionType::Command));
        } else if (!hasText) {
            _domain->local().RemoveAction(_index, FakePasscode::ActionType::Command);
            _command = nullptr;
        }

        if (_command) {
            _command->SetCommand(command_field_->getLastText());
        }
        _domain->local().writeAccounts();
        command_field_->clearFocus();
    }, command_field_->lifetime());
}

void CommandUI::resizeEvent(QResizeEvent *e) {
    Ui::RpWidget::resizeEvent(e);

    int32 w = st::boxWidth - st::boxPadding.left() - st::boxPadding.right();
    command_field_->resize(w, command_field_->height());
    command_field_->moveToLeft(st::boxPadding.left(), command_field_->pos().y());
}
