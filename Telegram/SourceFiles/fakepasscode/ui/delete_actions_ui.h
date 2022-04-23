#ifndef TELEGRAM_DELETE_ACTIONS_UI_H
#define TELEGRAM_DELETE_ACTIONS_UI_H

#include "action_ui.h"

class DeleteActionsUI : public ActionUI {
public:
    DeleteActionsUI(QWidget*, gsl::not_null<Main::Domain*> domain, size_t index);

    void Create(not_null<Ui::VerticalLayout*> content,
                Window::SessionController* controller = nullptr) override;
};


#endif //TELEGRAM_DELETE_ACTIONS_UI_H
