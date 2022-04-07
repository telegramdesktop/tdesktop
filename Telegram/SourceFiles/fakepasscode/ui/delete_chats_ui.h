#ifndef TELEGRAM_DELETE_CHATS_UI_H
#define TELEGRAM_DELETE_CHATS_UI_H

#include "multiaccount_chats_ui.h"

class DeleteChatsUI final : public MultiAccountSelectChatsUi {
public:
    DeleteChatsUI(QWidget* parent, gsl::not_null<Main::Domain*> domain, size_t index);
};

#endif //TELEGRAM_DELETE_CHATS_UI_H
