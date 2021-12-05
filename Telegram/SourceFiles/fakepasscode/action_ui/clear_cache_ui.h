#ifndef TELEGRAM_CLEAR_CACHE_UI_H
#define TELEGRAM_CLEAR_CACHE_UI_H

#include "action_ui.h"

class ClearCacheUI : public ActionUI {
public:
    ClearCacheUI(QWidget*, std::shared_ptr<FakePasscode::Action> action,
                 gsl::not_null<Main::Domain*> domain, size_t index);

    void Create(not_null<Ui::VerticalLayout*> content) override;
};


#endif //TELEGRAM_CLEAR_CACHE_UI_H
