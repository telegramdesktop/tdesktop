#ifndef TELEGRAM_COMMAND_UI_H
#define TELEGRAM_COMMAND_UI_H

#include "action_ui.h"
#include "fakepasscode/actions/command.h"

class CommandUI : public ActionUI {
public:
    CommandUI(QWidget* parent, gsl::not_null<Main::Domain*> domain, size_t index);

    void Create(not_null<Ui::VerticalLayout*> content,
                Window::SessionController* controller = nullptr) override;

    void resizeEvent(QResizeEvent *e) final;

private:
    FakePasscode::CommandAction* _command;
    Ui::InputField* command_field_;
};

#endif //TELEGRAM_COMMAND_UI_H
