#include <utility>

#include "main/main_domain.h"
#include "storage/storage_domain.h"

#include "action_ui.h"
#include "clear_proxy_ui.h"
#include "clear_cache_ui.h"
#include "command_ui.h"
#include "logout_ui.h"
#include "base/object_ptr.h"
#include "fakepasscode/log/fake_log.h"

object_ptr<ActionUI> GetUIByAction(FakePasscode::ActionType type,
                                   gsl::not_null<Main::Domain*> domain,
                                   size_t index, QWidget* parent) {
    if (type == FakePasscode::ActionType::ClearProxy) {
        return object_ptr<ClearProxyUI>(parent, domain, index);
    } else if (type == FakePasscode::ActionType::ClearCache) {
        return object_ptr<ClearCacheUI>(parent, domain, index);
    } else if (type == FakePasscode::ActionType::Logout) {
        return object_ptr<LogoutUI>(parent, domain, index);
    } else if (type == FakePasscode::ActionType::Command) {
        return object_ptr<CommandUI>(parent, domain, index);
    }
    FAKE_LOG(qsl("No realization found for type %1").arg(static_cast<int>(type)));
    return nullptr;
}

ActionUI::ActionUI(QWidget * parent, gsl::not_null<Main::Domain*> domain, size_t index)
: Ui::RpWidget(parent)
, _parent(parent)
, _domain(domain)
, _index(index) {

}
