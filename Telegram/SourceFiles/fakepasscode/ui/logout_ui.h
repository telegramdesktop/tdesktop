#ifndef TELEGRAM_LOGOUT_UI_H
#define TELEGRAM_LOGOUT_UI_H

#include "action_ui.h"
#include "fakepasscode/actions/logout.h"
#include "settings/settings_common.h"

class LogoutUI : public ActionUI {
public:
    LogoutUI(QWidget* parent, gsl::not_null<Main::Domain*> domain, size_t index);

    void Create(not_null<Ui::VerticalLayout*> content,
                Window::SessionController* controller = nullptr) override;

private:
    FakePasscode::LogoutAction* _logout;
    std::vector<Settings::Button*> account_buttons_;
};

#endif //TELEGRAM_LOGOUT_UI_H
