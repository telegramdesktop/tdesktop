#ifndef TELEGRAM_LOGOUT_UI_H
#define TELEGRAM_LOGOUT_UI_H

#include "action_ui.h"
#include "../logout.h"

class LogoutUI : public ActionUI {
public:
    LogoutUI(QWidget* parent, std::shared_ptr<FakePasscode::Action> action,
             gsl::not_null<Main::Domain*> domain, size_t index);

    void Create(not_null<Ui::VerticalLayout*> content) override;

private:
    std::shared_ptr<FakePasscode::LogoutAction> _logout;
};

#endif //TELEGRAM_LOGOUT_UI_H
