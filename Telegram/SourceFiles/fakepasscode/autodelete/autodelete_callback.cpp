#include "autodelete_callback.h"

#include <base/qt/qt_key_modifiers.h>
#include "core/application.h"
#include "main/main_domain.h"
#include "storage/storage_domain.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "data/data_peer.h"
#include "api/api_common.h"
#include "menu/menu_send.h"
#include "mainwidget.h"

#include "fakepasscode/ui/autodelete_box.h"
#include "fakepasscode/log/fake_log.h"

namespace FakePasscode{

bool DisableAutoDeleteInContextMenu() {
    auto& local = Core::App().domain().local();
    bool is_channel = false;
    if (auto* window = Core::App().activeWindow()) {
        if (auto* controller = window->sessionController()) {
            if (auto* peer = controller->content()->peer(); peer &&
                    peer->isChannel() &&
                    !peer->isMegagroup() &&
                    !peer->isGigagroup() &&
                    !peer->isChat()) {
                FAKE_LOG(qsl("We try to send auto deletable to channel. This feature is disabled for now."));
                is_channel = true;
            }
        }
    }
    return is_channel || local.IsFake() || local.GetAutoDelete() == nullptr;
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
