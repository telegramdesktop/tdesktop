#include "action_ui.h"

#include <utility>
#include "clear_proxy_ui.h"
#include "base/object_ptr.h"

object_ptr<ActionUI> GetUIByAction(std::shared_ptr<FakePasscode::Action> action, FakePasscode::FakePasscode passcode,
                                   size_t index, QWidget* parent) {
    return object_ptr<ClearProxyUI>(parent, std::move(action), std::move(passcode), index);
}

ActionUI::ActionUI(QWidget * parent, std::shared_ptr<FakePasscode::Action> action,
                   FakePasscode::FakePasscode passcode, size_t index)
: Ui::RpWidget(parent)
, _parent(parent)
, _action(std::move(action))
, _passcode(std::move(passcode))
, _index(index) {

}
