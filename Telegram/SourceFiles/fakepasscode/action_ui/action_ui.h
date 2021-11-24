#ifndef TELEGRAM_ACTION_UI_H
#define TELEGRAM_ACTION_UI_H

#include <memory>
#include <ui/wrap/vertical_layout_reorder.h>

#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "fakepasscode/fake_passcode.h"
#include "fakepasscode/action.h"
#include "ui/wrap/vertical_layout.h"

class ActionUI: public Ui::RpWidget {
public:
    ActionUI(QWidget* parent, std::shared_ptr<FakePasscode::Action> action,
             FakePasscode::FakePasscode passcode, size_t index);

    virtual void Create(not_null<Ui::VerticalLayout*> content) = 0;

protected:
    QWidget* _parent;
    std::shared_ptr<FakePasscode::Action> _action;
    FakePasscode::FakePasscode _passcode;
    size_t _index;
};

object_ptr<ActionUI> GetUIByAction(std::shared_ptr<FakePasscode::Action> action,
                                   FakePasscode::FakePasscode passcode, size_t index,
                                   QWidget* parent);

#endif //TELEGRAM_ACTION_UI_H
