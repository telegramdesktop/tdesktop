#include "action_ui.h"

#include <utility>
#include "clear_proxy_ui.h"
#include "base/object_ptr.h"

object_ptr<ActionUI> GetUIByAction(std::shared_ptr<FakePasscode::Action> action, FakePasscode::FakePasscode* passcode,
                                   QWidget* parent) {
    return object_ptr<ClearProxyUI>(parent, std::move(action), passcode);
}

ActionUI::ActionUI(QWidget * parent, std::shared_ptr<FakePasscode::Action> action,
                   FakePasscode::FakePasscode* passcode)
: Ui::RpWidget(parent)
, _parent(parent)
, _action(std::move(action))
, _passcode(passcode) {

}
