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
#include "data/data_folder.h"
#include "window/window_session_controller.h"

#include "fakepasscode/actions/delete_chats.h"
#include "styles/style_menu_icons.h"

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
            setInnerWidget(object_ptr<SelectChatsContent>(this, domain_, action_, this, index_, description_,
                                                          action_->GetData(index_)),
                           st::sessionsScroll);
    content->resize(st::boxWideWidth, st::noContactsHeight);
    content->setupContent();
    setDimensions(st::boxWideWidth, st::sessionsHeight);
}

void SelectChatsContent::setupContent() {
    using ChatWithName = std::pair<not_null<const Dialogs::MainList*>, rpl::producer<QString>>;

    const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
    Settings::AddSubsectionTitle(content, description_->popup_window_title());

    const auto& accounts = domain_->accounts();
    Main::Account* cur_account = nullptr;
    for (const auto&[index, account]: accounts) {
        if (index == index_) {
            cur_account = account.get();
        }
    }
    if (cur_account == nullptr) {
        return;
    }
    const auto& account_data = cur_account->session().data();

    std::vector<ChatWithName> chat_lists;
    if (auto archive_folder = account_data.folderLoaded(Data::Folder::kId)) {
        chat_lists.emplace_back(account_data.chatsList(archive_folder), tr::lng_chats_action_archive());
    }
    chat_lists.emplace_back(account_data.chatsList(), tr::lng_chats_action_main_chats());
    for (const auto&[list, name] : chat_lists) {
        Settings::AddSubsectionTitle(content, name);
        for (auto chat: list->indexed()->all()) {
            if (chat->entry()->fixedOnTopIndex() == Dialogs::Entry::kArchiveFixOnTopIndex) {
                continue; // Archive, skip
            }

            chat->entry()->chatListPreloadData();
            auto button = Settings::AddButton(content, rpl::single(chat->entry()->chatListName()), st::settingsButton);
            Settings::AddDialogImageToButton(button, st::settingsButton, chat);
            auto dialog_id = chat->key().peer()->id.value;
            button->toggleOn(rpl::single(data_.peer_ids.contains(dialog_id)));
            button->addClickHandler([this, chat, button] {
                data_ = description_->button_handler(button, chat, std::move(data_));
                action_->UpdateOrAddAction(index_, data_);
                domain_->local().writeAccounts();
            });
            buttons_.push_back(button);
        }
    }

    Ui::ResizeFitChild(this, content);
}

MultiAccountSelectChatsUi::MultiAccountSelectChatsUi(QWidget *parent, gsl::not_null<Main::Domain*> domain, size_t index, Description description)
        : ActionUI(parent, domain, index)
        , _description(std::move(description)) {
    if (auto* action = domain->local().GetAction(_index, _description.action_type)) {
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
    for (const auto&[index, account] : accounts) {
        Settings::AddButton(
                content,
                _description.account_title(account.get()),
                st::settingsButton,
                {&st::menuIconChannel}
        )->addClickHandler([index = index, controller, this] {
            if (!_action->HasAction(index)) {
                _action->AddAction(index, FakePasscode::SelectPeersData{});
            }

            _domain->local().writeAccounts();
            controller->show(Box<SelectChatsContentBox>(_domain, _action, index, &_description));
        });
    }
}

rpl::producer<QString> MultiAccountSelectChatsUi::DefaultAccountNameFormat(gsl::not_null<Main::Account*> account) {
    auto user = account->session().user();
    return rpl::single(user->firstName + " " + user->lastName);
}

