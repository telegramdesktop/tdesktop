#ifndef TELEGRAM_DELETE_CONTACTS_UI_H
#define TELEGRAM_DELETE_CONTACTS_UI_H

#include "multiaccount_toggle_ui.h"

class DeleteContactsUi final : public MultiAccountToggleUi {
public:
    DeleteContactsUi(QWidget* parent, gsl::not_null<Main::Domain*> domain, size_t index);
};


#endif //TELEGRAM_DELETE_CONTACTS_UI_H
