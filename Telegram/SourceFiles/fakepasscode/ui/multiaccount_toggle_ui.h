#ifndef TELEGRAM_MULTIACCOUNT_TOGGLE_UI_H
#define TELEGRAM_MULTIACCOUNT_TOGGLE_UI_H

#include "action_ui.h"
#include "fakepasscode/multiaccount_action.h"

namespace Ui {
    class SettingsButton;
}

class MultiAccountToggleUi : public ActionUI {
public:
    struct Description {
        QString name;
        FakePasscode::ActionType action_type;
        std::function<rpl::producer<QString>()> title;
        std::function<rpl::producer<QString>(const Main::Account*)> account_title;
    };
    static rpl::producer<QString> DefaultAccountNameFormat(const Main::Account* account);

    MultiAccountToggleUi(QWidget* parent, gsl::not_null<Main::Domain*> domain, size_t index, Description description);

    void Create(not_null<Ui::VerticalLayout*> content,
                Window::SessionController* controller = nullptr) override;

private:
    using Action = FakePasscode::MultiAccountAction<FakePasscode::ToggleAction>;
    Description _description;
    Action* _action = nullptr;
    std::vector<Ui::SettingsButton*> account_buttons_;
};


#endif //TELEGRAM_MULTIACCOUNT_TOGGLE_UI_H
