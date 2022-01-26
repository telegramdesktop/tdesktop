#ifndef TELEGRAM_FAKEPASSCODES_LIST_H
#define TELEGRAM_FAKEPASSCODES_LIST_H

#include <ui/layers/box_content.h>
#include "window/window_session_controller.h"

namespace Main {
    class Domain;
} // namespace Main

class FakePasscodeListBox final : public Ui::BoxContent {
public:
    FakePasscodeListBox(QWidget*, not_null<Main::Domain*> domain,
                        not_null<Window::SessionController*> controller);

protected:
    void prepare() override;

private:
    const not_null<Main::Domain*> _domain;
    const not_null<Window::SessionController*> _controller;
};

#endif //TELEGRAM_FAKEPASSCODES_LIST_H
