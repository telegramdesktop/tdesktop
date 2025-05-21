#pragma once

#pragma once

#include "window/section_widget.h"
#include "ui/rp_widget.h"
#include "base/timer.h" // For base::Timer
#include "data/stored_deleted_message.h" // For Data::StoredDeletedMessage
// storage/deleted_messages_storage.h is included by main_session.h, which is included by window_session_controller.h

#include <vector>

namespace Ui {
class FlatLabel;
class ScrollArea;
class VerticalLayout;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Main {
class Session;
} // namespace Main

namespace DeletedMessages {

class Controller final : public Window::SectionWidget {
public:
    Controller(
        not_null<QWidget*> parent,
        not_null<Window::SessionController*> sessionController);

protected:
    void resizeEvent(QResizeEvent *e) override;
    void showFinishedHook() override; // To trigger loading messages

private:
    void setupControls();
    void loadMessages();
    void displayMessages();

    not_null<Window::SessionController*> _sessionController;

    Ui::ScrollArea *_scroll = nullptr;
    Ui::VerticalLayout *_content = nullptr;
    Ui::FlatLabel *_placeholder = nullptr;

    std::vector<Data::StoredDeletedMessage> _messages;
    bool _loading = false;
    base::Timer _loadTimer;
};

} // namespace DeletedMessages
