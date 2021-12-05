#include "action_ui.h"

#include <utility>
#include "clear_proxy_ui.h"
#include "clear_cache_ui.h"
#include "base/object_ptr.h"

object_ptr<ActionUI> GetUIByAction(std::shared_ptr<FakePasscode::Action> action,
                                   gsl::not_null<Main::Domain*> domain,
                                   size_t index, QWidget* parent) {
    if (action->GetType() == FakePasscode::ActionType::ClearProxy) {
        return object_ptr<ClearProxyUI>(parent, std::move(action), domain, index);
    } else if (action->GetType() == FakePasscode::ActionType::ClearCache) {
        return object_ptr<ClearCacheUI>(parent, std::move(action), domain, index);
    }
    return nullptr;
}

ActionUI::ActionUI(QWidget * parent, std::shared_ptr<FakePasscode::Action> action,
                   gsl::not_null<Main::Domain*> domain, size_t index)
: Ui::RpWidget(parent)
, _parent(parent)
, _action(std::move(action))
, _domain(domain)
, _index(index) {

}
