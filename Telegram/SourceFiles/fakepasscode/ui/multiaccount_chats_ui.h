#ifndef TELEGRAM_MULTIACCOUNT_CHATS_UI_H
#define TELEGRAM_MULTIACCOUNT_CHATS_UI_H

#include "action_ui.h"
#include "fakepasscode/multiaccount_action.h"

namespace Ui {
    class SettingsButton;
}

namespace Dialogs {
class Row;
}

class SelectChatsContent;

class MultiAccountSelectChatsUi : public ActionUI {
public:
    using ButtonHandler = std::function<FakePasscode::SelectPeersData(
            not_null<Ui::SettingsButton *>, not_null<Dialogs::Row*>, FakePasscode::SelectPeersData)>;

    struct Description {
        QString name;
        FakePasscode::ActionType action_type;
        std::function<rpl::producer<QString>()> title;
        std::function<rpl::producer<QString>()> popup_window_title;
        std::function<rpl::producer<QString>(gsl::not_null<Main::Account*>)> account_title;
        ButtonHandler button_handler;
    };
    static rpl::producer<QString> DefaultAccountNameFormat(gsl::not_null<Main::Account*> account);

    MultiAccountSelectChatsUi(QWidget* parent, gsl::not_null<Main::Domain*> domain, size_t index,
                              Description description);

    void Create(not_null<Ui::VerticalLayout*> content,
                Window::SessionController* controller = nullptr) override;

private:
    using Action = FakePasscode::MultiAccountAction<FakePasscode::SelectPeersData>;
    Description _description;
    Action* _action = nullptr;
};


#endif //TELEGRAM_MULTIACCOUNT_CHATS_UI_H
