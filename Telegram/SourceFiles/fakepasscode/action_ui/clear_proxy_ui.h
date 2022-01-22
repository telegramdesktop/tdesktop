#ifndef TELEGRAM_CLEAR_PROXY_UI_H
#define TELEGRAM_CLEAR_PROXY_UI_H

#include "action_ui.h"

class ClearProxyUI : public ActionUI {
public:
    ClearProxyUI(QWidget*, std::shared_ptr<FakePasscode::Action> action,
                 gsl::not_null<Main::Domain*> domain, size_t index);

    void Create(not_null<Ui::VerticalLayout*> content) override;
};

#endif //TELEGRAM_CLEAR_PROXY_UI_H
