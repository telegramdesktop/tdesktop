#include "clear_cache_permissions.h"

#ifdef Q_OS_MAC

#include "core/application.h"
#include "main/main_domain.h"
#include "storage/storage_domain.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/abstract_box.h"
#include "lang_auto.h"

#include <dirent.h>
#include <QStandardPaths>

namespace FakePasscode {
    void RequestCacheFolderMacosPermission() {
        if (Core::App().domain().local().cacheFolderPermissionRequested()) {
            return;
        }
        auto box = Ui::MakeInformBox(tr::lng_macos_cache_folder_permission_desc());
        box->lifetime().add([]{
            auto downloadPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
            auto downloadPathLoc = QFile::encodeName(downloadPath);
            auto dir = opendir(downloadPathLoc.constData());
            Core::App().domain().local().cacheFolderPermissionRequested(true);
            if (dir != nullptr) {
                closedir(dir);
            }
        });
        Ui::show(std::move(box), Ui::LayerOption::KeepOther);
    }
}
#endif
