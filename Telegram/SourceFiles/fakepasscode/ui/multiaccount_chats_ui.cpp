#include "multiaccount_chats_ui.h"

#include "fakepasscode/log/fake_log.h"

#include "core/application.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "data/data_user.h"
#include "storage/storage_domain.h"
#include "settings/settings_common.h"
#include "ui/widgets/buttons.h"
#include "lang/lang_keys.h"
#include "ui/widgets/input_fields.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "boxes/abstract_box.h"
#include "ui/text/text_utilities.h"
#include "data/data_session.h"
#include "window/window_session_controller.h"

#include "fakepasscode/actions/delete_chats.h"

using Action = FakePasscode::MultiAccountAction<FakePasscode::SelectPeersData>;

using ButtonHandler = MultiAccountSelectChatsUi::ButtonHandler;

class SelectChatsContentBox : public Ui::BoxContent {
public:
    SelectChatsContentBox(QWidget* parent,
                          Main::Domain* domain, Action* action,
                          qint64 index,
                          MultiAccountSelectChatsUi::Description* description);

protected:
    void prepare() override;

private:
    Main::Domain* domain_;
    Action* action_;
    qint64 index_;
    MultiAccountSelectChatsUi::Description* description_;
};

SelectChatsContentBox::SelectChatsContentBox(QWidget *,
                                             Main::Domain* domain, Action* action,
                                             qint64 index,
                                             MultiAccountSelectChatsUi::Description* description)
        : domain_(domain)
        , action_(action)
        , index_(index)
        , description_(description) {
}

class SelectChatsContent : public Ui::RpWidget {
public:
    SelectChatsContent(QWidget *parent,
                       Main::Domain* domain, Action* action,
                       SelectChatsContentBox* outerBox, qint64 index,
                       MultiAccountSelectChatsUi::Description* description,
                       FakePasscode::SelectPeersData data = {});

    void setupContent();

private:
    Main::Domain* domain_;
    Action* action_;
    SelectChatsContentBox* outerBox_;
    std::vector<Ui::SettingsButton*> buttons_;
    qint64 index_;
    MultiAccountSelectChatsUi::Description* description_;
    FakePasscode::SelectPeersData data_;
};

SelectChatsContent::SelectChatsContent(QWidget *parent,
                                       Main::Domain* domain, Action* action,
                                       SelectChatsContentBox* outerBox, qint64 index,
                                       MultiAccountSelectChatsUi::Description* description,
                                       FakePasscode::SelectPeersData data)
        : Ui::RpWidget(parent)
        , domain_(domain)
        , action_(action)
        , outerBox_(outerBox)
        , index_(index)
        , description_(description)
        , data_(std::move(data)) {
}

void SelectChatsContentBox::prepare() {
    using namespace Settings;
    addButton(tr::lng_close(), [=] { closeBox(); });
    const auto content =
            setInnerWidget(object_ptr<SelectChatsContent>(this, domain_, action_, this, index_, description_),
                           st::sessionsScroll);
    content->resize(st::boxWideWidth, st::noContactsHeight);
    content->setupContent();
    setDimensions(st::boxWideWidth, st::sessionsHeight);
}

void SelectChatsContent::setupContent() {
    const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
    Settings::AddSubsectionTitle(content, description_->popup_window_title());

    for (auto chat : domain_->active().session().data().chatsList()->indexed()->all()) {
        auto button = Settings::AddButton(content, rpl::single(chat->entry()->chatListName()), st::settingsButton);
        button->toggleOn(rpl::single(false));
        button->addClickHandler([this, chat, button] {
            data_ = description_->button_handler(button, chat, data_);
            action_->UpdateOrAddAction(index_, data_);
            domain_->local().writeAccounts();
        });
        buttons_.push_back(button);
    }

    Ui::ResizeFitChild(this, content);
}

MultiAccountSelectChatsUi::MultiAccountSelectChatsUi(QWidget *parent, gsl::not_null<Main::Domain*> domain, size_t index, Description description)
        : ActionUI(parent, domain, index)
        , _description(std::move(description)) {
    if (auto* action = domain->local().GetAction(index, _description.action_type); action != nullptr) {
        _action = dynamic_cast<Action*>(action);
    } else {
        _action = dynamic_cast<Action*>(
                _domain->local().AddAction(_index, _description.action_type));
    }
}

void MultiAccountSelectChatsUi::Create(not_null<Ui::VerticalLayout *> content,
                                       Window::SessionController* controller) {
    Expects(controller != nullptr);
    Settings::AddSubsectionTitle(content, _description.title());
    const auto& accounts = Core::App().domain().accounts();
    account_buttons_.resize(accounts.size());
    size_t idx = 0;
    for (const auto&[index, account]: accounts) {
        auto button = Settings::AddButton(
                content,
                _description.account_title(account),
                st::settingsButton
        );
        account_buttons_[idx] = button;

        button->addClickHandler([index = index, button, controller, this] {
            FAKE_LOG(qsl("%1: Set  %2 to %3").arg(_description.name).arg(index).arg(button->toggled()));
            if (button->toggled()) {
                _action->AddAction(index, FakePasscode::SelectPeersData{});
            } else {
                _action->RemoveAction(index);
            }

            _domain->local().writeAccounts();
            controller->show(Box<SelectChatsContentBox>(_domain, _action, index, &_description));
        });
        ++idx;
    }
}

rpl::producer<QString> MultiAccountSelectChatsUi::DefaultAccountNameFormat(const std::unique_ptr<Main::Account>& account) {
    auto user = account->session().user();
    return rpl::single(user->firstName + " " + user->lastName);
}

