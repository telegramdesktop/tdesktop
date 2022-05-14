#include "autodelete_callback.h"

#include <base/qt/qt_key_modifiers.h>
#include <core/application.h>
#include <main/main_domain.h>
#include <storage/storage_domain.h>

#include "api/api_common.h"
#include "menu/menu_send.h"

#include "fakepasscode/ui/autodelete_box.h"
#include "fakepasscode/log/fake_log.h"

namespace FakePasscode{

bool DisableAutoDeleteInContextMenu() {
    auto& local = Core::App().domain().local();
    return local.IsFake() || local.GetAutoDelete() == nullptr || !local.hasLocalPasscode();
}

Fn<void()> DefaultAutoDeleteCallback(
        not_null<Ui::RpWidget*> parent,
        Fn<void(object_ptr<Ui::BoxContent>)> show,
        Fn<void(Api::SendOptions)> send) {
    return crl::guard(parent, [=] {
        show(AutoDeleteBox(parent, send));
    });
}

}
